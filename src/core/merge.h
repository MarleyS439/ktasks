// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "task.h"

namespace KTasks
{

struct MergeResult {
    Task merged; // remote sync metadata is NOT applied here; only user fields
    bool conflict = false; // both sides changed the same field differently
    bool localChangesKept = false; // merged differs from remote, so it must be uploaded
    QStringList conflictFields;
};

/**
 * Three-way merge of the user-visible fields. Where only one side changed a
 * field, that side wins. Where both changed it to different values the
 * remote value is taken and the conflict is reported.
 */
MergeResult mergeTasks(const Task &base, const Task &local, const Task &remote);

/** True when all user-visible fields are equal. */
bool sameContent(const Task &a, const Task &b);

}
