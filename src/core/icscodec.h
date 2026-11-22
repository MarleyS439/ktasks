// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "task.h"

#include <optional>

namespace KTasks::IcsCodec
{

/** Decodes the first (master) VTODO of an iCalendar document. */
std::optional<Task> decode(const QString &ics);
/** Decodes every VTODO of an iCalendar document (used for import). */
QList<Task> decodeAll(const QString &ics);
/**
 * Serializes a task. When the task carries a server copy (baseIcs) the
 * result is that copy with only the changed fields replaced, so properties
 * this application does not know about survive the round trip.
 */
QString encode(const Task &task);
/** One VCALENDAR holding all the given tasks (used for export). */
QString exportCalendar(const QList<Task> &tasks);

struct DecodedRoutine {
    RoutineBlock block;
    QList<RoutineLog> logs;
};
/** Decodes the first (master) VEVENT of an iCalendar document into a RoutineBlock. */
std::optional<DecodedRoutine> decodeEvent(const QString &ics);
/** Serializes a RoutineBlock (and optional daily logs) as a standard VEVENT (preserving baseIcs). */
QString encodeEvent(const RoutineBlock &block, const QList<RoutineLog> &logs = {});

}
