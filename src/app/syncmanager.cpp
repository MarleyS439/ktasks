// SPDX-License-Identifier: GPL-3.0-or-later
#include "syncmanager.h"
#include "logging.h"
#include "settings.h"

#include <KLocalizedString>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkInformation>
#include <QNetworkReply>
#include <QUrlQuery>
#include <qt6keychain/keychain.h>

using namespace KTasks;

static const QString kKeychainService = QStringLiteral("KTasks");

static QString keyFor(qint64 accountId)
{
    return QStringLiteral("account-%1").arg(accountId);
}

SyncManager::SyncManager(TaskStore *store, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
{
    m_engine = new SyncEngine(store->database()->path());
    m_engine->setConflictSuffix(i18nc("appended to the title of the local copy kept after a sync conflict", "conflicting copy"));
    m_engine->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_engine, &QObject::deleteLater);
    connect(this, &SyncManager::requestSync, m_engine, &SyncEngine::startSync);
    connect(this, &SyncManager::requestNotesSync, m_engine, &SyncEngine::startNotesSync);
    connect(this, &SyncManager::requestRoutineSync, m_engine, &SyncEngine::startRoutineSync);
    connect(this, &SyncManager::requestVerify, m_engine, &SyncEngine::startVerify);
    connect(m_engine, &SyncEngine::syncFinished, this, &SyncManager::onSyncFinished);
    connect(m_engine, &SyncEngine::notesSyncFinished, this, &SyncManager::onNotesSyncFinished);
    connect(m_engine, &SyncEngine::routineSyncFinished, this, &SyncManager::onRoutineSyncFinished);
    connect(m_engine, &SyncEngine::verifyFinished, this, &SyncManager::onVerifyFinished);
    connect(m_engine, &SyncEngine::dataChanged, m_store, &TaskStore::reload);
    m_thread.setObjectName(QStringLiteral("ktasks-sync"));
    m_thread.start();

    // Upload shortly after the user stops editing rather than on every keystroke.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(4000);
    connect(&m_debounce, &QTimer::timeout, this, &SyncManager::syncPending);
    m_notesDebounce.setSingleShot(true);
    m_notesDebounce.setInterval(2500);
    connect(&m_notesDebounce, &QTimer::timeout, this, &SyncManager::syncPendingNotes);
    m_routineDebounce.setSingleShot(true);
    m_routineDebounce.setInterval(2500);
    connect(&m_routineDebounce, &QTimer::timeout, this, &SyncManager::syncPendingRoutines);
    connect(store, &TaskStore::localChange, this, [this] {
        if (hasAccounts()) {
            m_debounce.start();
        }
    });
    connect(store, &TaskStore::changed, this, &SyncManager::accountsChanged);

    connect(&m_intervalTimer, &QTimer::timeout, this, &SyncManager::syncNow);
    connect(settings, &Settings::syncIntervalChanged, this, &SyncManager::applyInterval);
    applyInterval();

    if (QNetworkInformation::loadDefaultBackend()) {
        auto *info = QNetworkInformation::instance();
        connect(info, &QNetworkInformation::reachabilityChanged, this, [this](QNetworkInformation::Reachability r) {
            if (r == QNetworkInformation::Reachability::Online || r == QNetworkInformation::Reachability::Site) {
                qCInfo(lcSync) << "network is back, syncing";
                syncNow();
            }
        });
    }

    m_loginPoll.setInterval(2000);
    connect(&m_loginPoll, &QTimer::timeout, this, &SyncManager::pollLogin);

    loadPasswords();
}

SyncManager::~SyncManager()
{
    m_engine->cancel();
    m_thread.quit();
    m_thread.wait(8000);
}

void SyncManager::applyInterval()
{
    const int minutes = m_settings->syncInterval();
    if (minutes > 0) {
        m_intervalTimer.start(minutes * 60 * 1000);
    } else {
        m_intervalTimer.stop();
    }
}

bool SyncManager::hasAccounts() const
{
    return !m_store->accounts().isEmpty();
}

QVariantList SyncManager::accounts() const
{
    QVariantList out;
    for (const Account &a : m_store->accounts()) {
        QVariantMap m;
        m[QStringLiteral("id")] = a.id;
        m[QStringLiteral("name")] = a.name;
        m[QStringLiteral("server")] = a.serverUrl;
        m[QStringLiteral("username")] = a.username;
        m[QStringLiteral("lastSync")] = a.lastSync.isValid() ? QLocale().toString(a.lastSync.toLocalTime(), QLocale::ShortFormat) : QString();
        QString error = m_errors.value(a.id);
        if (m_passwordMissing.contains(a.id)) {
            error = i18n("The password is not in the keyring. Sign in again.");
        }
        m[QStringLiteral("error")] = error;
        m[QStringLiteral("needsLogin")] = m_passwordMissing.contains(a.id);
        m[QStringLiteral("syncing")] = m_running.contains(a.id);
        out.append(m);
    }
    return out;
}

QString SyncManager::statusText() const
{
    if (!hasAccounts()) {
        return i18nc("sync status", "Local only");
    }
    if (syncing()) {
        return i18nc("sync status", "Synchronizing…");
    }
    if (!errorText().isEmpty()) {
        return i18nc("sync status", "Sync problem");
    }
    if (m_offline) {
        return i18nc("sync status", "Offline – changes are saved and will be sent later");
    }
    if (m_lastSync.isValid()) {
        return i18nc("sync status, %1 is a time", "Synchronized at %1", QLocale().toString(m_lastSync.toLocalTime().time(), QLocale::ShortFormat));
    }
    return i18nc("sync status", "Not synchronized yet");
}

QString SyncManager::errorText() const
{
    QStringList parts;
    for (const Account &a : m_store->accounts()) {
        if (m_passwordMissing.contains(a.id)) {
            parts << i18nc("account: problem", "%1: sign in again", a.name);
        } else if (m_errors.contains(a.id)) {
            parts << i18nc("account: problem", "%1: %2", a.name, m_errors.value(a.id));
        }
    }
    return parts.join(QLatin1Char('\n'));
}

QString SyncManager::describeError(int kind, const QString &message)
{
    switch (kind) {
    case SyncEngine::Offline:
        return i18n("The server cannot be reached.");
    case SyncEngine::Auth:
        return i18n("The server rejected the user name or password.");
    case SyncEngine::Cancelled:
        return i18n("Cancelled.");
    case SyncEngine::Server:
        return i18nc("%1 is a technical detail such as 'HTTP 503'", "The server reported a problem (%1).", message);
    default:
        return message;
    }
}

// ---- credentials ----

void SyncManager::loadPasswords()
{
    for (const Account &a : m_store->accounts()) {
        const qint64 id = a.id;
        auto *job = new QKeychain::ReadPasswordJob(kKeychainService, this);
        job->setKey(keyFor(id));
        connect(job, &QKeychain::Job::finished, this, [this, job, id] {
            if (job->error() == QKeychain::NoError && !job->textData().isEmpty()) {
                m_passwords.insert(id, job->textData());
                m_passwordMissing.remove(id);
                if (m_settings->syncOnStart()) {
                    syncAccount(id);
                    if (m_settings->syncNotes()) {
                        syncNotesAccount(id);
                    }
                    if (m_settings->syncRoutines()) {
                        syncRoutineAccount(id);
                    }
                }
            } else {
                // One-time seamless migration from legacy "Afazer" keyring entries
                auto *legacyJob = new QKeychain::ReadPasswordJob(QStringLiteral("Afazer"), this);
                legacyJob->setKey(keyFor(id));
                connect(legacyJob, &QKeychain::Job::finished, this, [this, legacyJob, id] {
                    if (legacyJob->error() == QKeychain::NoError && !legacyJob->textData().isEmpty()) {
                        const QString pw = legacyJob->textData();
                        storePassword(id, pw);
                        if (m_settings->syncOnStart()) {
                            syncAccount(id);
                            if (m_settings->syncNotes()) {
                                syncNotesAccount(id);
                            }
                            if (m_settings->syncRoutines()) {
                                syncRoutineAccount(id);
                            }
                        }
                    } else {
                        qCWarning(lcSync) << "no stored credentials for account" << id << legacyJob->errorString();
                        m_passwordMissing.insert(id);
                    }
                    Q_EMIT accountsChanged();
                    Q_EMIT statusChanged();
                });
                legacyJob->start();
                return;
            }
            Q_EMIT accountsChanged();
            Q_EMIT statusChanged();
        });
        job->start();
    }
}

void SyncManager::storePassword(qint64 accountId, const QString &password)
{
    m_passwords.insert(accountId, password);
    m_passwordMissing.remove(accountId);
    auto *job = new QKeychain::WritePasswordJob(kKeychainService, this);
    job->setKey(keyFor(accountId));
    job->setTextData(password);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        if (job->error() != QKeychain::NoError) {
            // Still usable for this session; the user has to sign in again after a restart.
            qCWarning(lcSync) << "could not store the password in the keyring:" << job->errorString();
            Q_EMIT loginFailed(i18n("The password could not be saved in the keyring (%1). You will have to sign in again next time.", job->errorString()));
        }
    });
    job->start();
}

// ---- syncing ----

void SyncManager::syncNow()
{
    for (const Account &a : m_store->accounts()) {
        syncAccount(a.id);
        if (m_settings->syncNotes()) {
            syncNotesAccount(a.id);
        }
        if (m_settings->syncRoutines()) {
            syncRoutineAccount(a.id);
        }
    }
}

void SyncManager::syncNotesNow()
{
    for (const Account &a : m_store->accounts()) {
        syncNotesAccount(a.id);
    }
}

void SyncManager::syncRoutinesNow()
{
    for (const Account &a : m_store->accounts()) {
        syncRoutineAccount(a.id);
    }
}

void SyncManager::scheduleNotesSync()
{
    if (hasAccounts()) {
        m_notesDebounce.start();
    }
}

void SyncManager::scheduleRoutinesSync()
{
    if (hasAccounts()) {
        m_routineDebounce.start();
    }
}

QVariantList SyncManager::routineCalendars(qlonglong accountId) const
{
    const QString raw = m_store->database()->meta(QStringLiteral("routine_cals_%1").arg(accountId));
    if (raw.isEmpty()) {
        return {};
    }
    return QJsonDocument::fromJson(raw.toUtf8()).toVariant().toList();
}

QString SyncManager::selectedRoutineCalendar(qlonglong accountId) const
{
    const QString pref = m_store->database()->meta(QStringLiteral("routine_cal_pref_%1").arg(accountId));
    return pref.isEmpty() ? m_store->database()->meta(QStringLiteral("routine_cal_%1").arg(accountId)) : pref;
}

void SyncManager::setSelectedRoutineCalendar(qlonglong accountId, const QString &href)
{
    m_store->database()->setMeta(QStringLiteral("routine_cal_pref_%1").arg(accountId), href.trimmed());
    m_store->database()->setMeta(QStringLiteral("routine_ctag_%1").arg(accountId), QString());
    Q_EMIT accountsChanged();
    if (m_settings->syncRoutines()) {
        syncRoutineAccount(accountId);
    }
}

void SyncManager::syncPending()
{
    for (const Account &a : m_store->accounts()) {
        if (m_store->database()->hasPendingChanges(a.id)) {
            syncAccount(a.id);
        }
        if (m_settings->syncNotes() && m_store->database()->hasPendingJournalChanges(a.id)) {
            syncNotesAccount(a.id);
        }
        if (m_settings->syncRoutines() && m_store->database()->hasPendingRoutineChanges(a.id)) {
            syncRoutineAccount(a.id);
        }
    }
}

void SyncManager::syncPendingNotes()
{
    for (const Account &a : m_store->accounts()) {
        if (m_store->database()->hasPendingJournalChanges(a.id)) {
            syncNotesAccount(a.id);
        }
    }
}

void SyncManager::syncPendingRoutines()
{
    for (const Account &a : m_store->accounts()) {
        if (m_store->database()->hasPendingRoutineChanges(a.id)) {
            syncRoutineAccount(a.id);
        }
    }
}

void SyncManager::syncAccount(qint64 id)
{
    if (!m_passwords.contains(id)) {
        return;
    }
    if (m_running.contains(id)) {
        m_again.insert(id);
        return;
    }
    m_running.insert(id);
    Q_EMIT syncingChanged();
    Q_EMIT statusChanged();
    Q_EMIT accountsChanged();
    Q_EMIT requestSync(id, m_passwords.value(id));
}

void SyncManager::syncNotesAccount(qint64 id)
{
    if (!m_passwords.contains(id)) {
        return;
    }
    if (m_notesRunning.contains(id)) {
        m_notesAgain.insert(id);
        return;
    }
    m_notesRunning.insert(id);
    Q_EMIT syncingChanged();
    Q_EMIT statusChanged();
    Q_EMIT requestNotesSync(id, m_passwords.value(id));
}

void SyncManager::onNotesSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles)
{
    Q_UNUSED(pulled)
    Q_UNUSED(pushed)
    m_notesRunning.remove(accountId);
    const Account *acc = m_store->account(accountId);
    const QString name = acc ? acc->name : QString();

    if (errorKind == SyncEngine::NoError) {
        m_offline = false;
    } else if (errorKind == SyncEngine::Offline) {
        m_offline = true;
    } else if (errorKind != SyncEngine::Cancelled && !m_notifiedProblems.contains(accountId)) {
        m_notifiedProblems.insert(accountId);
        Q_EMIT syncProblem(name, describeError(errorKind, message), errorKind == SyncEngine::Auth);
    }
    if (!conflictTitles.isEmpty()) {
        Q_EMIT conflicts(name, conflictTitles);
    }
    Q_EMIT notesSynced();
    Q_EMIT syncingChanged();
    Q_EMIT statusChanged();

    if (m_notesAgain.remove(accountId) && errorKind == SyncEngine::NoError) {
        syncNotesAccount(accountId);
    }
}

void SyncManager::syncRoutineAccount(qint64 id)
{
    if (!m_passwords.contains(id)) {
        return;
    }
    if (m_routineRunning.contains(id)) {
        m_routineAgain.insert(id);
        return;
    }
    m_routineRunning.insert(id);
    Q_EMIT syncingChanged();
    Q_EMIT statusChanged();
    Q_EMIT requestRoutineSync(id, m_passwords.value(id));
}

void SyncManager::onRoutineSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles)
{
    Q_UNUSED(pulled)
    Q_UNUSED(pushed)
    m_routineRunning.remove(accountId);
    const Account *acc = m_store->account(accountId);
    const QString name = acc ? acc->name : QString();

    if (errorKind == SyncEngine::NoError) {
        m_offline = false;
    } else if (errorKind == SyncEngine::Offline) {
        m_offline = true;
    } else if (errorKind != SyncEngine::Cancelled && !m_notifiedProblems.contains(accountId)) {
        m_notifiedProblems.insert(accountId);
        Q_EMIT syncProblem(name, describeError(errorKind, message), errorKind == SyncEngine::Auth);
    }
    if (!conflictTitles.isEmpty()) {
        Q_EMIT conflicts(name, conflictTitles);
    }
    Q_EMIT routinesSynced();
    Q_EMIT accountsChanged();
    Q_EMIT syncingChanged();
    Q_EMIT statusChanged();

    if (m_routineAgain.remove(accountId) && errorKind == SyncEngine::NoError) {
        syncRoutineAccount(accountId);
    }
}

void SyncManager::onSyncFinished(qint64 accountId, int errorKind, const QString &message, int pulled, int pushed, const QStringList &conflictTitles)
{
    Q_UNUSED(pulled)
    Q_UNUSED(pushed)
    m_running.remove(accountId);
    const Account *acc = m_store->account(accountId);
    const QString name = acc ? acc->name : QString();

    if (errorKind == SyncEngine::NoError) {
        m_errors.remove(accountId);
        m_notifiedProblems.remove(accountId);
        m_offline = false;
        m_lastSync = QDateTime::currentDateTime();
    } else if (errorKind == SyncEngine::Offline) {
        // Expected on a laptop: stay quiet, the changes are queued.
        m_offline = true;
        m_errors.remove(accountId);
    } else if (errorKind != SyncEngine::Cancelled) {
        m_offline = false;
        m_errors.insert(accountId, describeError(errorKind, message));
        if (!m_notifiedProblems.contains(accountId)) {
            m_notifiedProblems.insert(accountId);
            Q_EMIT syncProblem(name, describeError(errorKind, message), errorKind == SyncEngine::Auth);
        }
    }
    if (!conflictTitles.isEmpty()) {
        Q_EMIT conflicts(name, conflictTitles);
    }
    // Account rows changed (last sync time); lists and tasks were already reloaded.
    m_store->reload();
    Q_EMIT syncingChanged();
    Q_EMIT statusChanged();
    Q_EMIT accountsChanged();

    if (m_again.remove(accountId) && errorKind == SyncEngine::NoError) {
        syncAccount(accountId);
    }
}

// ---- adding accounts ----

void SyncManager::setLoginState(bool active, const QString &status)
{
    m_loginActive = active;
    m_loginStatus = status;
    if (!active) {
        m_loginPoll.stop();
        m_loginEndpoint.clear();
        m_loginToken.clear();
    }
    Q_EMIT loginChanged();
}

void SyncManager::startLoginFlow(const QString &serverUrl)
{
    QUrl base = QUrl::fromUserInput(serverUrl.trimmed());
    if (!base.isValid() || base.host().isEmpty()) {
        Q_EMIT loginFailed(i18n("That does not look like a server address."));
        return;
    }
    QString path = base.path();
    // Accept addresses copied from the browser, e.g. https://cloud.example.com/apps/tasks/
    const int idx = path.indexOf(QLatin1String("/index.php"));
    if (idx >= 0) {
        path.truncate(idx);
    }
    const int apps = path.indexOf(QLatin1String("/apps/"));
    if (apps >= 0) {
        path.truncate(apps);
    }
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    base.setPath(path);
    base.setQuery(QString());
    QUrl start = base;
    start.setPath(path + QStringLiteral("/index.php/login/v2"));

    setLoginState(true, i18n("Contacting the server…"));
    QNetworkRequest req(start);
    req.setRawHeader("User-Agent", "KTasks");
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_nam.post(req, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, base] {
        reply->deleteLater();
        if (!m_loginActive) {
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject poll = obj[QStringLiteral("poll")].toObject();
        const QUrl login(obj[QStringLiteral("login")].toString());
        if (reply->error() != QNetworkReply::NoError || poll.isEmpty() || !login.isValid()) {
            setLoginState(false, QString());
            Q_EMIT loginFailed(i18n("This server did not answer like a Nextcloud server (%1). Check the address, or use \"Other CalDAV server\".",
                                    reply->error() == QNetworkReply::NoError ? i18n("unexpected answer") : reply->errorString()));
            return;
        }
        // Only ever hand the browser and the token to the server the user named.
        const QUrl endpoint(poll[QStringLiteral("endpoint")].toString());
        if (endpoint.host() != base.host() || login.host() != base.host()) {
            setLoginState(false, QString());
            Q_EMIT loginFailed(i18n("The server redirected the sign-in to another address. Aborted for safety."));
            return;
        }
        m_loginEndpoint = endpoint.toString();
        m_loginToken = poll[QStringLiteral("token")].toString();
        m_loginDeadline = QDateTime::currentDateTime().addSecs(20 * 60);
        QDesktopServices::openUrl(login);
        setLoginState(true, i18n("Confirm the access in the browser window that just opened. Waiting…"));
        m_loginPoll.start();
    });
}

void SyncManager::pollLogin()
{
    if (!m_loginActive || m_loginEndpoint.isEmpty()) {
        return;
    }
    if (QDateTime::currentDateTime() > m_loginDeadline) {
        setLoginState(false, QString());
        Q_EMIT loginFailed(i18n("The sign-in was not confirmed in time."));
        return;
    }
    QNetworkRequest req{QUrl(m_loginEndpoint)};
    req.setRawHeader("User-Agent", "KTasks");
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(15000);
    QUrlQuery body;
    body.addQueryItem(QStringLiteral("token"), m_loginToken);
    QNetworkReply *reply = m_nam.post(req, body.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (!m_loginActive || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            return; // 404 until the user confirms
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const QString server = obj[QStringLiteral("server")].toString();
        const QString user = obj[QStringLiteral("loginName")].toString();
        const QString password = obj[QStringLiteral("appPassword")].toString();
        if (server.isEmpty() || user.isEmpty() || password.isEmpty()) {
            return;
        }
        m_loginPoll.stop();
        setLoginState(true, i18n("Signed in. Looking for task lists…"));
        verifyCredentials(server, user, password);
    });
}

void SyncManager::cancelLogin()
{
    setLoginState(false, QString());
}

void SyncManager::addAccountManually(const QString &serverUrl, const QString &user, const QString &password)
{
    if (serverUrl.trimmed().isEmpty() || user.trimmed().isEmpty() || password.isEmpty()) {
        Q_EMIT loginFailed(i18n("Fill in the address, the user name and the password."));
        return;
    }
    setLoginState(true, i18n("Checking the account…"));
    verifyCredentials(serverUrl.trimmed(), user.trimmed(), password);
}

void SyncManager::verifyCredentials(const QString &serverUrl, const QString &user, const QString &password)
{
    const int id = ++m_verifySeq;
    m_verifying.insert(id, PendingAccount{serverUrl, user, password});
    Q_EMIT requestVerify(id, serverUrl, user, password);
}

void SyncManager::onVerifyFinished(int requestId, int errorKind, const QString &message, const QString &davUrl, const QString &principalUrl, const QString &homeUrl, int taskLists)
{
    const PendingAccount pending = m_verifying.take(requestId);
    if (!m_loginActive) {
        return; // cancelled meanwhile
    }
    setLoginState(false, QString());
    if (errorKind != SyncEngine::NoError) {
        Q_EMIT loginFailed(describeError(errorKind, message));
        return;
    }
    // Signing in again to an account we already have just refreshes its password.
    for (const Account &a : m_store->accounts()) {
        if (a.davUrl == davUrl && a.username == pending.user) {
            storePassword(a.id, pending.password);
            m_errors.remove(a.id);
            m_notifiedProblems.remove(a.id);
            Q_EMIT accountAdded(a.name, taskLists);
            Q_EMIT accountsChanged();
            Q_EMIT statusChanged();
            syncAccount(a.id);
            if (m_settings->syncNotes()) {
                syncNotesAccount(a.id);
            }
            if (m_settings->syncRoutines()) {
                syncRoutineAccount(a.id);
            }
            return;
        }
    }
    Account a;
    a.serverUrl = pending.serverUrl;
    a.davUrl = davUrl;
    a.principalUrl = principalUrl;
    a.homeUrl = homeUrl;
    a.username = pending.user;
    const QString host = QUrl::fromUserInput(pending.serverUrl).host();
    a.name = host.isEmpty() ? pending.user : QStringLiteral("%1@%2").arg(pending.user, host);
    if (!m_store->database()->insertAccount(a)) {
        Q_EMIT loginFailed(i18n("The account could not be saved."));
        return;
    }
    storePassword(a.id, pending.password);
    m_store->reload();
    Q_EMIT accountAdded(a.name, taskLists);
    Q_EMIT accountsChanged();
    Q_EMIT statusChanged();
    syncAccount(a.id);
    if (m_settings->syncNotes()) {
        syncNotesAccount(a.id);
    }
    if (m_settings->syncRoutines()) {
        syncRoutineAccount(a.id);
    }
}

void SyncManager::removeAccount(qlonglong id)
{
    m_engine->cancel();
    m_store->database()->removeAccount(id);
    m_passwords.remove(id);
    m_passwordMissing.remove(id);
    m_running.remove(id);
    m_again.remove(id);
    m_notesRunning.remove(id);
    m_notesAgain.remove(id);
    m_routineRunning.remove(id);
    m_routineAgain.remove(id);
    m_errors.remove(id);
    auto *job = new QKeychain::DeletePasswordJob(kKeychainService, this);
    job->setKey(keyFor(id));
    job->start();
    m_store->reload();
    Q_EMIT accountsChanged();
    Q_EMIT statusChanged();
}

void SyncManager::renameAccount(qlonglong id, const QString &name)
{
    if (const Account *cur = m_store->account(id); cur && !name.trimmed().isEmpty()) {
        Account a = *cur;
        a.name = name.trimmed();
        m_store->database()->updateAccount(a);
        m_store->reload();
        Q_EMIT accountsChanged();
    }
}
