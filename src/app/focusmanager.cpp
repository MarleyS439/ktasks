// SPDX-License-Identifier: GPL-3.0-or-later
#include "focusmanager.h"
#include "database.h"
#include "settings.h"
#include "taskstore.h"

#include <KLocalizedString>
#include <KNotification>
#include <QLocale>
#include <QStandardPaths>

using namespace KTasks;

FocusManager::FocusManager(TaskStore *store, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &FocusManager::onTick);
    connect(m_store, &TaskStore::changed, this, [this] {
        if (m_hasSession && m_activeTaskId > 0) {
            refreshCachedTitle(true);
        }
    });
    refreshCachedTitle(false);
    updateTimeText();
}

QString FocusManager::formatDuration(int totalSeconds)
{
    if (totalSeconds <= 0) {
        return i18nc("duration", "0 min");
    }
    if (totalSeconds < 60) {
        return i18ncp("duration seconds", "%1 s", "%1 s", totalSeconds);
    }
    const int totalMin = qRound(totalSeconds / 60.0);
    const int hrs = totalMin / 60;
    const int mins = totalMin % 60;
    if (hrs > 0) {
        return mins > 0 ? i18nc("hours and minutes", "%1h %2m", hrs, mins) : i18nc("hours only", "%1h", hrs);
    }
    return i18nc("minutes short", "%1 min", mins);
}

QString FocusManager::activeTaskTitle() const
{
    return m_cachedTaskTitle;
}

void FocusManager::refreshCachedTitle(bool emitIfChanged)
{
    QString next;
    if (m_activeTaskId <= 0) {
        next = !m_fallbackTitle.isEmpty() ? m_fallbackTitle : i18nc("focus target", "Free Focus");
    } else if (const Task *t = m_store->task(m_activeTaskId)) {
        m_fallbackTitle = t->title;
        next = t->title;
    } else {
        next = m_fallbackTitle;
    }
    if (next != m_cachedTaskTitle) {
        m_cachedTaskTitle = next;
        if (emitIfChanged) {
            Q_EMIT stateChanged();
        }
    }
}

void FocusManager::updateTimeText()
{
    const int secs = (m_mode == FocusMode::Stopwatch) ? m_elapsedSeconds : m_remainingSeconds;
    const int hrs = secs / 3600;
    const int mins = (secs % 3600) / 60;
    const int s = secs % 60;
    if (hrs > 0) {
        m_timeText = QStringLiteral("%1:%2:%3")
                         .arg(hrs)
                         .arg(mins, 2, 10, QLatin1Char('0'))
                         .arg(s, 2, 10, QLatin1Char('0'));
    } else {
        m_timeText = QStringLiteral("%1:%2").arg(mins, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
}

QString FocusManager::timeText() const
{
    return m_timeText;
}

QString FocusManager::statusLabel() const
{
    if (!m_hasSession) {
        return i18nc("focus status", "Ready");
    }
    if (m_mode == FocusMode::Break) {
        return m_running ? i18nc("focus status", "Break") : i18nc("focus status", "Break (Paused)");
    }
    if (m_mode == FocusMode::Stopwatch) {
        return m_running ? i18nc("focus status", "Stopwatch") : i18nc("focus status", "Stopwatch (Paused)");
    }
    return m_running
        ? i18nc("focus status", "Pomodoro %1/4", m_cycleNumber)
        : i18nc("focus status", "Pomodoro %1/4 (Paused)", m_cycleNumber);
}

double FocusManager::progress() const
{
    if (!m_hasSession || m_targetSeconds <= 0) {
        return 0.0;
    }
    if (m_mode == FocusMode::Stopwatch) {
        return 0.0;
    }
    return qBound(0.0, double(m_elapsedSeconds) / double(m_targetSeconds), 1.0);
}

void FocusManager::startPomodoro(qlonglong taskId)
{
    if (m_hasSession && m_elapsedSeconds > 0) {
        commitSegment(false);
    }
    m_mode = FocusMode::Pomodoro;
    m_activeTaskId = taskId > 0 ? taskId : 0;
    const Task *t = m_activeTaskId > 0 ? m_store->task(m_activeTaskId) : nullptr;
    m_fallbackTitle = t ? t->title : QString();
    const int focusMins = qBound(1, m_settings->pomodoroFocusMinutes(), 180);
    m_targetSeconds = focusMins * 60;
    m_elapsedSeconds = 0;
    m_remainingSeconds = m_targetSeconds;
    m_startedAt = QDateTime::currentDateTimeUtc();
    m_hasSession = true;
    m_running = true;
    m_timer.start();
    refreshCachedTitle(false);
    updateTimeText();
    Q_EMIT stateChanged();
    Q_EMIT tickChanged();
}

void FocusManager::startPomodoroLabeled(const QString &label)
{
    if (m_hasSession && m_elapsedSeconds > 0) {
        commitSegment(false);
    }
    m_mode = FocusMode::Pomodoro;
    m_activeTaskId = 0;
    m_fallbackTitle = label.trimmed();
    const int focusMins = qBound(1, m_settings->pomodoroFocusMinutes(), 180);
    m_targetSeconds = focusMins * 60;
    m_elapsedSeconds = 0;
    m_remainingSeconds = m_targetSeconds;
    m_startedAt = QDateTime::currentDateTimeUtc();
    m_hasSession = true;
    m_running = true;
    m_timer.start();
    refreshCachedTitle(false);
    updateTimeText();
    Q_EMIT stateChanged();
    Q_EMIT tickChanged();
}

void FocusManager::startStopwatch(qlonglong taskId)
{
    if (m_hasSession && m_elapsedSeconds > 0) {
        commitSegment(false);
    }
    m_mode = FocusMode::Stopwatch;
    m_activeTaskId = taskId > 0 ? taskId : 0;
    const Task *t = m_activeTaskId > 0 ? m_store->task(m_activeTaskId) : nullptr;
    m_fallbackTitle = t ? t->title : QString();
    m_targetSeconds = 0;
    m_elapsedSeconds = 0;
    m_remainingSeconds = 0;
    m_startedAt = QDateTime::currentDateTimeUtc();
    m_hasSession = true;
    m_running = true;
    m_timer.start();
    refreshCachedTitle(false);
    updateTimeText();
    Q_EMIT stateChanged();
    Q_EMIT tickChanged();
}

void FocusManager::startBreak(bool longBreak)
{
    if (m_hasSession && m_elapsedSeconds > 0) {
        commitSegment(false);
    }
    m_mode = FocusMode::Break;
    const int mins = longBreak
        ? qBound(1, m_settings->pomodoroLongBreakMinutes(), 60)
        : qBound(1, m_settings->pomodoroShortBreakMinutes(), 60);
    m_targetSeconds = mins * 60;
    m_elapsedSeconds = 0;
    m_remainingSeconds = m_targetSeconds;
    m_startedAt = QDateTime::currentDateTimeUtc();
    m_hasSession = true;
    m_running = true;
    m_timer.start();
    refreshCachedTitle(false);
    updateTimeText();
    Q_EMIT stateChanged();
    Q_EMIT tickChanged();
}

void FocusManager::togglePause()
{
    if (!m_hasSession) {
        return;
    }
    m_running = !m_running;
    if (m_running) {
        m_timer.start();
    } else {
        m_timer.stop();
    }
    Q_EMIT stateChanged();
}

void FocusManager::stopAndSave()
{
    if (!m_hasSession) {
        return;
    }
    m_timer.stop();
    m_running = false;
    const int savedSecs = m_elapsedSeconds;
    const bool wasBreak = (m_mode == FocusMode::Break);
    if (savedSecs > 0) {
        commitSegment(false);
    }
    m_hasSession = false;
    m_elapsedSeconds = 0;
    m_remainingSeconds = 0;
    m_targetSeconds = 0;
    if (!wasBreak && savedSecs >= 5) {
        Q_EMIT message(i18nc("@info", "Recorded %1 on “%2”", formatDuration(savedSecs), activeTaskTitle()), false);
    }
    updateTimeText();
    Q_EMIT stateChanged();
    Q_EMIT tickChanged();
}

void FocusManager::discard()
{
    m_timer.stop();
    m_running = false;
    m_hasSession = false;
    m_elapsedSeconds = 0;
    m_remainingSeconds = 0;
    m_targetSeconds = 0;
    updateTimeText();
    Q_EMIT stateChanged();
    Q_EMIT tickChanged();
}

void FocusManager::completeActiveTask()
{
    const qint64 tid = m_activeTaskId;
    stopAndSave();
    if (tid > 0) {
        m_store->setCompleted(tid, true);
    }
}

void FocusManager::addManualMinutes(qlonglong taskId, int minutes)
{
    if (minutes <= 0) {
        return;
    }
    const int secs = qBound(60, minutes * 60, 12 * 3600);
    const Task *t = taskId > 0 ? m_store->task(taskId) : nullptr;
    FocusSession s;
    s.taskId = t ? t->id : 0;
    s.taskUid = t ? t->uid : QString();
    s.taskTitle = t ? t->title : i18nc("focus target", "Manual Focus Log");
    s.listId = t ? t->listId : 0;
    s.quadrant = t ? t->quadrant : 0;
    s.mode = FocusMode::Stopwatch;
    s.durationSeconds = secs;
    s.completedPomodoro = false;
    s.endedAt = QDateTime::currentDateTimeUtc();
    s.startedAt = s.endedAt.addSecs(-secs);
    m_store->database()->insertFocusSession(s);
    if (t) {
        m_store->recordFocus(t->id, secs, 0);
    }
    Q_EMIT sessionsChanged();
    Q_EMIT message(i18nc("@info", "Added %1 to execution time", formatDuration(secs)), false);
}

bool FocusManager::deleteSession(qlonglong sessionId)
{
    const bool ok = m_store->database()->removeFocusSession(sessionId);
    if (ok) {
        Q_EMIT sessionsChanged();
    }
    return ok;
}

QVariantList FocusManager::recentSessions(int limit) const
{
    QVariantList out;
    const QLocale locale;
    const auto sessions = m_store->database()->focusSessions({}, qBound(1, limit, 200));
    for (const FocusSession &s : sessions) {
        const TaskList *l = s.listId > 0 ? m_store->list(s.listId) : nullptr;
        out.append(QVariantMap{
            {QStringLiteral("id"), s.id},
            {QStringLiteral("taskId"), s.taskId},
            {QStringLiteral("taskTitle"), s.taskTitle.isEmpty() ? i18nc("focus target", "Free Focus") : s.taskTitle},
            {QStringLiteral("listName"), l ? l->name : QString()},
            {QStringLiteral("listColor"), l ? l->color : QString()},
            {QStringLiteral("quadrant"), s.quadrant},
            {QStringLiteral("mode"), int(s.mode)},
            {QStringLiteral("durationSeconds"), s.durationSeconds},
            {QStringLiteral("durationText"), formatDuration(s.durationSeconds)},
            {QStringLiteral("completedPomodoro"), s.completedPomodoro},
            {QStringLiteral("endedText"), locale.toString(s.endedAt.toLocalTime(), QLocale::ShortFormat)},
        });
    }
    return out;
}

void FocusManager::advanceSecondsForTesting(int secs)
{
    for (int i = 0; i < secs && m_hasSession && m_running; ++i) {
        onTick();
    }
}

void FocusManager::onTick()
{
    if (!m_hasSession || !m_running) {
        return;
    }
    ++m_elapsedSeconds;
    if (m_mode == FocusMode::Pomodoro || m_mode == FocusMode::Break) {
        m_remainingSeconds = qMax(0, m_targetSeconds - m_elapsedSeconds);
        if (m_remainingSeconds == 0) {
            m_timer.stop();
            m_running = false;
            const FocusMode finished = m_mode;
            const qint64 finishedTaskId = m_activeTaskId;
            const QString title = activeTaskTitle();

            if (finished == FocusMode::Pomodoro) {
                commitSegment(true);
                ++m_completedInStretch;
                m_cycleNumber = (m_cycleNumber % 4) + 1;
            } else {
                commitSegment(false);
            }
            m_hasSession = false;
            m_elapsedSeconds = 0;
            updateTimeText();
            Q_EMIT stateChanged();
            Q_EMIT tickChanged();
            Q_EMIT cycleCompleted(int(finished), finishedTaskId);
            notifyCycleFinished(finished, title, finishedTaskId);
            return;
        }
    }
    updateTimeText();
    Q_EMIT tickChanged();
}

void FocusManager::commitSegment(bool completedPomodoro)
{
    if (m_elapsedSeconds <= 0) {
        return;
    }
    const Task *t = m_activeTaskId > 0 ? m_store->task(m_activeTaskId) : nullptr;
    FocusSession s;
    s.taskId = t ? t->id : m_activeTaskId;
    s.taskUid = t ? t->uid : QString();
    s.taskTitle = activeTaskTitle();
    s.listId = t ? t->listId : 0;
    s.quadrant = t ? t->quadrant : 0;
    s.mode = m_mode;
    s.durationSeconds = m_elapsedSeconds;
    s.completedPomodoro = completedPomodoro;
    s.startedAt = m_startedAt.isValid() ? m_startedAt : QDateTime::currentDateTimeUtc().addSecs(-m_elapsedSeconds);
    s.endedAt = QDateTime::currentDateTimeUtc();
    m_store->database()->insertFocusSession(s);

    if (m_mode != FocusMode::Break && t) {
        m_store->recordFocus(t->id, m_elapsedSeconds, completedPomodoro ? 1 : 0);
    }
    Q_EMIT sessionsChanged();
}

void FocusManager::notifyCycleFinished(FocusMode finishedMode, const QString &taskTitle, qint64 taskId)
{
    const bool isPomo = (finishedMode == FocusMode::Pomodoro);
    const QString msgText = isPomo
        ? i18nc("@info", "Pomodoro finished for “%1”! Take a break.", taskTitle)
        : i18nc("@info", "Break finished! Ready for the next focus cycle.");
    Q_EMIT message(msgText, false);

    if (!m_settings->notifyFocus() || QStandardPaths::isTestModeEnabled()) {
        return;
    }
    auto *n = new KNotification(QStringLiteral("focus"), KNotification::CloseOnTimeout, this);
    n->setTitle(isPomo ? i18nc("notification title", "Pomodoro Completed") : i18nc("notification title", "Break Finished"));
    n->setText(msgText);
    n->setIconName(QStringLiteral("io.github.marleys439.ktasks"));
    if (isPomo) {
        const bool longBrk = (m_completedInStretch > 0 && (m_completedInStretch % 4) == 0);
        auto *brk = n->addAction(longBrk ? i18nc("notification action", "Start Long Break") : i18nc("notification action", "Start Break"));
        connect(brk, &KNotificationAction::activated, this, [this, longBrk] { startBreak(longBrk); });
    } else if (taskId > 0) {
        auto *cont = n->addAction(i18nc("notification action", "Resume Focus"));
        connect(cont, &KNotificationAction::activated, this, [this, taskId] { startPomodoro(taskId); });
    }
    n->sendEvent();
}
