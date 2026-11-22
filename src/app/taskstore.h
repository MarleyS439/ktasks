// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <optional>

/**
 * The main thread's view of the data: an in-memory copy of the database
 * plus every user-initiated change, each of which is one undo step.
 */
class TaskStore : public QObject
{
    Q_OBJECT
public:
    explicit TaskStore(KTasks::Database *db, QObject *parent = nullptr);

    /** Re-reads everything (after a sync, an import, ...). */
    void reload();

    const QList<KTasks::Task> &tasks() const
    {
        return m_tasks;
    }
    const QList<KTasks::TaskList> &lists() const
    {
        return m_lists;
    }
    const QList<KTasks::Account> &accounts() const
    {
        return m_accounts;
    }
    const KTasks::Task *task(qint64 id) const;
    const KTasks::Task *taskByUid(qint64 listId, const QString &uid) const;
    const KTasks::TaskList *list(qint64 id) const;
    const KTasks::Account *account(qint64 id) const;
    QList<const KTasks::Task *> childrenOf(const KTasks::Task &parent) const;
    QList<const KTasks::Task *> descendantsOf(const KTasks::Task &parent) const;
    QStringList allTags() const;
    /** The list new tasks go to when nothing else is known; creates one if needed. */
    qint64 fallbackListId(qint64 preferred = 0);
    KTasks::Database *database() const
    {
        return m_db;
    }

    // --- tasks (undoable) ---
    qint64 addTask(KTasks::Task task);
    void updateTask(const KTasks::Task &task, const QString &what);
    void updateTasks(const QList<KTasks::Task> &tasks, const QString &what);
    /** Completes or reopens a task; handles subtasks and recurrence. */
    void setCompleted(qint64 id, bool done);
    /** Completes or reopens multiple tasks in a single undoable step. */
    void setCompletedMany(const QList<qint64> &ids, bool done);
    void recordFocus(qint64 id, int addSeconds, int addPomodoros);
    void removeTask(qint64 id);
    void removeTasks(const QList<qint64> &ids, const QString &what);
    void moveToList(qint64 id, qint64 listId);
    void moveToListMany(const QList<qint64> &ids, qint64 listId);
    int removeCompleted(qint64 listId);
    int importTasks(QList<KTasks::Task> tasks, qint64 listId);
    void setCollapsed(qint64 id, bool collapsed);

    // --- lists (not undoable) ---
    qint64 addList(const QString &name, const QString &color, qint64 accountId);
    void updateList(qint64 id, const QString &name, const QString &color);
    bool moveListToAccount(qint64 id, qint64 targetAccountId);
    void removeList(qint64 id);

    // --- undo ---
    bool canUndo() const
    {
        return !m_undo.isEmpty();
    }
    bool canRedo() const
    {
        return !m_redo.isEmpty();
    }
    QString undoText() const;
    QString redoText() const;
    void undo();
    void redo();

Q_SIGNALS:
    void changed();
    /** A change made by the user that should be uploaded. */
    void localChange();
    void undoChanged();

private:
    struct Change {
        std::optional<KTasks::Task> before;
        std::optional<KTasks::Task> after;
    };
    struct Step {
        QString text;
        QList<Change> changes;
    };
    void commit(const QString &text, QList<Change> changes);
    bool writeState(qint64 id, std::optional<KTasks::Task> &target);
    void applyStep(const Step &step, bool forward);
    static void normalize(KTasks::Task &t);
    void appendCompletionChanges(const KTasks::Task &cur, bool done, const QDateTime &now, QList<Change> &changes, QSet<qint64> &seen, bool *repeatedOut = nullptr) const;
    void rebuildIndex();

    KTasks::Database *m_db;
    QList<KTasks::Task> m_tasks;
    QList<KTasks::TaskList> m_lists;
    QList<KTasks::Account> m_accounts;
    QHash<qint64, int> m_taskIndex;
    QHash<QString, QList<int>> m_children; // "listId/parentUid" -> indexes
    QList<Step> m_undo;
    QList<Step> m_redo;
};
