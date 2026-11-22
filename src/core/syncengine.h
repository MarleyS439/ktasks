// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "caldavclient.h"
#include "database.h"

#include <QObject>
#include <atomic>
#include <memory>

namespace KTasks
{

/**
 * Two-way CalDAV synchronization. All work is blocking and meant for a
 * worker thread; the engine opens its own database connection there.
 *
 * Local changes are queued in the database itself (the sync_state column),
 * so they survive restarts and are retried until the server accepts them.
 */
class SyncEngine : public QObject
{
    Q_OBJECT
public:
    enum ErrorKind { NoError = 0, Offline, Auth, Server, Cancelled, Internal };
    Q_ENUM(ErrorKind)

    struct Result {
        ErrorKind kind = NoError;
        QString message;
        int pulled = 0;
        int pushed = 0;
        QStringList conflictTitles;
        bool changedLocal = false;
        bool ok() const
        {
            return kind == NoError;
        }
    };

    struct VerifyResult {
        ErrorKind kind = NoError;
        QString message;
        QString davUrl;
        QString principalUrl;
        QString homeUrl;
        int taskLists = 0;
    };

    explicit SyncEngine(const QString &dbPath, QObject *parent = nullptr);
    ~SyncEngine() override;

    /** Suffix appended to the title of the local copy kept after a conflict. */
    void setConflictSuffix(const QString &suffix)
    {
        m_conflictSuffix = suffix;
    }
    void setTimeoutMs(int ms)
    {
        m_timeoutMs = ms;
    }

    Result syncAccount(qint64 accountId, const QString &password);
    Result syncNotesAccount(qint64 accountId, const QString &password);
    Result syncRoutineAccount(qint64 accountId, const QString &password);
    VerifyResult verify(const QString &serverUrl, const QString &user, const QString &password);

    /** Thread-safe: aborts the sync that is running, if any. */
    void cancel();

public Q_SLOTS:
    void startSync(qint64 accountId, const QString &password);
    void startNotesSync(qint64 accountId, const QString &password);
    void startRoutineSync(qint64 accountId, const QString &password);
    void startVerify(int requestId, const QString &serverUrl, const QString &user, const QString &password);

Q_SIGNALS:
    void syncStarted(qint64 accountId);
    void syncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles);
    void notesSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles);
    void routineSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles);
    void dataChanged();
    void verifyFinished(int requestId, int errorKind, const QString &message, const QString &davUrl, const QString &principalUrl, const QString &homeUrl, int taskLists);

private:
    struct QueuedJob {
        enum Kind { Tasks, Notes, Routines, Verify };
        Kind kind = Tasks;
        qint64 accountId = 0;
        int requestId = 0;
        QString serverUrl;
        QString user;
        QString password;
    };
    void runJobs(QueuedJob first);
    bool ensureDb();
    bool fail(const DavResponse &r, const QString &context);
    bool softFail(const DavResponse &r, const QString &context);
    bool syncLists(Account &acc, const QList<DavCollection> &remote);
    bool syncList(TaskList list, const DavCollection &col);
    bool syncNextcloudNotes(const Account &acc);
    bool syncRoutineEvents(Account &acc);
    bool pushRoutineBlock(const QString &calHref, RoutineBlock b, qint64 accountId, int depth = 0);
    bool pullFull(const TaskList &list);
    bool fetchAndApply(const TaskList &list, const QList<DavItem> &changed);
    void applyRemote(const TaskList &list, const QString &href, const QString &etag, const QString &ics);
    void applyRemoteDelete(const TaskList &list, const QString &href);
    bool pushTask(const TaskList &list, Task task, int depth = 0);
    void touched();

    QString m_dbPath;
    std::unique_ptr<Database> m_db;
    std::unique_ptr<CalDavClient> m_client;
    std::atomic_bool m_cancel{false};
    bool m_jobActive = false;
    QList<QueuedJob> m_jobs;
    QString m_conflictSuffix = QStringLiteral("conflict");
    int m_timeoutMs = 30000;
    Result m_result;
    QString m_softError;
};

}
