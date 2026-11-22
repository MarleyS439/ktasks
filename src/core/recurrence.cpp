// SPDX-License-Identifier: GPL-3.0-or-later
#include "recurrence.h"

#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/Recurrence>
#include <KCalendarCore/RecurrenceRule>
#include <QTimeZone>

namespace KTasks::Recurrence
{

// Timed recurrences are evaluated in local time so that "every day at 9:00"
// stays at 9:00 across daylight saving changes. All-day ones are evaluated
// as plain dates (UTC midnight) so a date-only UNTIL includes its last day.
static QDateTime toLocalBase(const QDateTime &dt, bool allDay)
{
    if (allDay) {
        return QDateTime(dt.date(), QTime(0, 0), QTimeZone::UTC);
    }
    return dt.toTimeZone(QTimeZone::systemTimeZone());
}

static bool setup(KCalendarCore::Recurrence &rec, const QString &rrule, const QDateTime &base, bool allDay)
{
    if (rrule.isEmpty() || !base.isValid()) {
        return false;
    }
    KCalendarCore::ICalFormat fmt;
    auto *rule = new KCalendarCore::RecurrenceRule;
    if (!fmt.fromString(rule, rrule)) {
        delete rule;
        return false;
    }
    const QDateTime start = toLocalBase(base, allDay);
    rule->setStartDt(start);
    rec.addRRule(rule);
    rec.setStartDateTime(start, allDay);
    return true;
}

QDateTime nextOccurrence(const QString &rrule, const QDateTime &base, bool allDay, const QDateTime &after)
{
    KCalendarCore::Recurrence rec;
    if (!setup(rec, rrule, base, allDay)) {
        return {};
    }
    return rec.getNextDateTime(toLocalBase(after, allDay));
}

bool advance(Task &task, const QDateTime &now)
{
    const QDateTime base = task.start.isValid() ? task.start : task.due;
    KCalendarCore::Recurrence rec;
    if (!setup(rec, task.rrule, base, task.allDay)) {
        return false;
    }
    const QDateTime localBase = toLocalBase(base, task.allDay);
    QDateTime next = rec.getNextDateTime(localBase);
    if (!next.isValid()) {
        return false;
    }
    // Skip occurrences that are already in the past: completing a task that
    // is weeks overdue should not leave another overdue copy behind.
    const QDateTime floor = task.allDay ? QDateTime(now.toLocalTime().date(), QTime(0, 0), QTimeZone::UTC) : now;
    if (next < floor) {
        const QDateTime later = rec.getNextDateTime(floor.addSecs(-1));
        if (later.isValid()) {
            next = later;
        }
    }

    KCalendarCore::RecurrenceRule *rule = rec.rRules().first();
    QString newRule = task.rrule;
    if (rule->duration() > 0) {
        const int remaining = rule->duration() - (rule->durationTo(next) - 1);
        if (remaining < 1) {
            return false;
        }
        rule->setDuration(remaining);
        KCalendarCore::ICalFormat fmt;
        newRule = fmt.toString(rule).trimmed();
        if (newRule.startsWith(QLatin1String("RRULE:"))) {
            newRule.remove(0, 6);
        }
    }

    auto shift = [&](const QDateTime &dt) -> QDateTime {
        if (!dt.isValid()) {
            return dt;
        }
        if (task.allDay) {
            return QDateTime(dt.date().addDays(localBase.date().daysTo(next.date())), QTime(0, 0));
        }
        // Shift in local time to keep the wall-clock time across DST changes.
        const QDateTime local = dt.toTimeZone(QTimeZone::systemTimeZone());
        const qint64 days = localBase.date().daysTo(next.date());
        const qint64 secs = localBase.time().secsTo(next.time());
        QDateTime moved(local.date().addDays(days), local.time(), QTimeZone::systemTimeZone());
        return moved.addSecs(secs).toUTC();
    };

    const qint64 absShift = localBase.secsTo(next);
    for (Alarm &a : task.alarms) {
        if (a.kind == Alarm::Absolute && a.at.isValid()) {
            a.at = a.at.addSecs(absShift);
        }
    }
    task.start = shift(task.start);
    task.due = shift(task.due);
    task.rrule = newRule;
    task.status = TaskStatus::NeedsAction;
    task.percent = 0;
    task.completedAt = QDateTime();
    return true;
}

Simple parseSimple(const QString &rrule)
{
    Simple s;
    if (rrule.isEmpty()) {
        return s;
    }
    const QStringList parts = rrule.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    bool complex = false;
    for (const QString &part : parts) {
        const QString key = part.section(QLatin1Char('='), 0, 0).toUpper();
        const QString value = part.section(QLatin1Char('='), 1).toUpper();
        if (key == QLatin1String("FREQ")) {
            s.freq = value;
        } else if (key == QLatin1String("INTERVAL")) {
            s.interval = qMax(1, value.toInt());
        } else if (key == QLatin1String("BYDAY") && value == QLatin1String("MO,TU,WE,TH,FR")) {
            s.weekdays = true;
        } else if (key == QLatin1String("WKST")) {
            // irrelevant for the presets
        } else {
            complex = true;
        }
    }
    static const QStringList known{QStringLiteral("DAILY"), QStringLiteral("WEEKLY"), QStringLiteral("MONTHLY"), QStringLiteral("YEARLY")};
    s.valid = !complex && known.contains(s.freq) && (!s.weekdays || (s.freq == QLatin1String("WEEKLY") && s.interval == 1));
    return s;
}

QString buildSimple(const QString &freq, int interval, bool weekdays)
{
    if (weekdays) {
        return QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
    }
    if (freq.isEmpty()) {
        return {};
    }
    QString r = QStringLiteral("FREQ=") + freq.toUpper();
    if (interval > 1) {
        r += QStringLiteral(";INTERVAL=%1").arg(interval);
    }
    return r;
}

static const QStringList &orderedWeekdays()
{
    static const QStringList kDays{QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"), QStringLiteral("TH"),
                                   QStringLiteral("FR"), QStringLiteral("SA"), QStringLiteral("SU")};
    return kDays;
}

RuleParts parseRule(const QString &rrule)
{
    RuleParts p;
    if (rrule.trimmed().isEmpty()) {
        p.valid = true;
        return p;
    }
    const QStringList parts = rrule.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    bool complex = false;
    for (const QString &part : parts) {
        const QString key = part.section(QLatin1Char('='), 0, 0).toUpper();
        const QString value = part.section(QLatin1Char('='), 1).toUpper();
        if (key == QLatin1String("FREQ")) {
            p.freq = value;
        } else if (key == QLatin1String("INTERVAL")) {
            p.interval = qMax(1, value.toInt());
        } else if (key == QLatin1String("BYDAY")) {
            const QStringList tokens = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (const QString &tok : tokens) {
                if (!orderedWeekdays().contains(tok)) {
                    complex = true;
                    break;
                }
                if (!p.byDays.contains(tok)) {
                    p.byDays.append(tok);
                }
            }
        } else if (key == QLatin1String("COUNT")) {
            bool ok = false;
            const int c = value.toInt(&ok);
            if (ok && c > 0) {
                p.endMode = QStringLiteral("count");
                p.count = c;
            } else {
                complex = true;
            }
        } else if (key == QLatin1String("UNTIL")) {
            const QDate d = QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
            if (d.isValid()) {
                p.endMode = QStringLiteral("until");
                p.until = d;
            } else {
                complex = true;
            }
        } else if (key == QLatin1String("WKST")) {
            // ignored
        } else {
            complex = true;
        }
    }
    static const QStringList known{QStringLiteral("DAILY"), QStringLiteral("WEEKLY"), QStringLiteral("MONTHLY"), QStringLiteral("YEARLY")};
    if (!p.byDays.isEmpty() && p.freq != QLatin1String("WEEKLY")) {
        complex = true;
    }
    p.valid = !complex && known.contains(p.freq);
    return p;
}

QString buildRule(const RuleParts &parts)
{
    const QString freq = parts.freq.trimmed().toUpper();
    if (freq.isEmpty()) {
        return {};
    }
    QString r = QStringLiteral("FREQ=") + freq;
    if (parts.interval > 1) {
        r += QStringLiteral(";INTERVAL=%1").arg(parts.interval);
    }
    if (freq == QLatin1String("WEEKLY") && !parts.byDays.isEmpty()) {
        QStringList sorted;
        for (const QString &d : orderedWeekdays()) {
            if (parts.byDays.contains(d, Qt::CaseInsensitive)) {
                sorted.append(d);
            }
        }
        if (!sorted.isEmpty()) {
            r += QStringLiteral(";BYDAY=") + sorted.join(QLatin1Char(','));
        }
    }
    if (parts.endMode == QLatin1String("count") && parts.count > 0) {
        r += QStringLiteral(";COUNT=%1").arg(parts.count);
    } else if (parts.endMode == QLatin1String("until") && parts.until.isValid()) {
        r += QStringLiteral(";UNTIL=%1").arg(parts.until.toString(QStringLiteral("yyyyMMdd")));
    }
    return r;
}

}
