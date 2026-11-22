// SPDX-License-Identifier: GPL-3.0-or-later
#include "icscodec.h"
#include "logging.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/Todo>
#include <QBitArray>
#include <QTimeZone>

using namespace KCalendarCore;

namespace KTasks::IcsCodec
{

static const QByteArray kSortOrderProp = QByteArrayLiteral("X-APPLE-SORT-ORDER");
static const QByteArray kQuadrantProp = QByteArrayLiteral("X-KTASKS-QUADRANT");
static const QByteArray kEstMinProp = QByteArrayLiteral("X-KTASKS-ESTIMATED-MIN");
static const QByteArray kTrackedSecProp = QByteArrayLiteral("X-KTASKS-TRACKED-SEC");
static const QByteArray kPomodorosProp = QByteArrayLiteral("X-KTASKS-POMODOROS");

static void ensureProductId()
{
    static bool done = false;
    if (!done) {
        CalFormat::setApplication(QStringLiteral("KTasks"), QStringLiteral("-//KTasks//KTasks 1.0//EN"));
        done = true;
    }
}

static QDateTime toModel(const QDateTime &dt, bool allDay)
{
    if (!dt.isValid()) {
        return {};
    }
    if (allDay) {
        return QDateTime(dt.date(), QTime(0, 0));
    }
    return dt.toUTC();
}

static QDateTime toIcal(const QDateTime &dt, bool allDay)
{
    if (!dt.isValid()) {
        return {};
    }
    if (allDay) {
        return QDateTime(dt.date(), QTime(0, 0), QTimeZone::LocalTime);
    }
    return dt.toTimeZone(QTimeZone::systemTimeZone());
}

static QString customProp(const Incidence::Ptr &inc, const QByteArray &primary, const QByteArray &legacy)
{
    const QString v = inc->nonKDECustomProperty(primary).trimmed();
    return v.isEmpty() ? inc->nonKDECustomProperty(legacy).trimmed() : v;
}

static Task fromTodo(const Todo::Ptr &todo)
{
    ensureProductId();
    Task t;
    t.uid = todo->uid();
    t.title = todo->summary();
    t.notes = todo->description();
    t.priority = todo->priority();
    const bool hasDue = todo->hasDueDate();
    const bool hasStart = todo->hasStartDate();
    t.allDay = (hasDue || hasStart) ? todo->allDay() : true;
    if (hasDue) {
        t.due = toModel(todo->dtDue(true), t.allDay);
    }
    if (hasStart) {
        t.start = toModel(todo->dtStart(true), t.allDay);
    }
    if (todo->isCompleted()) {
        t.status = TaskStatus::Completed;
        t.completedAt = todo->completed().isValid() ? todo->completed().toUTC() : todo->lastModified().toUTC();
        t.percent = 100;
    } else {
        t.percent = todo->percentComplete();
        if (todo->status() == Incidence::StatusCanceled) {
            t.status = TaskStatus::Cancelled;
        } else if (todo->status() == Incidence::StatusInProcess) {
            t.status = TaskStatus::InProcess;
        }
    }
    t.tags = todo->categories();
    t.parentUid = todo->relatedTo();
    const auto rules = todo->recurrence()->rRules();
    if (!rules.isEmpty()) {
        ICalFormat fmt;
        t.rrule = fmt.toString(rules.first()).trimmed();
        if (t.rrule.startsWith(QLatin1String("RRULE:"))) {
            t.rrule.remove(0, 6);
        }
    }
    const auto alarms = todo->alarms();
    for (const auto &a : alarms) {
        if (!a->enabled()) {
            continue;
        }
        KTasks::Alarm m;
        if (a->hasTime()) {
            m.kind = KTasks::Alarm::Absolute;
            m.at = a->time().toUTC();
        } else if (a->hasEndOffset()) {
            m.kind = KTasks::Alarm::RelativeToDue;
            m.offsetSecs = a->endOffset().asSeconds();
        } else {
            m.kind = KTasks::Alarm::RelativeToStart;
            m.offsetSecs = a->startOffset().asSeconds();
        }
        if (!t.alarms.contains(m)) {
            t.alarms.append(m);
        }
    }
    t.created = todo->created().toUTC();
    t.modified = todo->lastModified().toUTC();
    bool ok = false;
    const double order = todo->nonKDECustomProperty(kSortOrderProp).toDouble(&ok);
    t.sortOrder = ok ? order : defaultSortOrder(t.created);
    bool qOk = false;
    const int quad = customProp(todo, kQuadrantProp, QByteArrayLiteral("X-AFAZER-QUADRANT")).toInt(&qOk);
    t.quadrant = (qOk && quad >= 1 && quad <= 4) ? quad : 0;
    bool eOk = false;
    const int estMin = customProp(todo, kEstMinProp, QByteArrayLiteral("X-AFAZER-ESTIMATED-MIN")).toInt(&eOk);
    t.estimatedMinutes = (eOk && estMin > 0) ? estMin : 0;
    bool tOk = false;
    const int trSec = customProp(todo, kTrackedSecProp, QByteArrayLiteral("X-AFAZER-TRACKED-SEC")).toInt(&tOk);
    t.trackedSeconds = (tOk && trSec > 0) ? trSec : 0;
    bool pOk = false;
    const int poms = customProp(todo, kPomodorosProp, QByteArrayLiteral("X-AFAZER-POMODOROS")).toInt(&pOk);
    t.pomodorosCompleted = (pOk && poms > 0) ? poms : 0;
    return t;
}

static QList<Todo::Ptr> parseTodos(const QString &ics)
{
    ensureProductId();
    if (!ics.contains(QLatin1String("BEGIN:VCALENDAR"))) {
        return {};
    }
    MemoryCalendar::Ptr cal(new MemoryCalendar(QTimeZone::utc()));
    ICalFormat fmt;
    if (!fmt.fromString(cal, ics)) {
        return {};
    }
    return cal->rawTodos();
}

static Todo::Ptr masterTodo(const QString &ics)
{
    const auto todos = parseTodos(ics);
    for (const auto &todo : todos) {
        if (!todo->hasRecurrenceId()) {
            return todo;
        }
    }
    return todos.isEmpty() ? Todo::Ptr() : todos.first();
}

std::optional<Task> decode(const QString &ics)
{
    const Todo::Ptr todo = masterTodo(ics);
    if (!todo) {
        return std::nullopt;
    }
    return fromTodo(todo);
}

QList<Task> decodeAll(const QString &ics)
{
    QList<Task> out;
    const auto todos = parseTodos(ics);
    for (const auto &todo : todos) {
        if (!todo->hasRecurrenceId()) {
            out.append(fromTodo(todo));
        }
    }
    return out;
}

// Writes the fields of @p t that differ from @p base onto @p todo.
static void applyToTodo(const Todo::Ptr &todo, const Task &t, const Task &base, bool fresh)
{
    if (fresh || t.title != base.title) {
        todo->setSummary(t.title);
    }
    if (fresh || t.notes != base.notes) {
        todo->setDescription(t.notes);
    }
    if (fresh || t.priority != base.priority) {
        todo->setPriority(t.priority);
    }
    if (fresh || t.tags != base.tags) {
        todo->setCategories(t.tags);
    }
    if (fresh || t.parentUid != base.parentUid) {
        todo->setRelatedTo(t.parentUid);
    }
    const bool datesChanged = fresh || t.allDay != base.allDay || t.start != base.start || t.due != base.due;
    const bool rruleChanged = fresh || t.rrule != base.rrule;
    if (datesChanged || rruleChanged) {
        todo->recurrence()->clear();
    }
    if (datesChanged) {
        todo->setAllDay(t.allDay);
        // A recurring to-do needs DTSTART as the anchor of its rule.
        const QDateTime start = t.start.isValid() || t.rrule.isEmpty() ? t.start : t.due;
        todo->setDtStart(toIcal(start, t.allDay));
        todo->setDtDue(toIcal(t.due, t.allDay), true);
        todo->setAllDay(t.allDay);
    }
    if ((datesChanged || rruleChanged) && !t.rrule.isEmpty()) {
        ICalFormat fmt;
        auto *rule = new RecurrenceRule;
        if (fmt.fromString(rule, t.rrule)) {
            rule->setStartDt(todo->dtStart(true));
            rule->setAllDay(t.allDay);
            todo->recurrence()->addRRule(rule);
        } else {
            delete rule;
            qCWarning(lcSync) << "ignoring unparsable recurrence rule";
        }
    }
    if (fresh || t.status != base.status || t.completedAt != base.completedAt || t.percent != base.percent) {
        if (t.status == TaskStatus::Completed) {
            if (todo->recurs()) {
                todo->recurrence()->clear();
            }
            todo->setCompleted(t.completedAt.isValid() ? t.completedAt : QDateTime::currentDateTimeUtc());
        } else {
            todo->setCompleted(false);
            todo->setPercentComplete(qBound(0, t.percent, 99));
            switch (t.status) {
            case TaskStatus::InProcess:
                todo->setStatus(Incidence::StatusInProcess);
                break;
            case TaskStatus::Cancelled:
                todo->setStatus(Incidence::StatusCanceled);
                break;
            default:
                todo->setStatus(Incidence::StatusNeedsAction);
                break;
            }
        }
    }
    if (fresh || t.alarms != base.alarms) {
        todo->clearAlarms();
        for (const KTasks::Alarm &m : t.alarms) {
            auto a = todo->newAlarm();
            a->setDisplayAlarm(t.title);
            a->setEnabled(true);
            switch (m.kind) {
            case KTasks::Alarm::Absolute:
                a->setTime(m.at);
                break;
            case KTasks::Alarm::RelativeToDue:
                if (t.due.isValid()) {
                    a->setEndOffset(Duration(m.offsetSecs));
                } else {
                    a->setStartOffset(Duration(m.offsetSecs));
                }
                break;
            case KTasks::Alarm::RelativeToStart:
                a->setStartOffset(Duration(m.offsetSecs));
                break;
            }
        }
    }
    if (fresh || qint64(t.sortOrder) != qint64(base.sortOrder)) {
        todo->setNonKDECustomProperty(kSortOrderProp, QString::number(qint64(t.sortOrder)));
    }
    if (fresh || t.quadrant != base.quadrant) {
        if (t.quadrant >= 1 && t.quadrant <= 4) {
            todo->setNonKDECustomProperty(kQuadrantProp, QString::number(t.quadrant));
        } else {
            todo->removeNonKDECustomProperty(kQuadrantProp);
        }
    }
    auto syncIntProp = [&](const QByteArray &prop, int val, int baseVal) {
        if (fresh || val != baseVal) {
            if (val > 0) {
                todo->setNonKDECustomProperty(prop, QString::number(val));
            } else {
                todo->removeNonKDECustomProperty(prop);
            }
        }
    };
    syncIntProp(kEstMinProp, t.estimatedMinutes, base.estimatedMinutes);
    syncIntProp(kTrackedSecProp, t.trackedSeconds, base.trackedSeconds);
    syncIntProp(kPomodorosProp, t.pomodorosCompleted, base.pomodorosCompleted);
    todo->setLastModified(t.modified.isValid() ? t.modified : QDateTime::currentDateTimeUtc());
}

static Todo::Ptr toTodo(const Task &task)
{
    Todo::Ptr todo;
    Task base;
    bool fresh = true;
    if (!task.baseIcs.isEmpty()) {
        todo = masterTodo(task.baseIcs);
        if (todo) {
            base = fromTodo(todo);
            fresh = false;
        }
    }
    if (!todo) {
        todo = Todo::Ptr(new Todo);
        todo->setCreated(task.created.isValid() ? task.created : QDateTime::currentDateTimeUtc());
    }
    todo->setUid(task.uid);
    applyToTodo(todo, task, base, fresh);
    return todo;
}

QString encode(const Task &task)
{
    ensureProductId();
    ICalFormat fmt;
    return fmt.toICalString(toTodo(task));
}

QString exportCalendar(const QList<Task> &tasks)
{
    ensureProductId();
    MemoryCalendar::Ptr cal(new MemoryCalendar(QTimeZone::systemTimeZone()));
    for (const Task &t : tasks) {
        cal->addTodo(toTodo(t));
    }
    ICalFormat fmt;
    return fmt.toString(cal.staticCast<Calendar>());
}

// ---- VEVENT (RoutineBlock) ----

static const QByteArray kRoutineColorProp = QByteArrayLiteral("X-KTASKS-COLOR");
static const QByteArray kRoutineActiveProp = QByteArrayLiteral("X-KTASKS-ACTIVE");
static const QByteArray kRoutineNotifyProp = QByteArrayLiteral("X-KTASKS-NOTIFY");
static const QByteArray kRoutineNotifyMinProp = QByteArrayLiteral("X-KTASKS-NOTIFY-MIN");
static const QByteArray kRoutineLogsProp = QByteArrayLiteral("X-KTASKS-LOGS");
static const QByteArray kRoutineStartDateProp = QByteArrayLiteral("X-KTASKS-START-DATE");

static Event::Ptr masterEvent(const QString &ics)
{
    ensureProductId();
    if (!ics.contains(QLatin1String("BEGIN:VCALENDAR"))) {
        return {};
    }
    MemoryCalendar::Ptr cal(new MemoryCalendar(QTimeZone::utc()));
    ICalFormat fmt;
    if (!fmt.fromString(cal, ics)) {
        return {};
    }
    const auto events = cal->rawEvents();
    for (const auto &ev : events) {
        if (!ev->hasRecurrenceId()) {
            return ev;
        }
    }
    return events.isEmpty() ? Event::Ptr() : events.first();
}

std::optional<DecodedRoutine> decodeEvent(const QString &ics)
{
    const Event::Ptr ev = masterEvent(ics);
    if (!ev) {
        return std::nullopt;
    }
    DecodedRoutine out;
    RoutineBlock &b = out.block;
    b.uid = ev->uid();
    b.title = ev->summary().trimmed();
    b.notes = ev->description();
    if (!ev->categories().isEmpty()) {
        b.category = ev->categories().first().trimmed();
    }
    const QTimeZone tz = QTimeZone::systemTimeZone();
    const QDateTime startLocal = ev->dtStart().toTimeZone(tz);
    const QDateTime endLocal = ev->dtEnd().isValid() ? ev->dtEnd().toTimeZone(tz) : startLocal.addSecs(3600);
    if (ev->allDay() || !startLocal.isValid()) {
        b.startMinute = 9 * 60;
        b.durationMinutes = 60;
    } else {
        b.startMinute = qBound(0, startLocal.time().hour() * 60 + startLocal.time().minute(), 1439);
        const qint64 mins = startLocal.secsTo(endLocal) / 60;
        b.durationMinutes = qBound(5, int(mins > 0 ? mins : 60), 1440);
    }

    const auto rrules = ev->recurrence()->rRules();
    if (!rrules.isEmpty()) {
        b.specificDate = QDate();
        b.daysMask = 0;
        if (rrules.first()->recurrenceType() == RecurrenceRule::rDaily) {
            b.daysMask = 127;
        } else {
            const QBitArray days = ev->recurrence()->days();
            for (int i = 0; i < 7 && i < days.size(); ++i) {
                if (days.testBit(i)) {
                    b.daysMask |= (1 << i);
                }
            }
        }
        if (b.daysMask == 0 && startLocal.isValid()) {
            b.daysMask = 1 << (startLocal.date().dayOfWeek() - 1);
        }
        const QString sdProp = ev->nonKDECustomProperty(kRoutineStartDateProp).trimmed();
        if (!sdProp.isEmpty()) {
            b.startDate = QDate::fromString(sdProp.trimmed(), Qt::ISODate);
        }
    } else if (startLocal.isValid()) {
        b.specificDate = startLocal.date();
        b.daysMask = 0;
        b.startDate = QDate();
    }

    const QString customColor = customProp(ev, kRoutineColorProp, QByteArrayLiteral("X-AFAZER-COLOR"));
    const QString calColor = !customColor.isEmpty() ? customColor : ev->color().trimmed();
    if (!calColor.isEmpty()) {
        b.color = calColor;
    }
    const QString actStr = customProp(ev, kRoutineActiveProp, QByteArrayLiteral("X-AFAZER-ACTIVE"));
    if (!actStr.isEmpty()) {
        b.active = actStr != QLatin1String("0");
    } else {
        b.active = ev->status() != Incidence::StatusCanceled;
    }

    bool notifyParsed = false;
    const QString nEn = customProp(ev, kRoutineNotifyProp, QByteArrayLiteral("X-AFAZER-NOTIFY"));
    if (!nEn.isEmpty()) {
        b.notifyEnabled = nEn != QLatin1String("0");
        notifyParsed = true;
    }
    const QString nMin = customProp(ev, kRoutineNotifyMinProp, QByteArrayLiteral("X-AFAZER-NOTIFY-MIN"));
    bool okMin = false;
    const int customMins = nMin.toInt(&okMin);
    if (okMin) {
        b.notifyMinutesBefore = qBound(0, customMins, 120);
    }
    for (const auto &a : ev->alarms()) {
        if (!a->enabled()) {
            continue;
        }
        if (!notifyParsed) {
            b.notifyEnabled = true;
            notifyParsed = true;
        }
        if (!okMin && a->hasStartOffset()) {
            b.notifyMinutesBefore = qBound(0, int(-a->startOffset().asSeconds() / 60), 120);
        }
        break;
    }
    if (!notifyParsed) {
        b.notifyEnabled = false;
    }

    b.created = ev->created().toUTC();
    b.modified = ev->lastModified().toUTC();

    const QString logsRaw = customProp(ev, kRoutineLogsProp, QByteArrayLiteral("X-AFAZER-LOGS"));
    if (!logsRaw.isEmpty()) {
        for (const QString &tok : logsRaw.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QStringList kv = tok.split(QLatin1Char(':'));
            if (kv.size() == 2) {
                const QDate d = QDate::fromString(kv.at(0), Qt::ISODate);
                const int st = kv.at(1).toInt();
                if (d.isValid() && (st == 1 || st == 2)) {
                    RoutineLog rl;
                    rl.logDate = d;
                    rl.status = RoutineLogStatus(st);
                    out.logs.append(rl);
                }
            }
        }
    }
    return out;
}

static QDate routineAnchorDate(const RoutineBlock &b)
{
    if (b.specificDate.isValid()) {
        return b.specificDate;
    }
    QDate d = b.startDate.isValid() ? b.startDate
        : (b.created.isValid() ? b.created.toLocalTime().date() : QDate::currentDate());
    const int mask = b.daysMask > 0 ? b.daysMask : 127;
    for (int i = 0; i < 7; ++i) {
        if (mask & (1 << (d.dayOfWeek() - 1))) {
            return d;
        }
        d = d.addDays(1);
    }
    return d;
}

static QString encodeLogsProp(const QList<RoutineLog> &logs)
{
    QStringList parts;
    for (const RoutineLog &l : logs) {
        if (l.logDate.isValid() && l.status != RoutineLogStatus::Pending) {
            parts << QStringLiteral("%1:%2").arg(l.logDate.toString(Qt::ISODate)).arg(int(l.status));
        }
    }
    std::sort(parts.begin(), parts.end(), std::greater<QString>());
    if (parts.size() > 90) {
        parts = parts.mid(0, 90);
    }
    return parts.join(QLatin1Char(';'));
}

QString encodeEvent(const RoutineBlock &block, const QList<RoutineLog> &logs)
{
    ensureProductId();
    Event::Ptr ev;
    RoutineBlock base;
    QString baseLogs;
    bool fresh = true;
    if (!block.baseIcs.isEmpty()) {
        if (auto dec = decodeEvent(block.baseIcs)) {
            ev = masterEvent(block.baseIcs);
            base = dec->block;
            baseLogs = encodeLogsProp(dec->logs);
            fresh = false;
        }
    }
    if (!ev) {
        ev = Event::Ptr(new Event);
        ev->setCreated(block.created.isValid() ? block.created : QDateTime::currentDateTimeUtc());
    }
    ev->setUid(block.uid);
    if (fresh || block.title != base.title) {
        ev->setSummary(block.title);
    }
    if (fresh || block.notes != base.notes) {
        ev->setDescription(block.notes);
    }
    if (fresh || block.category != base.category) {
        ev->setCategories(block.category.isEmpty() ? QStringList() : QStringList{block.category});
    }
    if (fresh || block.color != base.color) {
        ev->setColor(block.color);
        ev->setNonKDECustomProperty(kRoutineColorProp, block.color);
    }
    if (fresh || block.active != base.active) {
        ev->setNonKDECustomProperty(kRoutineActiveProp, block.active ? QStringLiteral("1") : QStringLiteral("0"));
        ev->setStatus(block.active ? Incidence::StatusConfirmed : Incidence::StatusCanceled);
    }

    const bool scheduleChanged = fresh || block.startMinute != base.startMinute || block.durationMinutes != base.durationMinutes
        || block.daysMask != base.daysMask || block.specificDate != base.specificDate || block.startDate != base.startDate;
    if (scheduleChanged) {
        const QDate anchor = routineAnchorDate(block);
        const QTime stTime(block.startMinute / 60, block.startMinute % 60);
        const QDateTime dtStart(anchor, stTime, QTimeZone::systemTimeZone());
        const QDateTime dtEnd = dtStart.addSecs(qMax(5, block.durationMinutes) * 60);
        ev->setAllDay(false);
        ev->setDtStart(dtStart);
        ev->setDtEnd(dtEnd);
        ev->recurrence()->clear();
        if (!block.specificDate.isValid()) {
            QBitArray days(7);
            const int mask = block.daysMask > 0 ? block.daysMask : 127;
            for (int i = 0; i < 7; ++i) {
                days.setBit(i, (mask & (1 << i)) != 0);
            }
            ev->recurrence()->setWeekly(1, days, 1);
            if (block.startDate.isValid()) {
                ev->setNonKDECustomProperty(kRoutineStartDateProp, block.startDate.toString(Qt::ISODate));
            } else {
                ev->removeNonKDECustomProperty(kRoutineStartDateProp);
            }
        } else {
            ev->removeNonKDECustomProperty(kRoutineStartDateProp);
        }
    }

    if (fresh || block.notifyEnabled != base.notifyEnabled || block.notifyMinutesBefore != base.notifyMinutesBefore) {
        ev->clearAlarms();
        ev->setNonKDECustomProperty(kRoutineNotifyProp, block.notifyEnabled ? QStringLiteral("1") : QStringLiteral("0"));
        ev->setNonKDECustomProperty(kRoutineNotifyMinProp, QString::number(block.notifyMinutesBefore));
        if (block.notifyEnabled) {
            auto a = ev->newAlarm();
            a->setDisplayAlarm(block.title);
            a->setEnabled(true);
            a->setStartOffset(Duration(-block.notifyMinutesBefore * 60));
        }
    }

    const QString curLogs = encodeLogsProp(logs);
    if (fresh || curLogs != baseLogs) {
        if (curLogs.isEmpty()) {
            ev->removeNonKDECustomProperty(kRoutineLogsProp);
        } else {
            ev->setNonKDECustomProperty(kRoutineLogsProp, curLogs);
        }
    }
    ev->setLastModified(block.modified.isValid() ? block.modified : QDateTime::currentDateTimeUtc());

    ICalFormat fmt;
    return fmt.toICalString(ev);
}

}
