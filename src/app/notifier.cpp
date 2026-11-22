// SPDX-License-Identifier: GPL-3.0-or-later
#include "notifier.h"
#include "logging.h"
#include "routinemanager.h"
#include "settings.h"
#include "taskmodel.h"

#include <KLocalizedString>
#include <KNotification>
#include <QLocale>

using namespace KTasks;

// Reminders older than this are not shown any more when the application starts.
static const int kMaxAgeDays = 7;

Notifier::Notifier(TaskStore *store, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Notifier::check);
    connect(store, &TaskStore::changed, this, [this] {
        closeObsolete();
        // Coalesce bursts of changes (typing, sync) into one check.
        if (!m_timer.isActive() || m_timer.remainingTime() > 300) {
            m_timer.start(300);
        }
    });
    for (auto signal : {&Settings::notifyRemindersChanged, &Settings::notifyAtDueChanged, &Settings::allDayReminderTimeChanged, &Settings::dailySummaryChanged,
                        &Settings::dailySummaryTimeChanged, &Settings::notifyRoutinesChanged}) {
        connect(settings, signal, this, [this] {
            m_timer.start(100);
        });
    }
    m_store->database()->pruneReminderLog(QDateTime::currentDateTimeUtc().addDays(-60));
    m_timer.start(1500); // shortly after start, once the window is up
}

void Notifier::setRoutineManager(RoutineManager *routine)
{
    m_routine = routine;
    if (m_routine) {
        connect(m_routine, &RoutineManager::changed, this, [this] {
            if (!m_timer.isActive() || m_timer.remainingTime() > 300) {
                m_timer.start(300);
            }
        });
    }
}

static QTime parseTime(const QString &s, const QTime &fallback)
{
    const QTime t = QTime::fromString(s, QStringLiteral("HH:mm"));
    return t.isValid() ? t : fallback;
}

QList<Notifier::Due> Notifier::pending(const QDateTime &now, QDateTime *next) const
{
    QList<Due> out;
    QDateTime soonest;
    auto consider = [&](const QDateTime &when) {
        if (when > now && (!soonest.isValid() || when < soonest)) {
            soonest = when;
        }
    };
    if (!m_settings->notifyReminders()) {
        if (next) {
            *next = soonest;
        }
        return out;
    }
    Database *db = m_store->database();
    const QTime allDayTime = parseTime(m_settings->allDayReminderTime(), QTime(9, 0));
    const QDateTime oldest = now.addDays(-kMaxAgeDays);

    QHash<QString, QDateTime> snoozes;
    for (const auto &s : db->snoozes()) {
        snoozes.insert(s.first, s.second);
    }

    for (const Task &t : m_store->tasks()) {
        if (t.isDone()) {
            continue;
        }
        const auto snoozeIt = snoozes.constFind(t.uid);
        if (snoozeIt != snoozes.constEnd()) {
            // A snoozed task stays quiet until the snooze ends, then fires once.
            if (*snoozeIt <= now) {
                out.append(Due{t.id, t.uid, QString(), *snoozeIt, true});
            } else {
                consider(*snoozeIt);
            }
            continue;
        }
        QList<QDateTime> triggers;
        for (const Alarm &a : t.alarms) {
            QDateTime when = t.alarmTime(a);
            if (when.isValid() && t.allDay && a.kind != Alarm::Absolute) {
                // "On the day" for a task without a time means at the configured hour.
                if (a.offsetSecs % 86400 == 0) {
                    when = QDateTime(when.date(), allDayTime);
                }
            }
            if (when.isValid()) {
                triggers.append(when);
            }
        }
        if (triggers.isEmpty() && m_settings->notifyAtDue() && t.due.isValid()) {
            triggers.append(t.allDay ? QDateTime(t.due.date(), allDayTime) : t.due);
        }
        for (const QDateTime &when : std::as_const(triggers)) {
            if (when > now) {
                consider(when);
                continue;
            }
            if (when < oldest) {
                continue;
            }
            const QString key = when.toUTC().toString(Qt::ISODate);
            if (!db->reminderFired(t.uid, key)) {
                out.append(Due{t.id, t.uid, key, when, false});
            }
        }
    }
    if (next) {
        *next = soonest;
    }
    return out;
}

QList<Notifier::RoutineDue> Notifier::pendingRoutines(const QDateTime &now, QDateTime *next) const
{
    QList<RoutineDue> out;
    QDateTime soonest;
    auto consider = [&](const QDateTime &when) {
        if (when > now && (!soonest.isValid() || when < soonest)) {
            soonest = when;
        }
    };
    if (!m_routine || !m_settings->notifyRoutines()) {
        if (next) {
            *next = soonest;
        }
        return out;
    }

    Database *db = m_store->database();
    const QDate today = now.date();
    QHash<QString, QDateTime> snoozes;
    for (const auto &s : db->snoozes()) {
        snoozes.insert(s.first, s.second);
    }

    QHash<qint64, RoutineLogStatus> todayLogs;
    for (const RoutineLog &l : db->routineLogsForDate(today)) {
        todayLogs.insert(l.blockId, l.status);
    }

    for (const RoutineBlock &b : m_routine->allBlocks()) {
        if (!RoutineManager::matchesDate(b, today)) {
            continue;
        }
        const auto st = todayLogs.value(b.id, RoutineLogStatus::Pending);
        if (st != RoutineLogStatus::Pending) {
            continue;
        }
        const QString logUid = QStringLiteral("routine:%1").arg(b.uid);
        const auto snoozeIt = snoozes.constFind(logUid);
        if (snoozeIt != snoozes.constEnd()) {
            if (*snoozeIt <= now) {
                out.append(RoutineDue{b.id, logUid, QString(), *snoozeIt, today, true});
            } else {
                consider(*snoozeIt);
            }
            continue;
        }
        if (!b.notifyEnabled) {
            continue;
        }
        const QDateTime startDt(today, QTime(0, 0).addSecs(b.startMinute * 60));
        const QDateTime trigger = startDt.addSecs(-qint64(b.notifyMinutesBefore) * 60);
        if (trigger > now) {
            consider(trigger);
            continue;
        }
        // Only fire routine reminders within a 2-hour window from their scheduled end,
        // so yesterday's or very old routine blocks don't fire late.
        const QDateTime endDt = startDt.addSecs(qint64(b.durationMinutes) * 60);
        if (now > endDt.addSecs(1800)) {
            continue;
        }
        const QString key = QStringLiteral("routine-%1-%2").arg(today.toString(Qt::ISODate)).arg(b.startMinute);
        if (!db->reminderFired(logUid, key)) {
            out.append(RoutineDue{b.id, logUid, key, trigger, today, false});
        }
    }
    if (next) {
        *next = soonest;
    }
    return out;
}

void Notifier::schedule(const QDateTime &next)
{
    const QDateTime now = QDateTime::currentDateTime();
    // Wake up at least every 15 minutes: timers do not advance reliably
    // across suspend, and the clock or time zone may change.
    qint64 ms = 15 * 60 * 1000;
    if (next.isValid()) {
        ms = qBound<qint64>(500, now.msecsTo(next) + 200, ms);
    }
    if (m_settings->dailySummary()) {
        const QTime at = parseTime(m_settings->dailySummaryTime(), QTime(8, 0));
        QDateTime summary(now.date(), at);
        if (summary <= now) {
            summary = summary.addDays(1);
        }
        ms = qMin(ms, qMax<qint64>(500, now.msecsTo(summary) + 200));
    }
    m_timer.start(int(ms));
}

void Notifier::check()
{
    const QDateTime now = QDateTime::currentDateTime();
    QDateTime nextTask, nextRoutine;
    const QList<Due> due = pending(now, &nextTask);
    const QList<RoutineDue> dueRoutines = pendingRoutines(now, &nextRoutine);
    Database *db = m_store->database();

    // One notification per task, even if several of its reminders are due.
    QHash<qint64, Due> perTask;
    for (const Due &d : due) {
        if (d.snoozed) {
            db->clearSnooze(d.uid);
        } else {
            db->setReminderFired(d.uid, d.key);
        }
        if (!perTask.contains(d.taskId) || d.when > perTask.value(d.taskId).when) {
            perTask.insert(d.taskId, d);
        }
    }
    QList<const Task *> tasks;
    for (auto it = perTask.cbegin(); it != perTask.cend(); ++it) {
        if (const Task *t = m_store->task(it.key())) {
            tasks.append(t);
        }
    }
    // After being closed for a while, do not flood the screen.
    if (m_firstCheck && tasks.size() > 3) {
        showMissed(tasks);
    } else {
        for (const Task *t : std::as_const(tasks)) {
            showReminder(*t, perTask.value(t->id).snoozed);
        }
    }

    for (const RoutineDue &rd : dueRoutines) {
        if (rd.snoozed) {
            db->clearSnooze(rd.uid);
        } else {
            db->setReminderFired(rd.uid, rd.key);
        }
        if (const auto b = db->routineBlock(rd.blockId)) {
            showRoutineReminder(*b, rd.date);
        }
    }

    QDateTime next = nextTask;
    if (nextRoutine.isValid() && (!next.isValid() || nextRoutine < next)) {
        next = nextRoutine;
    }

    m_firstCheck = false;
    maybeShowSummary(now);
    schedule(next);
}

void Notifier::showRoutineReminder(const RoutineBlock &block, const QDate &date)
{
    const QString logUid = QStringLiteral("routine:%1").arg(block.uid);
    if (auto old = m_openRoutines.take(logUid)) {
        old->close();
    }
    const qint64 id = block.id;
    const QString range = QStringLiteral("%1 – %2").arg(RoutineManager::formatMinutes(block.startMinute),
                                                        RoutineManager::formatMinutes(qMin(1440, block.startMinute + block.durationMinutes)));
    auto *n = new KNotification(QStringLiteral("routine"), KNotification::Persistent, this);
    n->setTitle(i18nc("routine notification title", "Routine: %1", block.title));
    n->setText(block.category.isEmpty() ? range : QStringLiteral("%1 · %2").arg(range, block.category));
    n->setIconName(QStringLiteral("io.github.marleys439.ktasks"));

    auto *open = n->addDefaultAction(i18nc("notification action", "Open Routine"));
    connect(open, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("routine"), n->xdgActivationToken());
    });
    auto *done = n->addAction(i18nc("notification action", "Complete"));
    connect(done, &KNotificationAction::activated, this, [this, id, date] {
        if (m_routine) {
            m_routine->setBlockStatusForDate(id, date, int(RoutineLogStatus::Done));
        }
    });
    const int minutes = qMax(1, m_settings->snoozeMinutes());
    auto *later = n->addAction(i18ncp("notification action", "Snooze %1 minute", "Snooze %1 minutes", minutes));
    connect(later, &KNotificationAction::activated, this, [this, logUid, minutes] {
        snooze(logUid, minutes);
    });
    auto *skip = n->addAction(i18nc("notification action", "Skip today"));
    connect(skip, &KNotificationAction::activated, this, [this, id, date] {
        if (m_routine) {
            m_routine->setBlockStatusForDate(id, date, int(RoutineLogStatus::Skipped));
        }
    });
    m_openRoutines.insert(logUid, n);
    n->sendEvent();
    qCInfo(lcNotify) << "routine reminder shown for block" << id;
}

static QString describeTask(const Task &t, const TaskStore *store)
{
    QStringList parts;
    const QDateTime now = QDateTime::currentDateTime();
    if (t.due.isValid()) {
        const bool overdue = t.allDay ? t.due.date() < now.date() : t.due < now.addSecs(-60);
        const QString when = TaskModel::formatDue(t, now);
        parts << (overdue ? i18nc("notification text, %1 is a date", "Was due %1", when) : i18nc("notification text, %1 is a date", "Due %1", when));
    }
    if (const TaskList *l = store->list(t.listId)) {
        parts << l->name;
    }
    return parts.join(QStringLiteral(" · "));
}

void Notifier::showReminder(const Task &task, bool snoozed)
{
    Q_UNUSED(snoozed)
    if (auto old = m_open.take(task.uid)) {
        old->close();
    }
    const qint64 id = task.id;
    const QString uid = task.uid;
    auto *n = new KNotification(QStringLiteral("reminder"), KNotification::Persistent, this);
    n->setTitle(task.title);
    n->setText(describeTask(task, m_store));
    n->setIconName(QStringLiteral("io.github.marleys439.ktasks"));
    n->setUrgency(task.priorityLevel() == 3 ? KNotification::HighUrgency : KNotification::NormalUrgency);

    auto *open = n->addDefaultAction(i18nc("notification action", "Open"));
    connect(open, &KNotificationAction::activated, this, [this, n, id] {
        Q_EMIT activateRequested(id, QString(), n->xdgActivationToken());
    });
    auto *done = n->addAction(i18nc("notification action", "Complete"));
    connect(done, &KNotificationAction::activated, this, [this, id] {
        m_store->setCompleted(id, true);
    });
    const int minutes = qMax(1, m_settings->snoozeMinutes());
    auto *later = n->addAction(i18ncp("notification action", "Snooze %1 minute", "Snooze %1 minutes", minutes));
    connect(later, &KNotificationAction::activated, this, [this, uid, minutes] {
        snooze(uid, minutes);
    });
    auto *tomorrow = n->addAction(i18nc("notification action: snooze until tomorrow morning", "Tomorrow"));
    connect(tomorrow, &KNotificationAction::activated, this, [this, uid] {
        const QTime at = parseTime(m_settings->allDayReminderTime(), QTime(9, 0));
        const QDateTime until(QDate::currentDate().addDays(1), at);
        snooze(uid, int(QDateTime::currentDateTime().secsTo(until) / 60) + 1);
    });
    m_open.insert(uid, n);
    n->sendEvent();
    qCInfo(lcNotify) << "reminder shown for task" << id;
}

void Notifier::showMissed(const QList<const Task *> &tasks)
{
    auto *n = new KNotification(QStringLiteral("reminder"), KNotification::Persistent, this);
    n->setTitle(i18np("%1 reminder while you were away", "%1 reminders while you were away", tasks.size()));
    QStringList lines;
    for (int i = 0; i < qMin<int>(4, tasks.size()); ++i) {
        lines << QStringLiteral("• ") + tasks.at(i)->title;
    }
    if (tasks.size() > 4) {
        lines << i18np("…and %1 more", "…and %1 more", tasks.size() - 4);
    }
    n->setText(lines.join(QLatin1Char('\n')));
    n->setIconName(QStringLiteral("io.github.marleys439.ktasks"));
    auto *open = n->addDefaultAction(i18nc("notification action", "Open"));
    connect(open, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("today"), n->xdgActivationToken());
    });
    auto *show = n->addAction(i18nc("notification action", "Show tasks"));
    connect(show, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("today"), n->xdgActivationToken());
    });
    n->sendEvent();
}

void Notifier::snooze(const QString &uid, int minutes)
{
    m_store->database()->setSnooze(uid, QDateTime::currentDateTimeUtc().addSecs(qint64(minutes) * 60));
    m_timer.start(200);
}

void Notifier::closeObsolete()
{
    for (auto it = m_open.begin(); it != m_open.end();) {
        const Task *match = nullptr;
        for (const Task &t : m_store->tasks()) {
            if (t.uid == it.key()) {
                match = &t;
                break;
            }
        }
        if (!it.value()) {
            it = m_open.erase(it);
        } else if (!match || match->isDone()) {
            // Completed or deleted elsewhere (another device, the main window).
            it.value()->close();
            it = m_open.erase(it);
        } else {
            ++it;
        }
    }
}

void Notifier::maybeShowSummary(const QDateTime &now)
{
    if (!m_settings->dailySummary()) {
        return;
    }
    const QString today = now.date().toString(Qt::ISODate);
    if (m_settings->lastSummaryDate() == today || now.time() < parseTime(m_settings->dailySummaryTime(), QTime(8, 0))) {
        return;
    }
    m_settings->setLastSummaryDate(today);
    int dueToday = 0, overdue = 0;
    QStringList titles;
    for (const Task &t : m_store->tasks()) {
        if (t.isDone() || !t.due.isValid()) {
            continue;
        }
        const QDate d = t.allDay ? t.due.date() : t.due.toLocalTime().date();
        if (d == now.date()) {
            ++dueToday;
            titles << t.title;
        } else if (d < now.date()) {
            ++overdue;
        }
    }
    if (dueToday == 0 && overdue == 0) {
        return; // nothing to say
    }
    auto *n = new KNotification(QStringLiteral("dailySummary"), KNotification::CloseOnTimeout, this);
    QStringList head;
    if (dueToday > 0) {
        head << i18np("%1 task for today", "%1 tasks for today", dueToday);
    }
    if (overdue > 0) {
        head << i18np("%1 overdue", "%1 overdue", overdue);
    }
    n->setTitle(head.join(QStringLiteral(", ")));
    QStringList lines;
    for (int i = 0; i < qMin<int>(5, titles.size()); ++i) {
        lines << QStringLiteral("• ") + titles.at(i);
    }
    if (titles.size() > 5) {
        lines << i18np("…and %1 more", "…and %1 more", titles.size() - 5);
    }
    n->setText(lines.join(QLatin1Char('\n')));
    n->setIconName(QStringLiteral("io.github.marleys439.ktasks"));
    auto *open = n->addDefaultAction(i18nc("notification action", "Open"));
    connect(open, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("today"), n->xdgActivationToken());
    });
    auto *show = n->addAction(i18nc("notification action", "Show tasks"));
    connect(show, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("today"), n->xdgActivationToken());
    });
    n->sendEvent();
}

void Notifier::notifyConflicts(const QString &accountName, const QStringList &titles)
{
    if (!m_settings->notifySyncProblems()) {
        return;
    }
    auto *n = new KNotification(QStringLiteral("syncConflict"), KNotification::Persistent, this);
    n->setTitle(i18np("%1 task was changed in two places", "%1 tasks were changed in two places", titles.size()));
    QString text = i18nc("%1 is an account name", "Both versions were kept in %1; your version has \"conflicting copy\" in its title:", accountName);
    for (int i = 0; i < qMin<int>(4, titles.size()); ++i) {
        text += QStringLiteral("\n• ") + titles.at(i);
    }
    n->setText(text);
    n->setIconName(QStringLiteral("dialog-warning"));
    auto *open = n->addDefaultAction(i18nc("notification action", "Open"));
    connect(open, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("search:conflict"), n->xdgActivationToken());
    });
    n->sendEvent();
}

void Notifier::notifySyncProblem(const QString &accountName, const QString &message, bool needsLogin)
{
    if (!m_settings->notifySyncProblems()) {
        return;
    }
    auto *n = new KNotification(QStringLiteral("syncError"), KNotification::CloseOnTimeout, this);
    n->setTitle(i18nc("%1 is an account name", "Could not synchronize with %1", accountName));
    n->setText(needsLogin ? i18n("%1 Open the settings to sign in again. Your changes are kept on this device.", message)
                          : i18n("%1 Your changes are kept on this device and will be sent later.", message));
    n->setIconName(QStringLiteral("dialog-error"));
    auto *open = n->addDefaultAction(i18nc("notification action", "Open"));
    connect(open, &KNotificationAction::activated, this, [this, n] {
        Q_EMIT activateRequested(0, QStringLiteral("settings"), n->xdgActivationToken());
    });
    n->sendEvent();
}

void Notifier::sendTestNotification()
{
    auto *n = new KNotification(QStringLiteral("reminder"), KNotification::CloseOnTimeout, this);
    n->setTitle(i18n("This is what a reminder looks like"));
    n->setText(i18n("Reminders show the task, when it is due and buttons to complete or snooze it."));
    n->setIconName(QStringLiteral("io.github.marleys439.ktasks"));
    auto *done = n->addAction(i18nc("notification action", "Complete"));
    connect(done, &KNotificationAction::activated, n, &KNotification::close);
    const int minutes = qMax(1, m_settings->snoozeMinutes());
    auto *later = n->addAction(i18ncp("notification action", "Snooze %1 minute", "Snooze %1 minutes", minutes));
    connect(later, &KNotificationAction::activated, n, &KNotification::close);
    n->sendEvent();
}
