// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace KTasks
{

enum class SyncState { Synced = 0, New = 1, Modified = 2, Deleted = 3 };

enum class TaskStatus { NeedsAction = 0, InProcess = 1, Completed = 2, Cancelled = 3 };

/** A reminder. Either an absolute point in time or an offset to the due/start date. */
struct Alarm {
    enum Kind { Absolute = 0, RelativeToDue = 1, RelativeToStart = 2 };
    Kind kind = RelativeToDue;
    QDateTime at; // Absolute only (UTC)
    int offsetSecs = 0; // negative = before

    bool operator==(const Alarm &o) const = default;
};

struct Task {
    qint64 id = 0;
    QString uid;
    qint64 listId = 0;
    QString parentUid;
    QString title;
    QString notes;
    int priority = 0; // iCalendar: 0 none, 1-4 high, 5 medium, 6-9 low
    int quadrant = 0; // Eisenhower Matrix: 0 auto/triage, 1 Q1, 2 Q2, 3 Q3, 4 Q4
    QDateTime start; // all-day: local midnight; otherwise UTC
    QDateTime due;
    bool allDay = true;
    QDateTime completedAt;
    TaskStatus status = TaskStatus::NeedsAction;
    int percent = 0;
    QStringList tags;
    QString rrule;
    QList<Alarm> alarms;
    double sortOrder = 0;
    QDateTime created;
    QDateTime modified;
    bool collapsed = false;
    int estimatedMinutes = 0;
    int trackedSeconds = 0;
    int pomodorosCompleted = 0;

    // Sync metadata
    QString href;
    QString etag;
    QString baseIcs; // server copy as of the last successful sync
    SyncState syncState = SyncState::New;
    qint64 rev = 0;

    bool isDone() const
    {
        return status == TaskStatus::Completed || status == TaskStatus::Cancelled;
    }
    /** 0 none, 1 low, 2 medium, 3 high */
    int priorityLevel() const
    {
        if (priority <= 0) {
            return 0;
        }
        return priority <= 4 ? 3 : (priority == 5 ? 2 : 1);
    }
    static int priorityFromLevel(int level)
    {
        switch (level) {
        case 3:
            return 1;
        case 2:
            return 5;
        case 1:
            return 9;
        default:
            return 0;
        }
    }
    static int priorityForQuadrant(int q)
    {
        switch (q) {
        case 1:
            return 1; // High
        case 2:
            return 5; // Medium
        case 3:
            return 9; // Low
        default:
            return 0; // None (incl. Q4)
        }
    }
    static int levelForQuadrant(int q)
    {
        switch (q) {
        case 1:
            return 3;
        case 2:
            return 2;
        case 3:
            return 1;
        default:
            return 0;
        }
    }
    /** The moment the task is due; all-day tasks are due at the end of their day. */
    QDateTime effectiveDue() const;
    /** When @p alarm fires, or an invalid date if it cannot be computed. */
    QDateTime alarmTime(const Alarm &alarm) const;
};

struct TaskList {
    qint64 id = 0;
    qint64 accountId = 0; // 0 = local
    QString name;
    QString color;
    QString icon;
    QString href;
    QString ctag;
    QString syncToken;
    double sortOrder = 0;
    SyncState syncState = SyncState::New;
    bool readOnly = false;
};

struct Account {
    qint64 id = 0;
    QString name;
    QString serverUrl; // what the user typed / the Nextcloud base URL
    QString davUrl; // resolved DAV entry point
    QString username;
    QString principalUrl;
    QString homeUrl;
    QDateTime lastSync;
    QString lastError;
};

struct JournalNotebook {
    qint64 id = 0;
    qint64 syncAccountId = 0; // 0 = offline local only
    QString name;
    QString color = QStringLiteral("#3daee9");
    QString icon = QStringLiteral("notebook");
    double sortOrder = 0;
    QDateTime created;
};

struct JournalEntry {
    qint64 id = 0;
    qint64 notebookId = 1;
    QString uid;
    QString title;
    QString content; // empty in DB when isLocked is true
    QDate entryDate;
    QStringList tags;
    QString mood;
    bool isLocked = false;
    QString kdfSalt;
    QString passwordHash;
    QString encryptedContent;
    QDateTime created;
    QDateTime modified;
    bool deleted = false;

    // Sync metadata (Nextcloud Notes REST API; syncAccountId == 0 means offline local only)
    qint64 syncAccountId = 0;
    qint64 remoteId = 0;
    QString etag;
    qint64 remoteModified = 0;
    SyncState syncState = SyncState::Synced;
};

struct RoutineBlock {
    qint64 id = 0;
    QString uid;
    QString title;
    QString notes;
    int startMinute = 540; // 09:00
    int durationMinutes = 60;
    int daysMask = 127; // bit 0 = Mon .. bit 6 = Sun; 0 = specificDate only
    QString repeatFreq = QStringLiteral("WEEKLY"); // DAILY, WEEKLY, MONTHLY, YEARLY
    int repeatInterval = 1; // >= 1
    QDate specificDate; // non-empty for single-day planned items
    QDate startDate; // earliest active date for recurring templates; invalid = unrestricted
    qint64 linkedTaskId = 0;
    QString color = QStringLiteral("#3daee9");
    QString category;
    bool notifyEnabled = true;
    int notifyMinutesBefore = 5;
    bool active = true;
    double sortOrder = 0;
    QDateTime created;
    QDateTime modified;

    // Sync metadata (CalDAV VEVENT; syncAccountId == 0 means offline local only)
    qint64 syncAccountId = 0;
    QString calendarHref;
    QString href;
    QString etag;
    QString baseIcs;
    SyncState syncState = SyncState::Synced;
    qint64 rev = 0;
    bool deleted = false;
};

enum class RoutineLogStatus { Pending = 0, Done = 1, Skipped = 2 };

struct RoutineLog {
    qint64 blockId = 0;
    QDate logDate;
    RoutineLogStatus status = RoutineLogStatus::Pending;
    QDateTime completedAt;
};

enum class FocusMode { Pomodoro = 0, Break = 1, Stopwatch = 2 };

struct FocusSession {
    qint64 id = 0;
    qint64 taskId = 0;
    QString taskUid;
    QString taskTitle;
    qint64 listId = 0;
    int quadrant = 0;
    FocusMode mode = FocusMode::Pomodoro;
    int durationSeconds = 0;
    bool completedPomodoro = false;
    QDateTime startedAt;
    QDateTime endedAt;
};

QString newUid();
QString alarmsToJson(const QList<Alarm> &alarms);
QList<Alarm> alarmsFromJson(const QString &json);
/** Default manual sort position: seconds since 2001-01-01, as used by Apple and Nextcloud Tasks. */
double defaultSortOrder(const QDateTime &created);

}
