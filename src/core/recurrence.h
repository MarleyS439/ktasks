// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "task.h"

namespace KTasks::Recurrence
{

/** First occurrence strictly after @p after; invalid when the rule is exhausted or unparsable. */
QDateTime nextOccurrence(const QString &rrule, const QDateTime &base, bool allDay, const QDateTime &after);

/**
 * Moves a recurring task to its next occurrence (start, due, absolute
 * reminders, remaining COUNT) and reopens it. Returns false when there is
 * no further occurrence; the task is then left untouched.
 */
bool advance(Task &task, const QDateTime &now);

/** The pieces of a rule the editor understands. */
struct Simple {
    bool valid = false; // false: empty or too complex to edit as a preset
    QString freq; // DAILY, WEEKLY, MONTHLY, YEARLY
    int interval = 1;
    bool weekdays = false; // WEEKLY;BYDAY=MO..FR
};
Simple parseSimple(const QString &rrule);
QString buildSimple(const QString &freq, int interval, bool weekdays);

/** Full editable recurrence rule (supports days of week, count, and until date). */
struct RuleParts {
    bool valid = false; // false only for exotic rules (e.g. BYSETPOS, BYMONTHDAY, etc.)
    QString freq; // "", DAILY, WEEKLY, MONTHLY, YEARLY
    int interval = 1;
    QStringList byDays; // MO, TU, WE, TH, FR, SA, SU
    QString endMode = QStringLiteral("never"); // "never", "count", "until"
    int count = 5;
    QDate until;
};
RuleParts parseRule(const QString &rrule);
QString buildRule(const RuleParts &parts);

}
