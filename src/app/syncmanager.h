// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "syncengine.h"
#include "taskstore.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVariantList>

class Settings;

/**
 * Runs the sync engine on its own thread and decides when to sync:
 * at start, periodically, shortly after local changes, when the network
 * comes back and on request. Also owns the accounts and their credentials
 * (kept in the system keyring via QtKeychain).
 */
class SyncManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(bool offline READ offline NOTIFY statusChanged)
    Q_PROPERTY(bool hasAccounts READ hasAccounts NOTIFY accountsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)
    Q_PROPERTY(bool loginInProgress READ loginInProgress NOTIFY loginChanged)
    Q_PROPERTY(QString loginStatus READ loginStatus NOTIFY loginChanged)

public:
    SyncManager(TaskStore *store, Settings *settings, QObject *parent = nullptr);
    ~SyncManager() override;

    bool syncing() const
    {
        return !m_running.isEmpty() || !m_notesRunning.isEmpty() || !m_routineRunning.isEmpty();
    }
    bool offline() const
    {
        return m_offline;
    }
    bool hasAccounts() const;
    QString statusText() const;
    QString errorText() const;
    QVariantList accounts() const;
    bool loginInProgress() const
    {
        return m_loginActive;
    }
    QString loginStatus() const
    {
        return m_loginStatus;
    }

    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void syncNotesNow();
    Q_INVOKABLE void syncRoutinesNow();
    Q_INVOKABLE QVariantList routineCalendars(qlonglong accountId) const;
    Q_INVOKABLE QString selectedRoutineCalendar(qlonglong accountId) const;
    Q_INVOKABLE void setSelectedRoutineCalendar(qlonglong accountId, const QString &href);
    void scheduleNotesSync();
    void scheduleRoutinesSync();
    /** Nextcloud Login Flow v2: opens the browser and waits for the app password. */
    Q_INVOKABLE void startLoginFlow(const QString &serverUrl);
    Q_INVOKABLE void cancelLogin();
    /** Any CalDAV server, or Nextcloud with an app password typed by hand. */
    Q_INVOKABLE void addAccountManually(const QString &serverUrl, const QString &user, const QString &password);
    Q_INVOKABLE void removeAccount(qlonglong id);
    Q_INVOKABLE void renameAccount(qlonglong id, const QString &name);

Q_SIGNALS:
    void syncingChanged();
    void statusChanged();
    void accountsChanged();
    void loginChanged();
    void accountAdded(const QString &name, int taskLists);
    void loginFailed(const QString &message);
    void conflicts(const QString &accountName, const QStringList &titles);
    void syncProblem(const QString &accountName, const QString &message, bool needsLogin);
    void notesSynced();
    void routinesSynced();

    // to the worker thread
    void requestSync(qint64 accountId, const QString &password);
    void requestNotesSync(qint64 accountId, const QString &password);
    void requestRoutineSync(qint64 accountId, const QString &password);
    void requestVerify(int requestId, const QString &serverUrl, const QString &user, const QString &password);

private:
    void syncAccount(qint64 id);
    void syncNotesAccount(qint64 id);
    void syncRoutineAccount(qint64 id);
    void syncPending();
    void syncPendingNotes();
    void syncPendingRoutines();
    void loadPasswords();
    void storePassword(qint64 accountId, const QString &password);
    void onSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles);
    void onNotesSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles);
    void onRoutineSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles);
    void onVerifyFinished(int requestId, int errorKind, const QString &message, const QString &davUrl, const QString &principalUrl, const QString &homeUrl, int taskLists);
    void verifyCredentials(const QString &serverUrl, const QString &user, const QString &password);
    void pollLogin();
    void setLoginState(bool active, const QString &status);
    void applyInterval();
    static QString describeError(int kind, const QString &message);

    struct PendingAccount {
        QString serverUrl;
        QString user;
        QString password;
    };

    TaskStore *m_store;
    Settings *m_settings;
    QThread m_thread;
    KTasks::SyncEngine *m_engine;
    QHash<qint64, QString> m_passwords;
    QSet<qint64> m_passwordMissing;
    QSet<qint64> m_running;
    QSet<qint64> m_again; // changed while syncing: sync once more afterwards
    QSet<qint64> m_notesRunning;
    QSet<qint64> m_notesAgain;
    QSet<qint64> m_routineRunning;
    QSet<qint64> m_routineAgain;
    QHash<qint64, QString> m_errors;
    QSet<qint64> m_notifiedProblems;
    bool m_offline = false;
    QTimer m_intervalTimer;
    QTimer m_debounce;
    QTimer m_notesDebounce;
    QTimer m_routineDebounce;
    QDateTime m_lastSync;

    QNetworkAccessManager m_nam;
    QTimer m_loginPoll;
    QString m_loginEndpoint;
    QString m_loginToken;
    QDateTime m_loginDeadline;
    bool m_loginActive = false;
    QString m_loginStatus;
    int m_verifySeq = 0;
    QHash<int, PendingAccount> m_verifying;
};
