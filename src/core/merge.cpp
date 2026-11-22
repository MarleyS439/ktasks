// SPDX-License-Identifier: GPL-3.0-or-later
#include "merge.h"

namespace KTasks
{

namespace
{
struct Completion {
    TaskStatus status;
    int percent;
    QDateTime completedAt;
    bool operator==(const Completion &o) const
    {
        // Completion timestamps differ by nature when two devices tick the same box.
        return status == o.status && percent == o.percent;
    }
};
struct Dates {
    QDateTime start, due;
    bool allDay;
    bool operator==(const Dates &o) const
    {
        return start == o.start && due == o.due && (allDay == o.allDay || (!start.isValid() && !due.isValid()));
    }
};

template<typename T>
void mergeField(const T &base, const T &local, const T &remote, T &out, const char *name, MergeResult &res)
{
    if (local == remote) {
        out = remote;
    } else if (local == base) {
        out = remote;
    } else if (remote == base) {
        out = local;
        res.localChangesKept = true;
    } else {
        out = remote;
        res.conflict = true;
        res.conflictFields.append(QString::fromLatin1(name));
    }
}
}

bool sameContent(const Task &a, const Task &b)
{
    return a.title == b.title && a.notes == b.notes && a.priority == b.priority && a.quadrant == b.quadrant
        && a.estimatedMinutes == b.estimatedMinutes && a.trackedSeconds == b.trackedSeconds && a.pomodorosCompleted == b.pomodorosCompleted
        && Dates{a.start, a.due, a.allDay} == Dates{b.start, b.due, b.allDay}
        && Completion{a.status, a.percent, a.completedAt} == Completion{b.status, b.percent, b.completedAt} && a.tags == b.tags && a.rrule == b.rrule
        && a.alarms == b.alarms && a.parentUid == b.parentUid && qint64(a.sortOrder) == qint64(b.sortOrder);
}

MergeResult mergeTasks(const Task &base, const Task &local, const Task &remote)
{
    MergeResult res;
    Task &m = res.merged;
    m = local; // keeps id, list, uid, local-only fields

    mergeField(base.title, local.title, remote.title, m.title, "title", res);
    mergeField(base.notes, local.notes, remote.notes, m.notes, "notes", res);
    mergeField(base.priority, local.priority, remote.priority, m.priority, "priority", res);
    mergeField(base.quadrant, local.quadrant, remote.quadrant, m.quadrant, "quadrant", res);
    mergeField(base.estimatedMinutes, local.estimatedMinutes, remote.estimatedMinutes, m.estimatedMinutes, "estimated", res);
    mergeField(base.trackedSeconds, local.trackedSeconds, remote.trackedSeconds, m.trackedSeconds, "tracked", res);
    mergeField(base.pomodorosCompleted, local.pomodorosCompleted, remote.pomodorosCompleted, m.pomodorosCompleted, "pomodoros", res);
    mergeField(base.tags, local.tags, remote.tags, m.tags, "tags", res);
    mergeField(base.rrule, local.rrule, remote.rrule, m.rrule, "recurrence", res);
    mergeField(base.alarms, local.alarms, remote.alarms, m.alarms, "reminders", res);
    mergeField(base.parentUid, local.parentUid, remote.parentUid, m.parentUid, "parent", res);

    Dates d{};
    mergeField(Dates{base.start, base.due, base.allDay}, Dates{local.start, local.due, local.allDay}, Dates{remote.start, remote.due, remote.allDay}, d, "dates", res);
    m.start = d.start;
    m.due = d.due;
    m.allDay = d.allDay;

    Completion c{};
    mergeField(Completion{base.status, base.percent, base.completedAt},
               Completion{local.status, local.percent, local.completedAt},
               Completion{remote.status, remote.percent, remote.completedAt},
               c,
               "completion",
               res);
    m.status = c.status;
    m.percent = c.percent;
    m.completedAt = c.completedAt;

    // Manual order never causes a conflict: the remote position wins.
    const qint64 b = qint64(base.sortOrder), l = qint64(local.sortOrder), r = qint64(remote.sortOrder);
    if (l != b && r == b) {
        m.sortOrder = local.sortOrder;
        res.localChangesKept = true;
    } else {
        m.sortOrder = remote.sortOrder;
    }

    m.created = remote.created.isValid() ? remote.created : local.created;
    m.modified = qMax(local.modified, remote.modified);
    if (res.conflict) {
        res.localChangesKept = false; // the caller keeps the local version as a separate copy
        m = remote;
        m.id = local.id;
        m.listId = local.listId;
        m.collapsed = local.collapsed;
    }
    return res;
}

}
