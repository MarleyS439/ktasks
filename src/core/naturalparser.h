// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QString>
#include <QStringList>
#include <QTime>

namespace KTasks
{

/** What the quick-entry field understood from a line of text. */
struct ParsedInput {
    QString title;
    QDate date;
    QTime time;
    int priorityLevel = 0; // 0 none, 1 low, 2 medium, 3 high
    QStringList tags;
    QString listName; // from "@name"; resolved by the caller
    QString rrule;

    bool hasDate() const
    {
        return date.isValid();
    }
    bool hasTime() const
    {
        return time.isValid();
    }
};

/**
 * Rule-based parser for Portuguese and English quick entry, e.g.
 * "Pagar boleto amanhã 14h !alta #financas" or
 * "Call mom every friday at 6pm #family".
 *
 * @param now        reference point for relative dates
 * @param monthFirst read 3/4 as March 4th instead of April 3rd
 */
ParsedInput parseNaturalInput(const QString &text, const QDateTime &now, bool monthFirst = false);

}
