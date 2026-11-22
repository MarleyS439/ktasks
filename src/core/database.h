// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "task.h"

#include <QSqlDatabase>
#include <QSqlQuery>
#include <optional>

namespace KTasks
{

/**
 * SQLite storage. One instance per thread; every instance opens its own
 * connection to the same file (WAL mode makes that safe).
 */
class Database
{
public:
    explicit Database(const QString &path);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /** Opens the file and brings the schema up to date. */
    bool open(QString *error = nullptr);
    bool isOpen() const;
    QString path() const
    {
        return m_path;
    }
    static int latestSchemaVersion();
    int schemaVersion();
    /** Test hook: extra migrations appended after the built-in ones. */
    static void setExtraMigrationsForTesting(const QStringList &migrations);

    /** RAII write transaction (BEGIN IMMEDIATE); nests safely. */
    class Transaction
    {
    public:
        explicit Transaction(Database *db);
        ~Transaction();
        bool commit();

    private:
        Database *m_db;
        bool m_owner = false;
        bool m_done = false;
    };

    // Accounts
    QList<Account> accounts();
    std::optional<Account> account(qint64 id);
    bool insertAccount(Account &a);
    bool updateAccount(const Account &a);
    /** Removes the account with all of its lists and tasks. */
    bool removeAccount(qint64 id);

    // Lists
    QList<TaskList> lists(bool includeDeleted = false);
    std::optional<TaskList> list(qint64 id);
    bool insertList(TaskList &l);
    /** User edit: name/color/icon/order. Marks a synced list as modified. */
    bool updateListUser(const TaskList &l);
    /** Writes every column as given (sync engine). */
    bool updateListFull(const TaskList &l);
    /** Removes the list and its tasks immediately. */
    bool purgeList(qint64 id);
    /** User delete: purges local/unsynced lists, leaves a tombstone otherwise. */
    bool removeListUser(qint64 id);
    /** Converts an existing writable list between Offline Local (0) and a Nextcloud account. */
    bool moveListToAccount(qint64 listId, qint64 targetAccountId);

    // Tasks
    QList<Task> tasks(bool includeDeleted = false, bool includeBaseIcs = true);
    QList<Task> tasksInList(qint64 listId, bool includeDeleted = true);
    std::optional<Task> task(qint64 id);
    std::optional<Task> taskByHref(qint64 listId, const QString &href);
    std::optional<Task> taskByUid(qint64 listId, const QString &uid);
    bool insertTask(Task &t);
    /**
     * User edit: writes the user-visible columns only, bumps rev and derives
     * the sync state (New if never uploaded, Modified otherwise).
     */
    bool updateTaskUser(const Task &t);
    /** Writes every column as given, including sync metadata (sync engine). */
    bool updateTaskFull(const Task &t);
    /**
     * After a successful upload: stores the sync metadata and marks the task
     * Synced unless the user changed it meanwhile (rev differs).
     */
    bool markUploaded(qint64 id, qint64 revAtUpload, const QString &href, const QString &etag, const QString &ics);
    /** User delete: hard-deletes never-uploaded tasks, leaves a tombstone otherwise. */
    bool removeTaskUser(qint64 id);
    bool purgeTask(qint64 id);
    bool setCollapsed(qint64 id, bool collapsed);
    /**
     * Moves a task to another list. An uploaded task leaves a tombstone in
     * the old list and becomes New in the target list.
     */
    bool moveTask(qint64 id, qint64 newListId);
    bool hasPendingChanges(qint64 accountId);
    bool hasPendingJournalChanges(qint64 accountId);
    bool hasPendingRoutineChanges(qint64 accountId);

    // Reminder bookkeeping
    bool reminderFired(const QString &uid, const QString &key);
    void setReminderFired(const QString &uid, const QString &key);
    QDateTime snoozedUntil(const QString &uid);
    void setSnooze(const QString &uid, const QDateTime &until);
    void clearSnooze(const QString &uid);
    QList<QPair<QString, QDateTime>> snoozes();
    void pruneReminderLog(const QDateTime &olderThan);

    // Key/value
    QString meta(const QString &key, const QString &def = QString());
    void setMeta(const QString &key, const QString &value);

    // Journal notebooks & entries
    QList<JournalNotebook> journalNotebooks();
    bool insertJournalNotebook(JournalNotebook &nb);
    bool updateJournalNotebook(const JournalNotebook &nb);
    bool removeJournalNotebook(qint64 id);
    QList<JournalEntry> journalEntries(bool includeDeleted = false);
    std::optional<JournalEntry> journalEntry(qint64 id);
    std::optional<JournalEntry> journalEntryByRemote(qint64 accountId, qint64 remoteId);
    bool insertJournalEntry(JournalEntry &e);
    bool updateJournalEntry(const JournalEntry &e);
    bool removeJournalEntry(qint64 id);
    bool removeJournalEntryUser(qint64 id);
    bool queueJournalRemoteDelete(qint64 accountId, qint64 remoteId);
    QList<qint64> journalRemoteDeletes(qint64 accountId);
    bool clearJournalRemoteDelete(qint64 accountId, qint64 remoteId);

    // Routine blocks and logs
    QList<RoutineBlock> routineBlocks(bool includeInactive = true, bool includeDeleted = false, bool includeBaseIcs = true);
    std::optional<RoutineBlock> routineBlock(qint64 id);
    std::optional<RoutineBlock> routineBlockByHref(qint64 accountId, const QString &href);
    std::optional<RoutineBlock> routineBlockByUid(qint64 accountId, const QString &uid);
    bool insertRoutineBlock(RoutineBlock &b);
    bool updateRoutineBlock(const RoutineBlock &b);
    bool updateRoutineBlockUser(const RoutineBlock &b);
    bool updateRoutineBlockFull(const RoutineBlock &b);
    bool markRoutineUploaded(qint64 id, qint64 revAtUpload, const QString &calendarHref, const QString &href, const QString &etag, const QString &ics);
    bool removeRoutineBlock(qint64 id);
    bool removeRoutineBlockUser(qint64 id);
    bool queueRoutineRemoteDelete(qint64 accountId, const QString &href, const QString &etag);
    QList<QPair<QString, QString>> routineRemoteDeletes(qint64 accountId);
    bool clearRoutineRemoteDelete(qint64 accountId, const QString &href);
    QList<RoutineLog> routineLogsForDate(const QDate &date);
    QList<RoutineLog> routineLogsForBlock(qint64 blockId, const QDate &since = QDate());
    bool setRoutineLog(qint64 blockId, const QDate &date, RoutineLogStatus status);

    // Focus / Pomodoro execution sessions
    bool insertFocusSession(FocusSession &s);
    QList<FocusSession> focusSessions(const QDateTime &since = {}, int limit = 500);
    bool removeFocusSession(qint64 id);

    // Local file backup and restore
    bool backupTo(const QString &destPath, QString *error = nullptr);
    static bool validateBackup(const QString &filePath, QString *error = nullptr);
    bool restoreFrom(const QString &sourcePath, QString *error = nullptr);

    QSqlDatabase handle() const;
    QString lastError() const
    {
        return m_lastError;
    }

private:
    bool exec(QSqlQuery &q);
    bool execSql(const QString &sql);
    bool migrate(QString *error);
    bool backupBeforeMigration(int fromVersion);
    static Task taskFromQuery(const QSqlQuery &q);
    static TaskList listFromQuery(const QSqlQuery &q);
    static Account accountFromQuery(const QSqlQuery &q);
    static JournalEntry journalFromQuery(const QSqlQuery &q);
    static RoutineBlock routineBlockFromQuery(const QSqlQuery &q);
    void bindTask(QSqlQuery &q, const Task &t, bool full);

    QString m_path;
    QString m_connName;
    QString m_lastError;
    int m_txDepth = 0;
    friend class Transaction;
};

/** Storage format for dates: ISO UTC for timed values, local "yyyy-MM-ddT00:00:00" for all-day. */
QString dateToDb(const QDateTime &dt, bool allDay);
QDateTime dateFromDb(const QString &s, bool allDay);

}
