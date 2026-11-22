// SPDX-License-Identifier: GPL-3.0-or-later
#include "syncengine.h"
#include "icscodec.h"
#include "logging.h"
#include "merge.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <QUuid>

namespace KTasks
{

static SyncEngine::ErrorKind classify(const DavResponse &r, bool cancelled)
{
    if (cancelled) {
        return SyncEngine::Cancelled;
    }
    if (r.status == 0) {
        switch (r.netError) {
        case QNetworkReply::SslHandshakeFailedError:
        case QNetworkReply::InsecureRedirectError:
        case QNetworkReply::TooManyRedirectsError:
        case QNetworkReply::ProtocolUnknownError:
            return SyncEngine::Server;
        default:
            return SyncEngine::Offline;
        }
    }
    if (r.status == 401) {
        return SyncEngine::Auth;
    }
    return SyncEngine::Server;
}

SyncEngine::SyncEngine(const QString &dbPath, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
{
}

SyncEngine::~SyncEngine() = default;

void SyncEngine::cancel()
{
    m_cancel.store(true);
}

bool SyncEngine::ensureDb()
{
    if (m_db && m_db->isOpen()) {
        return true;
    }
    m_db = std::make_unique<Database>(m_dbPath);
    QString err;
    if (!m_db->open(&err)) {
        m_result.kind = Internal;
        m_result.message = err;
        m_db.reset();
        return false;
    }
    return true;
}

// Fatal for this run: remember the reason and unwind.
bool SyncEngine::fail(const DavResponse &r, const QString &context)
{
    m_result.kind = classify(r, m_cancel.load());
    m_result.message = QStringLiteral("%1: %2").arg(context, r.describe());
    qCWarning(lcSync) << "sync aborted:" << m_result.message;
    return false;
}

// Failures of a single item must not stop the rest; connectivity problems do.
bool SyncEngine::softFail(const DavResponse &r, const QString &context)
{
    if (r.status == 0 || r.status == 401 || m_cancel.load()) {
        return fail(r, context);
    }
    m_softError = QStringLiteral("%1: %2").arg(context, r.describe());
    qCWarning(lcSync) << m_softError;
    return true;
}

void SyncEngine::touched()
{
    m_result.changedLocal = true;
}

void SyncEngine::runJobs(QueuedJob first)
{
    if (m_jobActive) {
        // CalDavClient::request spins a nested QEventLoop; queue rather than re-enter and overwrite m_client.
        m_jobs.append(std::move(first));
        return;
    }
    m_jobActive = true;
    m_jobs.prepend(std::move(first));
    while (!m_jobs.isEmpty()) {
        const QueuedJob job = m_jobs.takeFirst();
        if (m_cancel.load()) {
            m_jobs.clear();
            break;
        }
        if (job.kind == QueuedJob::Tasks) {
            Q_EMIT syncStarted(job.accountId);
            const Result r = syncAccount(job.accountId, job.password);
            if (r.changedLocal) {
                Q_EMIT dataChanged();
            }
            Q_EMIT syncFinished(job.accountId, r.kind, r.message, r.pulled, r.pushed, r.conflictTitles);
        } else if (job.kind == QueuedJob::Notes) {
            const Result r = syncNotesAccount(job.accountId, job.password);
            if (r.changedLocal) {
                Q_EMIT dataChanged();
            }
            Q_EMIT notesSyncFinished(job.accountId, r.kind, r.message, r.pulled, r.pushed, r.conflictTitles);
        } else if (job.kind == QueuedJob::Routines) {
            const Result r = syncRoutineAccount(job.accountId, job.password);
            if (r.changedLocal) {
                Q_EMIT dataChanged();
            }
            Q_EMIT routineSyncFinished(job.accountId, r.kind, r.message, r.pulled, r.pushed, r.conflictTitles);
        } else {
            const VerifyResult v = verify(job.serverUrl, job.user, job.password);
            Q_EMIT verifyFinished(job.requestId, v.kind, v.message, v.davUrl, v.principalUrl, v.homeUrl, v.taskLists);
        }
    }
    m_jobActive = false;
}

void SyncEngine::startSync(qint64 accountId, const QString &password)
{
    QueuedJob j;
    j.kind = QueuedJob::Tasks;
    j.accountId = accountId;
    j.password = password;
    runJobs(std::move(j));
}

void SyncEngine::startNotesSync(qint64 accountId, const QString &password)
{
    QueuedJob j;
    j.kind = QueuedJob::Notes;
    j.accountId = accountId;
    j.password = password;
    runJobs(std::move(j));
}

void SyncEngine::startRoutineSync(qint64 accountId, const QString &password)
{
    QueuedJob j;
    j.kind = QueuedJob::Routines;
    j.accountId = accountId;
    j.password = password;
    runJobs(std::move(j));
}

void SyncEngine::startVerify(int requestId, const QString &serverUrl, const QString &user, const QString &password)
{
    QueuedJob j;
    j.kind = QueuedJob::Verify;
    j.requestId = requestId;
    j.serverUrl = serverUrl;
    j.user = user;
    j.password = password;
    runJobs(std::move(j));
}

SyncEngine::VerifyResult SyncEngine::verify(const QString &serverUrl, const QString &user, const QString &password)
{
    VerifyResult v;
    m_cancel.store(false);
    CalDavClient client;
    client.setTimeoutMs(m_timeoutMs);
    client.setCancelFlag(&m_cancel);
    client.setCredentials(user, password);
    const auto d = client.discover(QUrl::fromUserInput(serverUrl));
    if (!d.ok) {
        v.kind = classify(d.response, m_cancel.load());
        v.message = d.response.describe();
        return v;
    }
    QList<DavCollection> cols;
    DavResponse r;
    if (!client.listCollections(d.homePath, &cols, &r)) {
        v.kind = classify(r, m_cancel.load());
        v.message = r.describe();
        return v;
    }
    for (const DavCollection &c : std::as_const(cols)) {
        v.taskLists += c.isCalendar && c.supportsTodo;
    }
    v.davUrl = d.davUrl;
    v.principalUrl = d.principalPath;
    v.homeUrl = d.homePath;
    return v;
}

SyncEngine::Result SyncEngine::syncAccount(qint64 accountId, const QString &password)
{
    m_result = Result();
    m_softError.clear();
    m_cancel.store(false);
    if (!ensureDb()) {
        return m_result;
    }
    auto accOpt = m_db->account(accountId);
    if (!accOpt) {
        m_result.kind = Internal;
        m_result.message = QStringLiteral("unknown account");
        return m_result;
    }
    Account acc = *accOpt;

    m_client = std::make_unique<CalDavClient>();
    m_client->setTimeoutMs(m_timeoutMs);
    m_client->setCancelFlag(&m_cancel);
    m_client->setCredentials(acc.username, password);

    auto run = [&]() -> bool {
        if (acc.homeUrl.isEmpty() || acc.davUrl.isEmpty()) {
            const auto d = m_client->discover(QUrl::fromUserInput(acc.davUrl.isEmpty() ? acc.serverUrl : acc.davUrl));
            if (!d.ok) {
                return fail(d.response, QStringLiteral("discovery"));
            }
            acc.davUrl = d.davUrl;
            acc.principalUrl = d.principalPath;
            acc.homeUrl = d.homePath;
            m_db->updateAccount(acc);
        }
        m_client->setBaseUrl(QUrl(acc.davUrl));

        QList<DavCollection> cols;
        DavResponse r;
        if (!m_client->listCollections(acc.homeUrl, &cols, &r)) {
            if (r.status == 404) {
                // The home moved (server reconfigured): discover again next time.
                acc.homeUrl.clear();
                m_db->updateAccount(acc);
            }
            return fail(r, QStringLiteral("listing calendars"));
        }
        if (!syncLists(acc, cols)) {
            return false;
        }
        QHash<QString, DavCollection> byHref;
        for (const DavCollection &c : std::as_const(cols)) {
            byHref.insert(c.href, c);
        }
        const auto lists = m_db->lists();
        for (const TaskList &l : lists) {
            if (l.accountId != acc.id || l.href.isEmpty()) {
                continue;
            }
            if (m_cancel.load()) {
                return fail(DavResponse(), QStringLiteral("sync"));
            }
            if (!syncList(l, byHref.value(l.href))) {
                return false;
            }
        }
        return true;
    };

    const bool ok = run();
    // Re-read: the user may have renamed the account meanwhile.
    if (auto fresh = m_db->account(accountId)) {
        acc = *fresh;
        if (ok) {
            acc.lastSync = QDateTime::currentDateTimeUtc();
            acc.lastError = m_softError;
        } else if (m_result.kind != Cancelled && m_result.kind != Offline) {
            acc.lastError = m_result.message;
        }
        m_db->updateAccount(acc);
    }
    if (ok && !m_softError.isEmpty()) {
        m_result.kind = Server;
        m_result.message = m_softError;
    }
    m_client.reset();
    qCInfo(lcSync) << "sync finished for account" << accountId << "pulled" << m_result.pulled << "pushed" << m_result.pushed << "conflicts"
                   << m_result.conflictTitles.size() << (m_result.ok() ? QString() : m_result.message);
    return m_result;
}

static QString slugify(const QString &name)
{
    QString s = name.normalized(QString::NormalizationForm_D).toLower();
    static const QRegularExpression marks(QStringLiteral("\\p{M}"));
    s.remove(marks);
    static const QRegularExpression other(QStringLiteral("[^a-z0-9]+"));
    s.replace(other, QStringLiteral("-"));
    while (s.startsWith(QLatin1Char('-'))) {
        s.remove(0, 1);
    }
    while (s.endsWith(QLatin1Char('-'))) {
        s.chop(1);
    }
    return s.isEmpty() ? QStringLiteral("tasks") : s.left(40);
}

bool SyncEngine::syncLists(Account &acc, const QList<DavCollection> &remote)
{
    QString home = acc.homeUrl;
    if (!home.endsWith(QLatin1Char('/'))) {
        home += QLatin1Char('/');
    }
    QSet<QString> remoteHrefs;
    for (const DavCollection &c : remote) {
        remoteHrefs.insert(c.href);
    }
    QSet<QString> handled;

    const auto locals = m_db->lists(true);
    for (TaskList l : locals) {
        if (l.accountId != acc.id) {
            continue;
        }
        if (l.syncState == SyncState::Deleted) {
            if (!l.href.isEmpty() && remoteHrefs.contains(l.href)) {
                const DavResponse r = m_client->remove(l.href, QString());
                if (!r.ok() && r.status != 404) {
                    if (r.status == 0 || r.status == 401) {
                        return fail(r, QStringLiteral("deleting list"));
                    }
                    // Not allowed to delete it: bring the list back.
                    m_softError = QStringLiteral("deleting list \"%1\": %2").arg(l.name, r.describe());
                    l.syncState = SyncState::Synced;
                    l.ctag.clear();
                    l.syncToken.clear();
                    m_db->updateListFull(l);
                    touched();
                    continue;
                }
            }
            handled.insert(l.href);
            m_db->purgeList(l.id);
            touched();
            continue;
        }
        if (l.href.isEmpty()) {
            const QString href = home + slugify(l.name) + QLatin1Char('-') + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + QLatin1Char('/');
            const DavResponse r = m_client->mkCalendar(href, l.name, l.color);
            if (!r.ok()) {
                if (!softFail(r, QStringLiteral("creating list \"%1\"").arg(l.name))) {
                    return false;
                }
                continue;
            }
            l.href = href;
            l.syncState = SyncState::Synced;
            m_db->updateListFull(l);
            handled.insert(href);
            touched();
            continue;
        }
        if (!remoteHrefs.contains(l.href)) {
            // Deleted on the server.
            qCInfo(lcSync) << "list removed remotely:" << l.name;
            m_db->purgeList(l.id);
            touched();
            continue;
        }
        if (l.syncState == SyncState::Modified || l.syncState == SyncState::New) {
            const DavResponse r = m_client->propPatch(l.href, l.name, l.color);
            if (!r.ok() && !softFail(r, QStringLiteral("renaming list \"%1\"").arg(l.name))) {
                return false;
            }
            // Re-read so a rename made during the request is not lost.
            if (auto cur = m_db->list(l.id); cur && r.ok() && cur->name == l.name && cur->color == l.color) {
                cur->syncState = SyncState::Synced;
                m_db->updateListFull(*cur);
            }
            handled.insert(l.href);
            continue;
        }
    }

    QHash<QString, TaskList> byHref;
    for (const TaskList &l : m_db->lists(true)) {
        if (l.accountId == acc.id) {
            byHref.insert(l.href, l);
        }
    }
    double order = 0;
    for (const DavCollection &c : remote) {
        ++order;
        if (!c.isCalendar || !c.supportsTodo || handled.contains(c.href)) {
            continue;
        }
        auto it = byHref.find(c.href);
        if (it == byHref.end()) {
            TaskList l;
            l.accountId = acc.id;
            l.name = c.name;
            l.color = c.color;
            l.href = c.href;
            l.readOnly = c.readOnly;
            l.sortOrder = order;
            l.syncState = SyncState::Synced;
            m_db->insertList(l);
            touched();
        } else if (it->syncState == SyncState::Synced && (it->name != c.name || it->color != c.color || it->readOnly != c.readOnly)) {
            it->name = c.name;
            it->color = c.color;
            it->readOnly = c.readOnly;
            m_db->updateListFull(*it);
            touched();
        }
    }
    return true;
}

bool SyncEngine::fetchAndApply(const TaskList &list, const QList<DavItem> &changed)
{
    QStringList toFetch;
    for (const DavItem &it : changed) {
        const auto local = m_db->taskByHref(list.id, it.href);
        if (!local || local->etag != it.etag() || it.etag().isEmpty()) {
            toFetch << it.href;
        }
    }
    for (int i = 0; i < toFetch.size(); i += 50) {
        QList<DavItem> items;
        DavResponse r;
        if (!m_client->multiget(list.href, toFetch.mid(i, 50), &items, &r)) {
            return fail(r, QStringLiteral("downloading tasks"));
        }
        for (const DavItem &it : std::as_const(items)) {
            const QString ics = it.props.value(QStringLiteral("calendar-data"));
            if (it.status == 404) {
                applyRemoteDelete(list, it.href);
            } else if (!ics.isEmpty()) {
                applyRemote(list, it.href, it.etag(), ics);
            }
        }
    }
    return true;
}

bool SyncEngine::pullFull(const TaskList &list)
{
    QList<DavItem> items;
    DavResponse r;
    if (!m_client->listTodos(list.href, &items, &r)) {
        return fail(r, QStringLiteral("listing tasks"));
    }
    QSet<QString> present;
    for (const DavItem &it : std::as_const(items)) {
        present.insert(it.href);
    }
    if (!fetchAndApply(list, items)) {
        return false;
    }
    // Whatever we once uploaded or downloaded and is no longer there was deleted remotely.
    for (const Task &t : m_db->tasksInList(list.id)) {
        if (!t.href.isEmpty() && !present.contains(t.href)) {
            applyRemoteDelete(list, t.href);
        }
    }
    return true;
}

bool SyncEngine::syncList(TaskList list, const DavCollection &col)
{
    bool pending = false;
    for (const Task &t : m_db->tasksInList(list.id)) {
        if (t.syncState != SyncState::Synced) {
            pending = true;
            break;
        }
    }
    const bool remoteUnchanged = !col.ctag.isEmpty() && col.ctag == list.ctag;
    if (!pending && remoteUnchanged) {
        return true;
    }

    QString newToken = list.syncToken;
    if (!remoteUnchanged) {
        bool full = list.syncToken.isEmpty();
        if (!full) {
            const auto rep = m_client->syncCollection(list.href, list.syncToken);
            if (rep.ok) {
                if (!fetchAndApply(list, rep.changed)) {
                    return false;
                }
                for (const QString &href : rep.deleted) {
                    applyRemoteDelete(list, href);
                }
                newToken = rep.newToken;
            } else if (rep.response.status == 0 || rep.response.status == 401) {
                return fail(rep.response, QStringLiteral("checking for changes"));
            } else {
                // Stale token or no sync-collection support: compare everything.
                qCInfo(lcSync) << "falling back to a full comparison for" << list.name << rep.response.describe();
                full = true;
            }
        }
        if (full) {
            if (!pullFull(list)) {
                return false;
            }
            newToken = col.syncToken;
        }
    }

    if (!list.readOnly) {
        for (const Task &t : m_db->tasksInList(list.id)) {
            if (t.syncState == SyncState::Synced) {
                continue;
            }
            if (m_cancel.load()) {
                return fail(DavResponse(), QStringLiteral("sync"));
            }
            if (!pushTask(list, t)) {
                return false;
            }
        }
    }

    // Only the sync columns: the user may have renamed the list meanwhile.
    if (auto cur = m_db->list(list.id)) {
        cur->syncToken = newToken;
        cur->ctag = col.ctag;
        m_db->updateListFull(*cur);
    }
    return true;
}

static void copyContent(Task &dst, const Task &src)
{
    dst.uid = src.uid;
    dst.title = src.title;
    dst.notes = src.notes;
    dst.priority = src.priority;
    dst.quadrant = src.quadrant;
    dst.estimatedMinutes = src.estimatedMinutes;
    dst.trackedSeconds = src.trackedSeconds;
    dst.pomodorosCompleted = src.pomodorosCompleted;
    dst.start = src.start;
    dst.due = src.due;
    dst.allDay = src.allDay;
    dst.completedAt = src.completedAt;
    dst.status = src.status;
    dst.percent = src.percent;
    dst.tags = src.tags;
    dst.rrule = src.rrule;
    dst.alarms = src.alarms;
    dst.parentUid = src.parentUid;
    dst.sortOrder = src.sortOrder;
    dst.created = src.created;
    dst.modified = src.modified;
}

void SyncEngine::applyRemote(const TaskList &list, const QString &href, const QString &etag, const QString &ics)
{
    const auto remoteOpt = IcsCodec::decode(ics);
    if (!remoteOpt) {
        return; // an event or something else we do not handle
    }
    const Task &remote = *remoteOpt;

    Database::Transaction tx(m_db.get());
    auto localOpt = m_db->taskByHref(list.id, href);
    if (!localOpt) {
        // A task of ours whose upload succeeded but was never recorded.
        localOpt = m_db->taskByUid(list.id, remote.uid);
        if (localOpt && !localOpt->href.isEmpty() && localOpt->href != href) {
            localOpt.reset();
        }
    }
    if (!localOpt) {
        Task t = remote;
        t.id = 0;
        t.listId = list.id;
        t.href = href;
        t.etag = etag;
        t.baseIcs = ics;
        t.syncState = SyncState::Synced;
        m_db->insertTask(t);
        ++m_result.pulled;
        touched();
        tx.commit();
        return;
    }

    Task local = *localOpt;
    auto adoptRemote = [&](Task &t) {
        copyContent(t, remote);
        t.href = href;
        t.etag = etag;
        t.baseIcs = ics;
        t.syncState = SyncState::Synced;
    };

    if (local.syncState == SyncState::Synced || local.syncState == SyncState::Deleted) {
        // No local edits, or a local delete that lost against a remote edit.
        adoptRemote(local);
        m_db->updateTaskFull(local);
        ++m_result.pulled;
    } else if (sameContent(local, remote)) {
        adoptRemote(local);
        m_db->updateTaskFull(local);
    } else {
        const auto baseOpt = local.baseIcs.isEmpty() ? std::nullopt : IcsCodec::decode(local.baseIcs);
        MergeResult res;
        if (baseOpt) {
            res = mergeTasks(*baseOpt, local, remote);
        } else {
            res.conflict = true; // no common ancestor to merge from
        }
        if (res.conflict) {
            Task copy = local;
            copy.id = 0;
            copy.uid = newUid();
            copy.title = QStringLiteral("%1 (%2)").arg(local.title, m_conflictSuffix);
            copy.href.clear();
            copy.etag.clear();
            copy.baseIcs.clear();
            copy.syncState = SyncState::New;
            copy.rev = 0;
            copy.created = QDateTime::currentDateTimeUtc();
            m_db->insertTask(copy);
            m_result.conflictTitles << local.title;
            qCInfo(lcSync) << "conflict: kept both versions of a task; fields:" << res.conflictFields;
            adoptRemote(local);
            m_db->updateTaskFull(local);
        } else {
            copyContent(local, res.merged);
            local.href = href;
            local.etag = etag;
            local.baseIcs = ics;
            local.syncState = res.localChangesKept ? SyncState::Modified : SyncState::Synced;
            m_db->updateTaskFull(local);
        }
        ++m_result.pulled;
    }
    touched();
    tx.commit();
}

void SyncEngine::applyRemoteDelete(const TaskList &list, const QString &href)
{
    Database::Transaction tx(m_db.get());
    auto local = m_db->taskByHref(list.id, href);
    if (!local) {
        return;
    }
    if (local->syncState == SyncState::Modified) {
        // Edited here, deleted there: the edit wins and the task is uploaded again.
        local->href.clear();
        local->etag.clear();
        local->baseIcs.clear();
        local->syncState = SyncState::New;
        m_db->updateTaskFull(*local);
    } else {
        m_db->purgeTask(local->id);
        ++m_result.pulled;
    }
    touched();
    tx.commit();
}

static QString fileNameFor(const QString &uid)
{
    QString name = uid;
    static const QRegularExpression unsafe(QStringLiteral("[^A-Za-z0-9._@-]"));
    name.replace(unsafe, QStringLiteral("_"));
    return name + QStringLiteral(".ics");
}

bool SyncEngine::pushTask(const TaskList &list, Task task, int depth)
{
    if (task.syncState == SyncState::Deleted) {
        if (task.href.isEmpty()) {
            m_db->purgeTask(task.id);
            return true;
        }
        const DavResponse r = m_client->remove(task.href, task.etag);
        if (r.ok() || r.status == 404) {
            m_db->purgeTask(task.id);
            ++m_result.pushed;
            return true;
        }
        if (r.status == 412) {
            // Changed on the server after we last saw it: the remote edit wins.
            const DavResponse g = m_client->get(task.href);
            if (g.status == 404) {
                m_db->purgeTask(task.id);
            } else if (g.ok()) {
                applyRemote(list, task.href, g.etag.isEmpty() ? m_client->etagOf(task.href) : g.etag, QString::fromUtf8(g.body));
            } else {
                return softFail(g, QStringLiteral("deleting \"%1\"").arg(task.title));
            }
            return true;
        }
        return softFail(r, QStringLiteral("deleting \"%1\"").arg(task.title));
    }

    const QString ics = IcsCodec::encode(task);
    const bool create = task.href.isEmpty();
    const QString href = create ? list.href + fileNameFor(task.uid) : task.href;
    const DavResponse r = m_client->put(href, ics, task.etag, create);

    if (r.ok()) {
        QString etag = r.etag;
        if (etag.isEmpty()) {
            etag = m_client->etagOf(href);
        }
        m_db->markUploaded(task.id, task.rev, href, etag, ics);
        ++m_result.pushed;
        touched();
        return true;
    }
    if ((r.status == 412 || r.status == 404 || (create && r.status == 405)) && depth < 2) {
        const DavResponse g = m_client->get(href);
        if (g.status == 404) {
            if (create) {
                return softFail(r, QStringLiteral("uploading \"%1\"").arg(task.title));
            }
            // Gone on the server while edited here: upload it as a new task.
            task.href.clear();
            task.etag.clear();
            task.baseIcs.clear();
            task.syncState = SyncState::New;
            m_db->updateTaskFull(task);
            return pushTask(list, task, depth + 1);
        }
        if (!g.ok()) {
            return softFail(g, QStringLiteral("uploading \"%1\"").arg(task.title));
        }
        // Someone else changed it: merge, then upload what is left of our changes.
        applyRemote(list, href, g.etag.isEmpty() ? m_client->etagOf(href) : g.etag, QString::fromUtf8(g.body));
        const auto cur = m_db->task(task.id);
        if (cur && cur->syncState == SyncState::Modified) {
            return pushTask(list, *cur, depth + 1);
        }
        return true;
    }
    return softFail(r, QStringLiteral("uploading \"%1\"").arg(task.title));
}

// ---- Nextcloud Notes REST API v1 ----

static QString stripEtagQuotes(QString s)
{
    s = s.trimmed();
    if (s.startsWith(QLatin1Char('"'))) {
        s.remove(0, 1);
    }
    if (s.endsWith(QLatin1Char('"'))) {
        s.chop(1);
    }
    return s;
}

static QUrl nextcloudNotesUrl(const Account &acc, const QString &suffix = QString())
{
    const QString raw = !acc.serverUrl.isEmpty() ? acc.serverUrl : acc.davUrl;
    QUrl u = QUrl::fromUserInput(raw.trimmed());
    if (!u.isValid() || u.host().isEmpty()) {
        return {};
    }
    QString p = u.path();
    for (const char *marker : {"/remote.php", "/index.php", "/apps/"}) {
        const int idx = p.indexOf(QLatin1String(marker));
        if (idx >= 0) {
            p.truncate(idx);
        }
    }
    while (p.endsWith(QLatin1Char('/'))) {
        p.chop(1);
    }
    u.setPath(p + QStringLiteral("/index.php/apps/notes/api/v1/notes") + suffix);
    u.setQuery(QString());
    u.setFragment(QString());
    return u;
}

static QString notesTitleFallback(const QString &title, const QString &content)
{
    if (!title.trimmed().isEmpty()) {
        return title.trimmed();
    }
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        while (line.startsWith(QLatin1Char('#'))) {
            line.remove(0, 1);
        }
        line = line.trimmed();
        if (!line.isEmpty()) {
            return line.left(80);
        }
    }
    return QStringLiteral("Nota");
}

static QString notebookCategoryName(const QList<JournalNotebook> &notebooks, qint64 notebookId)
{
    for (const JournalNotebook &nb : notebooks) {
        if (nb.id == notebookId) {
            if (nb.id == 1 && (nb.name == QLatin1String("Personal") || nb.name == QLatin1String("Pessoal"))) {
                return QString();
            }
            return nb.name;
        }
    }
    return QString();
}

static qint64 resolveCategoryNotebook(Database *db, qint64 accountId, const QString &category)
{
    auto nbs = db->journalNotebooks();
    if (nbs.isEmpty()) {
        JournalNotebook nb;
        nb.name = QStringLiteral("Personal");
        nb.color = QStringLiteral("#3daee9");
        db->insertJournalNotebook(nb);
        nbs = db->journalNotebooks();
    }
    const QString cat = category.trimmed();
    if (cat.isEmpty()) {
        return nbs.first().id;
    }
    qint64 anyMatch = 0;
    for (const JournalNotebook &nb : nbs) {
        if (nb.name.compare(cat, Qt::CaseInsensitive) == 0) {
            if (nb.syncAccountId == accountId) {
                return nb.id;
            }
            if (anyMatch == 0) {
                anyMatch = nb.id;
            }
        }
    }
    if (anyMatch > 0) {
        return anyMatch;
    }
    JournalNotebook created;
    created.name = cat;
    created.syncAccountId = accountId;
    created.color = QStringLiteral("#3daee9");
    if (db->insertJournalNotebook(created)) {
        return created.id;
    }
    return nbs.first().id;
}

SyncEngine::Result SyncEngine::syncNotesAccount(qint64 accountId, const QString &password)
{
    m_result = Result();
    m_softError.clear();
    m_cancel.store(false);
    if (!ensureDb()) {
        return m_result;
    }
    const auto accOpt = m_db->account(accountId);
    if (!accOpt) {
        m_result.kind = Internal;
        m_result.message = QStringLiteral("unknown account");
        return m_result;
    }
    m_client = std::make_unique<CalDavClient>();
    m_client->setTimeoutMs(m_timeoutMs);
    m_client->setCancelFlag(&m_cancel);
    m_client->setCredentials(accOpt->username, password);
    m_client->setBaseUrl(QUrl::fromUserInput(accOpt->serverUrl));

    syncNextcloudNotes(*accOpt);
    if (m_result.ok() && !m_softError.isEmpty()) {
        m_result.kind = Server;
        m_result.message = m_softError;
    }
    m_client.reset();
    return m_result;
}

bool SyncEngine::syncNextcloudNotes(const Account &acc)
{
    const QUrl listUrl = nextcloudNotesUrl(acc);
    if (!listUrl.isValid()) {
        return true;
    }
    const QList<QPair<QByteArray, QByteArray>> commonHeaders = {
        {"OCS-APIRequest", "true"},
        {"Accept", "application/json"},
    };
    const QString etagKey = QStringLiteral("notes_list_etag_%1").arg(acc.id);
    bool mutatedServer = false;

    // 1. Delete notes that were removed or switched to offline-only locally.
    const QList<qint64> deletes = m_db->journalRemoteDeletes(acc.id);
    for (qint64 rid : deletes) {
        if (m_cancel.load()) {
            return fail(DavResponse(), QStringLiteral("syncing notes"));
        }
        const DavResponse dr = m_client->request(QByteArrayLiteral("DELETE"), nextcloudNotesUrl(acc, QStringLiteral("/%1").arg(rid)), {}, commonHeaders);
        if (dr.ok() || dr.status == 404) {
            m_db->clearJournalRemoteDelete(acc.id, rid);
            ++m_result.pushed;
            mutatedServer = true;
        } else if (dr.status == 405 || dr.status == 501) {
            return true;
        } else if (!softFail(dr, QStringLiteral("deleting remote note %1").arg(rid))) {
            return false;
        }
    }
    if (mutatedServer) {
        m_db->setMeta(etagKey, QString());
    }

    // 2. Pull remote notes list (using ETag if cached).
    QList<QPair<QByteArray, QByteArray>> getHeaders = commonHeaders;
    const QString savedEtag = m_db->meta(etagKey);
    if (!savedEtag.isEmpty()) {
        getHeaders.append(qMakePair(QByteArrayLiteral("If-None-Match"), QStringLiteral("\"%1\"").arg(savedEtag).toUtf8()));
    }
    const DavResponse gr = m_client->request(QByteArrayLiteral("GET"), listUrl, {}, getHeaders);
    if (gr.status == 404 || gr.status == 405 || gr.status == 501) {
        // Server does not have Nextcloud Notes enabled; treat silently as unavailable.
        return true;
    }
    if (gr.status == 0 || gr.status == 401) {
        return fail(gr, QStringLiteral("listing Nextcloud notes"));
    }

    QSet<qint64> pendingDeletes;
    for (qint64 rid : m_db->journalRemoteDeletes(acc.id)) {
        pendingDeletes.insert(rid);
    }

    if (gr.status == 200) {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(gr.body, &perr);
        if (perr.error == QJsonParseError::NoError && doc.isArray()) {
            const QJsonArray arr = doc.array();
            QSet<qint64> seenRemoteIds;
            for (const QJsonValue &val : arr) {
                const QJsonObject obj = val.toObject();
                const qint64 rid = obj.value(QStringLiteral("id")).toInteger();
                if (rid <= 0 || pendingDeletes.contains(rid)) {
                    continue;
                }
                seenRemoteIds.insert(rid);
                const QString rContent = obj.value(QStringLiteral("content")).toString();
                const QString rTitle = notesTitleFallback(obj.value(QStringLiteral("title")).toString(), rContent);
                const QString rCategory = obj.value(QStringLiteral("category")).toString().trimmed();
                const qint64 rMod = obj.value(QStringLiteral("modified")).toInteger();
                const QString rEtag = stripEtagQuotes(obj.value(QStringLiteral("etag")).toString());
                const qint64 nbId = resolveCategoryNotebook(m_db.get(), acc.id, rCategory);
                const QDateTime modDt = rMod > 0 ? QDateTime::fromSecsSinceEpoch(rMod, QTimeZone::UTC) : QDateTime::currentDateTimeUtc();

                auto localOpt = m_db->journalEntryByRemote(acc.id, rid);
                if (!localOpt) {
                    JournalEntry e;
                    e.notebookId = nbId;
                    e.title = rTitle;
                    e.content = rContent;
                    e.created = modDt;
                    e.modified = modDt;
                    e.entryDate = modDt.toLocalTime().date();
                    e.syncAccountId = acc.id;
                    e.remoteId = rid;
                    e.etag = rEtag;
                    e.remoteModified = rMod;
                    e.syncState = SyncState::Synced;
                    if (m_db->insertJournalEntry(e)) {
                        ++m_result.pulled;
                        touched();
                    }
                    continue;
                }

                JournalEntry local = *localOpt;
                if (local.isLocked) {
                    continue;
                }
                const bool remoteChanged = (!rEtag.isEmpty() && rEtag != local.etag)
                    || (rMod > 0 && rMod != local.remoteModified)
                    || rTitle != local.title || rContent != local.content || nbId != local.notebookId;
                if (!remoteChanged) {
                    continue;
                }

                if (local.syncState == SyncState::Synced) {
                    local.notebookId = nbId;
                    local.title = rTitle;
                    local.content = rContent;
                    local.modified = modDt;
                    local.etag = rEtag;
                    local.remoteModified = rMod;
                    local.syncState = SyncState::Synced;
                    if (m_db->updateJournalEntry(local)) {
                        ++m_result.pulled;
                        touched();
                    }
                } else {
                    const bool etagOrTimeChanged = (!rEtag.isEmpty() && !local.etag.isEmpty() && rEtag != local.etag)
                        || (rMod > 0 && local.remoteModified > 0 && rMod != local.remoteModified);
                    if (etagOrTimeChanged) {
                        // Conflict: preserve local edits in an offline local copy and take server latest.
                        JournalEntry backup = local;
                        backup.id = 0;
                        backup.uid.clear();
                        backup.title = QStringLiteral("%1 (%2)").arg(local.title, m_conflictSuffix);
                        backup.syncAccountId = 0;
                        backup.remoteId = 0;
                        backup.etag.clear();
                        backup.remoteModified = 0;
                        backup.syncState = SyncState::Synced;
                        m_db->insertJournalEntry(backup);

                        local.notebookId = nbId;
                        local.title = rTitle;
                        local.content = rContent;
                        local.modified = modDt;
                        local.etag = rEtag;
                        local.remoteModified = rMod;
                        local.syncState = SyncState::Synced;
                        m_db->updateJournalEntry(local);
                        m_result.conflictTitles << local.title;
                        ++m_result.pulled;
                        touched();
                    }
                }
            }

            // Delete local synced notes that were removed on server (never touch offline local notes).
            const auto allLocal = m_db->journalEntries(false);
            for (const JournalEntry &e : allLocal) {
                if (e.syncAccountId == acc.id && e.remoteId > 0 && e.syncState == SyncState::Synced && !e.isLocked
                    && !seenRemoteIds.contains(e.remoteId)) {
                    m_db->removeJournalEntry(e.id);
                    ++m_result.pulled;
                    touched();
                }
            }

            const QString newListEtag = stripEtagQuotes(gr.etag);
            if (!newListEtag.isEmpty()) {
                m_db->setMeta(etagKey, newListEtag);
            }
        }
    } else if (gr.status != 304) {
        if (!softFail(gr, QStringLiteral("listing Nextcloud notes"))) {
            return false;
        }
    }

    // 3. Push local new/modified unlocked synced entries to Nextcloud Notes.
    const auto notebooks = m_db->journalNotebooks();
    const auto entries = m_db->journalEntries(false);
    QList<QPair<QByteArray, QByteArray>> jsonHeaders = commonHeaders;
    jsonHeaders.append(qMakePair(QByteArrayLiteral("Content-Type"), QByteArrayLiteral("application/json; charset=utf-8")));

    for (JournalEntry e : entries) {
        if (m_cancel.load()) {
            return fail(DavResponse(), QStringLiteral("uploading notes"));
        }
        if (e.syncAccountId != acc.id || e.isLocked || e.syncState == SyncState::Synced) {
            continue;
        }
        const QString category = notebookCategoryName(notebooks, e.notebookId);
        const qint64 modSecs = e.modified.isValid() ? e.modified.toSecsSinceEpoch() : QDateTime::currentSecsSinceEpoch();
        QJsonObject payload;
        payload.insert(QStringLiteral("title"), e.title);
        payload.insert(QStringLiteral("content"), e.content);
        payload.insert(QStringLiteral("category"), category);
        payload.insert(QStringLiteral("modified"), modSecs);
        const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);

        DavResponse pr;
        if (e.remoteId <= 0 || e.syncState == SyncState::New) {
            pr = m_client->request(QByteArrayLiteral("POST"), listUrl, body, jsonHeaders);
        } else {
            QList<QPair<QByteArray, QByteArray>> putHeaders = jsonHeaders;
            if (!e.etag.isEmpty()) {
                putHeaders.append(qMakePair(QByteArrayLiteral("If-Match"), QStringLiteral("\"%1\"").arg(e.etag).toUtf8()));
            }
            pr = m_client->request(QByteArrayLiteral("PUT"), nextcloudNotesUrl(acc, QStringLiteral("/%1").arg(e.remoteId)), body, putHeaders);
            if (pr.status == 404) {
                pr = m_client->request(QByteArrayLiteral("POST"), listUrl, body, jsonHeaders);
            }
        }

        if (pr.ok()) {
            const QJsonObject robj = QJsonDocument::fromJson(pr.body).object();
            const qint64 rid = robj.value(QStringLiteral("id")).toInteger();
            if (rid > 0) {
                e.remoteId = rid;
            }
            const QString newEtag = stripEtagQuotes(robj.value(QStringLiteral("etag")).toString());
            if (!newEtag.isEmpty()) {
                e.etag = newEtag;
            } else if (!pr.etag.isEmpty()) {
                e.etag = stripEtagQuotes(pr.etag);
            }
            const qint64 srvMod = robj.value(QStringLiteral("modified")).toInteger();
            e.remoteModified = srvMod > 0 ? srvMod : modSecs;
            e.syncState = SyncState::Synced;
            m_db->updateJournalEntry(e);
            ++m_result.pushed;
            touched();
            m_db->setMeta(etagKey, QString());
        } else if (pr.status == 412 && e.remoteId > 0) {
            const DavResponse single = m_client->request(QByteArrayLiteral("GET"), nextcloudNotesUrl(acc, QStringLiteral("/%1").arg(e.remoteId)), {}, commonHeaders);
            if (single.ok()) {
                const QJsonObject sobj = QJsonDocument::fromJson(single.body).object();
                JournalEntry backup = e;
                backup.id = 0;
                backup.uid.clear();
                backup.title = QStringLiteral("%1 (%2)").arg(e.title, m_conflictSuffix);
                backup.syncAccountId = 0;
                backup.remoteId = 0;
                backup.etag.clear();
                backup.remoteModified = 0;
                backup.syncState = SyncState::Synced;
                m_db->insertJournalEntry(backup);

                e.content = sobj.value(QStringLiteral("content")).toString();
                e.title = notesTitleFallback(sobj.value(QStringLiteral("title")).toString(), e.content);
                e.etag = stripEtagQuotes(sobj.value(QStringLiteral("etag")).toString());
                e.remoteModified = sobj.value(QStringLiteral("modified")).toInteger();
                e.syncState = SyncState::Synced;
                m_db->updateJournalEntry(e);
                m_result.conflictTitles << e.title;
                touched();
                m_db->setMeta(etagKey, QString());
            }
        } else if (pr.status == 405 || pr.status == 501) {
            return true;
        } else if (!softFail(pr, QStringLiteral("uploading note \"%1\"").arg(e.title))) {
            return false;
        }
    }
    return true;
}

// ---- CalDAV VEVENT Routine Sync ----

static void mergeRemoteRoutineLogs(Database *db, qint64 blockId, const QList<RoutineLog> &logs)
{
    for (const RoutineLog &l : logs) {
        if (l.logDate.isValid() && l.status != RoutineLogStatus::Pending) {
            db->setRoutineLog(blockId, l.logDate, l.status);
        }
    }
}

static void applyRemoteRoutineBlock(RoutineBlock &dst, const RoutineBlock &src,
                                    const QString &calHref, const QString &href,
                                    const QString &etag, const QString &ics)
{
    dst.title = src.title;
    dst.notes = src.notes;
    dst.startMinute = src.startMinute;
    dst.durationMinutes = src.durationMinutes;
    dst.daysMask = src.daysMask;
    dst.specificDate = src.specificDate;
    dst.color = src.color;
    dst.category = src.category;
    dst.notifyEnabled = src.notifyEnabled;
    dst.notifyMinutesBefore = src.notifyMinutesBefore;
    dst.active = src.active;
    dst.calendarHref = calHref;
    dst.href = href;
    dst.etag = etag;
    dst.baseIcs = ics;
    dst.syncState = SyncState::Synced;
}

SyncEngine::Result SyncEngine::syncRoutineAccount(qint64 accountId, const QString &password)
{
    m_result = Result();
    m_softError.clear();
    m_cancel.store(false);
    if (!ensureDb()) {
        return m_result;
    }
    const auto accOpt = m_db->account(accountId);
    if (!accOpt) {
        m_result.kind = Internal;
        m_result.message = QStringLiteral("unknown account");
        return m_result;
    }
    Account acc = *accOpt;
    m_client = std::make_unique<CalDavClient>();
    m_client->setTimeoutMs(m_timeoutMs);
    m_client->setCancelFlag(&m_cancel);
    m_client->setCredentials(acc.username, password);

    syncRoutineEvents(acc);
    if (m_result.ok() && !m_softError.isEmpty()) {
        m_result.kind = Server;
        m_result.message = m_softError;
    }
    m_client.reset();
    return m_result;
}

bool SyncEngine::syncRoutineEvents(Account &acc)
{
    if (acc.homeUrl.isEmpty() || acc.davUrl.isEmpty()) {
        const auto d = m_client->discover(QUrl::fromUserInput(acc.davUrl.isEmpty() ? acc.serverUrl : acc.davUrl));
        if (!d.ok) {
            return fail(d.response, QStringLiteral("discovery"));
        }
        acc.davUrl = d.davUrl;
        acc.principalUrl = d.principalPath;
        acc.homeUrl = d.homePath;
        m_db->updateAccount(acc);
    }
    m_client->setBaseUrl(QUrl(acc.davUrl));

    QList<DavCollection> cols;
    DavResponse lr;
    if (!m_client->listCollections(acc.homeUrl, &cols, &lr)) {
        return fail(lr, QStringLiteral("listing calendars"));
    }

    QList<DavCollection> eventCals;
    QJsonArray calMeta;
    for (const DavCollection &c : cols) {
        if (c.isCalendar && c.supportsEvent && !c.readOnly && !c.href.endsWith(QLatin1String("/inbox/")) && !c.href.endsWith(QLatin1String("/outbox/"))) {
            eventCals.append(c);
            QJsonObject o;
            o.insert(QStringLiteral("href"), c.href);
            o.insert(QStringLiteral("name"), c.name);
            o.insert(QStringLiteral("color"), c.color);
            calMeta.append(o);
        }
    }
    m_db->setMeta(QStringLiteral("routine_cals_%1").arg(acc.id), QString::fromUtf8(QJsonDocument(calMeta).toJson(QJsonDocument::Compact)));

    const QString prefHref = m_db->meta(QStringLiteral("routine_cal_pref_%1").arg(acc.id));
    DavCollection targetCol;
    bool found = false;
    if (!prefHref.isEmpty()) {
        for (const DavCollection &c : eventCals) {
            if (c.href == prefHref) {
                targetCol = c;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        for (const DavCollection &c : eventCals) {
            if (c.name.compare(QLatin1String("Rotina"), Qt::CaseInsensitive) == 0 || c.name.compare(QLatin1String("Routine"), Qt::CaseInsensitive) == 0) {
                targetCol = c;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        QString home = acc.homeUrl;
        if (!home.endsWith(QLatin1Char('/'))) {
            home += QLatin1Char('/');
        }
        const QString newHref = home + QStringLiteral("rotina/");
        const DavResponse mr = m_client->mkEventCalendar(newHref, QStringLiteral("Rotina"), QStringLiteral("#3daee9"));
        if (mr.ok() || mr.status == 201) {
            targetCol.href = newHref;
            targetCol.name = QStringLiteral("Rotina");
            targetCol.color = QStringLiteral("#3daee9");
            targetCol.isCalendar = true;
            targetCol.supportsEvent = true;
            found = true;
            QJsonObject o;
            o.insert(QStringLiteral("href"), targetCol.href);
            o.insert(QStringLiteral("name"), targetCol.name);
            o.insert(QStringLiteral("color"), targetCol.color);
            calMeta.append(o);
            m_db->setMeta(QStringLiteral("routine_cals_%1").arg(acc.id), QString::fromUtf8(QJsonDocument(calMeta).toJson(QJsonDocument::Compact)));
        } else if (!eventCals.isEmpty()) {
            // Fallback to existing default writable VEVENT calendar if server forbids MKCALENDAR
            targetCol = eventCals.first();
            found = true;
        }
    }
    if (!found) {
        return true;
    }

    const QString calHref = targetCol.href;
    const QString calKey = QStringLiteral("routine_cal_%1").arg(acc.id);
    const QString ctagKey = QStringLiteral("routine_ctag_%1").arg(acc.id);
    const QString prevCalHref = m_db->meta(calKey);
    m_db->setMeta(calKey, calHref);
    if (prevCalHref != calHref) {
        m_db->setMeta(ctagKey, QString());
    }

    // 1. Delete events from blocks moved to offline-only or deleted locally.
    const auto deletes = m_db->routineRemoteDeletes(acc.id);
    for (const auto &pair : deletes) {
        if (m_cancel.load()) {
            return fail(DavResponse(), QStringLiteral("deleting routine events"));
        }
        const DavResponse dr = m_client->remove(pair.first, pair.second);
        if (dr.ok() || dr.status == 404 || dr.status == 412) {
            m_db->clearRoutineRemoteDelete(acc.id, pair.first);
            ++m_result.pushed;
        } else if (!softFail(dr, QStringLiteral("deleting remote routine event"))) {
            return false;
        }
    }

    // Also process tombstones in routine_blocks (deleted=1, sync_state=3)
    for (const RoutineBlock &b : m_db->routineBlocks(true, true)) {
        if (b.syncAccountId != acc.id || (!b.deleted && b.syncState != SyncState::Deleted)) {
            continue;
        }
        if (b.href.isEmpty()) {
            m_db->removeRoutineBlock(b.id);
            continue;
        }
        const DavResponse dr = m_client->remove(b.href, b.etag);
        if (dr.ok() || dr.status == 404) {
            m_db->removeRoutineBlock(b.id);
            ++m_result.pushed;
            touched();
        } else if (!softFail(dr, QStringLiteral("deleting routine \"%1\"").arg(b.title))) {
            return false;
        }
    }

    QSet<QString> suppressedHrefs;
    for (const auto &pair : m_db->routineRemoteDeletes(acc.id)) {
        suppressedHrefs.insert(pair.first);
    }

    // 2. Pull changes if ctag changed (or initial sync for this calendar).
    const QString savedCtag = m_db->meta(ctagKey);
    const bool needPull = savedCtag.isEmpty() || targetCol.ctag.isEmpty() || savedCtag != targetCol.ctag;
    if (needPull) {
        QList<DavItem> eventsList;
        DavResponse qr;
        if (!m_client->listEvents(calHref, &eventsList, &qr)) {
            if (!softFail(qr, QStringLiteral("listing routine events"))) {
                return false;
            }
        } else {
            QSet<QString> seenHrefs;
            QStringList toFetch;
            for (const DavItem &it : eventsList) {
                if (suppressedHrefs.contains(it.href)) {
                    continue;
                }
                seenHrefs.insert(it.href);
                const auto local = m_db->routineBlockByHref(acc.id, it.href);
                if (!local || local->etag != it.etag() || it.etag().isEmpty()) {
                    toFetch << it.href;
                }
            }
            for (int i = 0; i < toFetch.size(); i += 50) {
                QList<DavItem> fetched;
                DavResponse mr;
                if (!m_client->multiget(calHref, toFetch.mid(i, 50), &fetched, &mr)) {
                    return fail(mr, QStringLiteral("downloading routine events"));
                }
                for (const DavItem &it : std::as_const(fetched)) {
                    const QString ics = it.props.value(QStringLiteral("calendar-data"));
                    const auto dec = IcsCodec::decodeEvent(ics);
                    if (!dec) {
                        continue;
                    }
                    auto local = m_db->routineBlockByHref(acc.id, it.href);
                    if (!local && !dec->block.uid.isEmpty()) {
                        local = m_db->routineBlockByUid(acc.id, dec->block.uid);
                    }
                    if (!local) {
                        RoutineBlock nb = dec->block;
                        nb.syncAccountId = acc.id;
                        nb.calendarHref = calHref;
                        nb.href = it.href;
                        nb.etag = it.etag();
                        nb.baseIcs = ics;
                        nb.syncState = SyncState::Synced;
                        if (m_db->insertRoutineBlock(nb)) {
                            mergeRemoteRoutineLogs(m_db.get(), nb.id, dec->logs);
                            ++m_result.pulled;
                            touched();
                        }
                    } else if (local->syncState == SyncState::Synced) {
                        RoutineBlock upd = *local;
                        applyRemoteRoutineBlock(upd, dec->block, calHref, it.href, it.etag(), ics);
                        m_db->updateRoutineBlockFull(upd);
                        mergeRemoteRoutineLogs(m_db.get(), upd.id, dec->logs);
                        ++m_result.pulled;
                        touched();
                    } else if (local->etag != it.etag() && !it.etag().isEmpty()) {
                        // Conflict: preserve local edits in offline copy, accept remote version
                        RoutineBlock backup = *local;
                        backup.id = 0;
                        backup.uid = newUid();
                        backup.title = QStringLiteral("%1 (%2)").arg(local->title, m_conflictSuffix);
                        backup.syncAccountId = 0;
                        backup.calendarHref.clear();
                        backup.href.clear();
                        backup.etag.clear();
                        backup.baseIcs.clear();
                        backup.syncState = SyncState::Synced;
                        m_db->insertRoutineBlock(backup);

                        RoutineBlock upd = *local;
                        applyRemoteRoutineBlock(upd, dec->block, calHref, it.href, it.etag(), ics);
                        m_db->updateRoutineBlockFull(upd);
                        mergeRemoteRoutineLogs(m_db.get(), upd.id, dec->logs);
                        m_result.conflictTitles << upd.title;
                        ++m_result.pulled;
                        touched();
                    }
                }
            }

            // Delete synced local blocks removed on server (never touch offline syncAccountId == 0 blocks)
            for (const RoutineBlock &b : m_db->routineBlocks(true, false)) {
                if (b.syncAccountId == acc.id && b.calendarHref == calHref && !b.href.isEmpty() && b.syncState == SyncState::Synced
                    && !seenHrefs.contains(b.href)) {
                    m_db->removeRoutineBlock(b.id);
                    ++m_result.pulled;
                    touched();
                }
            }
            if (!targetCol.ctag.isEmpty()) {
                m_db->setMeta(ctagKey, targetCol.ctag);
            }
        }
    }

    // 3. Push local new/modified synced routine blocks
    bool pushedAny = false;
    for (const RoutineBlock &b : m_db->routineBlocks(true, false)) {
        if (m_cancel.load()) {
            return fail(DavResponse(), QStringLiteral("uploading routine blocks"));
        }
        if (b.syncAccountId != acc.id || b.syncState == SyncState::Synced) {
            continue;
        }
        if (!pushRoutineBlock(calHref, b, acc.id)) {
            return false;
        }
        pushedAny = true;
    }
    if (pushedAny) {
        m_db->setMeta(ctagKey, QString());
    }
    return true;
}

bool SyncEngine::pushRoutineBlock(const QString &calHref, RoutineBlock b, qint64 accountId, int depth)
{
    // If calendar preference changed while keeping old href on another calendar, delete old remote first.
    if (!b.href.isEmpty() && !b.calendarHref.isEmpty() && b.calendarHref != calHref) {
        m_client->remove(b.href, b.etag);
        b.href.clear();
        b.etag.clear();
        b.baseIcs.clear();
    }
    const QList<RoutineLog> logs = m_db->routineLogsForBlock(b.id, QDate::currentDate().addDays(-90));
    const QString ics = IcsCodec::encodeEvent(b, logs);
    const bool create = b.href.isEmpty();
    const QString href = create ? (calHref + fileNameFor(b.uid)) : b.href;
    const DavResponse r = m_client->put(href, ics, b.etag, create);
    if (r.ok()) {
        QString etag = r.etag;
        if (etag.isEmpty()) {
            etag = m_client->etagOf(href);
        }
        m_db->markRoutineUploaded(b.id, b.rev, calHref, href, etag, ics);
        ++m_result.pushed;
        touched();
        return true;
    }
    if ((r.status == 412 || r.status == 404 || (create && r.status == 405)) && depth < 2) {
        const DavResponse g = m_client->get(href);
        if (g.status == 404) {
            if (create) {
                return softFail(r, QStringLiteral("uploading routine \"%1\"").arg(b.title));
            }
            b.href.clear();
            b.etag.clear();
            b.baseIcs.clear();
            b.syncState = SyncState::New;
            m_db->updateRoutineBlockFull(b);
            return pushRoutineBlock(calHref, b, accountId, depth + 1);
        }
        if (!g.ok()) {
            return softFail(g, QStringLiteral("uploading routine \"%1\"").arg(b.title));
        }
        const QString remoteIcs = QString::fromUtf8(g.body);
        if (const auto dec = IcsCodec::decodeEvent(remoteIcs)) {
            RoutineBlock backup = b;
            backup.id = 0;
            backup.uid = newUid();
            backup.title = QStringLiteral("%1 (%2)").arg(b.title, m_conflictSuffix);
            backup.syncAccountId = 0;
            backup.calendarHref.clear();
            backup.href.clear();
            backup.etag.clear();
            backup.baseIcs.clear();
            backup.syncState = SyncState::Synced;
            m_db->insertRoutineBlock(backup);

            applyRemoteRoutineBlock(b, dec->block, calHref, href,
                                    g.etag.isEmpty() ? m_client->etagOf(href) : g.etag, remoteIcs);
            m_db->updateRoutineBlockFull(b);
            mergeRemoteRoutineLogs(m_db.get(), b.id, dec->logs);
            m_result.conflictTitles << b.title;
            touched();
            return true;
        }
    }
    return softFail(r, QStringLiteral("uploading routine \"%1\"").arg(b.title));
}

}
