// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"
#include "logging.h"

#include <QAtomicInt>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QThread>
#include <QTimeZone>
#include <QVariant>

namespace KTasks
{

static QStringList s_extraMigrations;

// Each entry upgrades the schema by one version. Never edit an entry that
// has shipped; append a new one instead.
static QStringList builtinMigrations()
{
    return {
        // v1
        QStringLiteral(R"(
CREATE TABLE accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL DEFAULT '',
    server_url TEXT NOT NULL DEFAULT '',
    dav_url TEXT NOT NULL DEFAULT '',
    username TEXT NOT NULL DEFAULT '',
    principal_url TEXT NOT NULL DEFAULT '',
    home_url TEXT NOT NULL DEFAULT '',
    last_sync TEXT NOT NULL DEFAULT '',
    last_error TEXT NOT NULL DEFAULT ''
);
CREATE TABLE lists (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL DEFAULT 0,
    name TEXT NOT NULL DEFAULT '',
    color TEXT NOT NULL DEFAULT '',
    icon TEXT NOT NULL DEFAULT '',
    href TEXT NOT NULL DEFAULT '',
    ctag TEXT NOT NULL DEFAULT '',
    sync_token TEXT NOT NULL DEFAULT '',
    sort_order REAL NOT NULL DEFAULT 0,
    sync_state INTEGER NOT NULL DEFAULT 1,
    read_only INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE tasks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uid TEXT NOT NULL,
    list_id INTEGER NOT NULL,
    parent_uid TEXT NOT NULL DEFAULT '',
    title TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    priority INTEGER NOT NULL DEFAULT 0,
    start_at TEXT NOT NULL DEFAULT '',
    due_at TEXT NOT NULL DEFAULT '',
    all_day INTEGER NOT NULL DEFAULT 1,
    completed_at TEXT NOT NULL DEFAULT '',
    status INTEGER NOT NULL DEFAULT 0,
    percent INTEGER NOT NULL DEFAULT 0,
    tags TEXT NOT NULL DEFAULT '[]',
    rrule TEXT NOT NULL DEFAULT '',
    alarms TEXT NOT NULL DEFAULT '[]',
    sort_order REAL NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT '',
    modified_at TEXT NOT NULL DEFAULT '',
    collapsed INTEGER NOT NULL DEFAULT 0,
    href TEXT NOT NULL DEFAULT '',
    etag TEXT NOT NULL DEFAULT '',
    base_ics TEXT NOT NULL DEFAULT '',
    sync_state INTEGER NOT NULL DEFAULT 1,
    rev INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX tasks_list ON tasks(list_id);
CREATE INDEX tasks_uid ON tasks(uid);
CREATE INDEX tasks_href ON tasks(list_id, href);
CREATE TABLE reminder_log (
    task_uid TEXT NOT NULL,
    trigger_key TEXT NOT NULL,
    fired_at TEXT NOT NULL,
    PRIMARY KEY (task_uid, trigger_key)
);
CREATE TABLE snoozes (
    task_uid TEXT PRIMARY KEY,
    until TEXT NOT NULL
);
CREATE TABLE meta (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
)"),
        // v2: Journal entries and Routine planner
        QStringLiteral(R"(
CREATE TABLE journal_entries (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uid TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL DEFAULT '',
    content TEXT NOT NULL DEFAULT '',
    entry_date TEXT NOT NULL DEFAULT '',
    tags TEXT NOT NULL DEFAULT '[]',
    mood TEXT NOT NULL DEFAULT '',
    is_locked INTEGER NOT NULL DEFAULT 0,
    kdf_salt TEXT NOT NULL DEFAULT '',
    password_hash TEXT NOT NULL DEFAULT '',
    encrypted_content TEXT NOT NULL DEFAULT '',
    created_at TEXT NOT NULL DEFAULT '',
    modified_at TEXT NOT NULL DEFAULT '',
    deleted INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX journal_date ON journal_entries(entry_date);
CREATE TABLE routine_blocks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    uid TEXT NOT NULL UNIQUE,
    title TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    start_minute INTEGER NOT NULL DEFAULT 540,
    duration_minutes INTEGER NOT NULL DEFAULT 60,
    days_mask INTEGER NOT NULL DEFAULT 127,
    specific_date TEXT NOT NULL DEFAULT '',
    linked_task_id INTEGER NOT NULL DEFAULT 0,
    color TEXT NOT NULL DEFAULT '#3daee9',
    category TEXT NOT NULL DEFAULT '',
    notify_enabled INTEGER NOT NULL DEFAULT 1,
    notify_minutes_before INTEGER NOT NULL DEFAULT 5,
    active INTEGER NOT NULL DEFAULT 1,
    sort_order REAL NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT '',
    modified_at TEXT NOT NULL DEFAULT ''
);
CREATE INDEX routine_blocks_date ON routine_blocks(specific_date);
CREATE TABLE routine_logs (
    block_id INTEGER NOT NULL,
    log_date TEXT NOT NULL,
    status INTEGER NOT NULL DEFAULT 0,
    completed_at TEXT NOT NULL DEFAULT '',
    PRIMARY KEY (block_id, log_date)
);
)"),
        // v3: Journal notebooks
        QStringLiteral(R"(
CREATE TABLE journal_notebooks (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL DEFAULT '',
    color TEXT NOT NULL DEFAULT '#3daee9',
    icon TEXT NOT NULL DEFAULT 'notebook',
    sort_order REAL NOT NULL DEFAULT 0,
    created_at TEXT NOT NULL DEFAULT ''
);
ALTER TABLE journal_entries ADD COLUMN notebook_id INTEGER NOT NULL DEFAULT 1;
CREATE INDEX journal_notebook ON journal_entries(notebook_id);
)"),
        // v4: Nextcloud Notes sync metadata and offline/synced journal toggle
        QStringLiteral(R"(
ALTER TABLE journal_notebooks ADD COLUMN sync_account_id INTEGER NOT NULL DEFAULT 0;
ALTER TABLE journal_entries ADD COLUMN sync_account_id INTEGER NOT NULL DEFAULT 0;
ALTER TABLE journal_entries ADD COLUMN remote_id INTEGER NOT NULL DEFAULT 0;
ALTER TABLE journal_entries ADD COLUMN etag TEXT NOT NULL DEFAULT '';
ALTER TABLE journal_entries ADD COLUMN remote_modified INTEGER NOT NULL DEFAULT 0;
ALTER TABLE journal_entries ADD COLUMN sync_state INTEGER NOT NULL DEFAULT 0;
CREATE INDEX journal_remote ON journal_entries(sync_account_id, remote_id);
CREATE TABLE journal_remote_deletes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    remote_id INTEGER NOT NULL,
    UNIQUE(account_id, remote_id)
);
)"),
        // v5: Routine sync metadata (CalDAV VEVENT) and offline/synced routine toggle
        QStringLiteral(R"(
ALTER TABLE routine_blocks ADD COLUMN sync_account_id INTEGER NOT NULL DEFAULT 0;
ALTER TABLE routine_blocks ADD COLUMN calendar_href TEXT NOT NULL DEFAULT '';
ALTER TABLE routine_blocks ADD COLUMN href TEXT NOT NULL DEFAULT '';
ALTER TABLE routine_blocks ADD COLUMN etag TEXT NOT NULL DEFAULT '';
ALTER TABLE routine_blocks ADD COLUMN base_ics TEXT NOT NULL DEFAULT '';
ALTER TABLE routine_blocks ADD COLUMN sync_state INTEGER NOT NULL DEFAULT 0;
ALTER TABLE routine_blocks ADD COLUMN rev INTEGER NOT NULL DEFAULT 0;
ALTER TABLE routine_blocks ADD COLUMN deleted INTEGER NOT NULL DEFAULT 0;
CREATE INDEX routine_sync ON routine_blocks(sync_account_id, sync_state);
CREATE INDEX routine_href ON routine_blocks(calendar_href, href);
CREATE TABLE routine_remote_deletes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER NOT NULL,
    href TEXT NOT NULL,
    etag TEXT NOT NULL DEFAULT '',
    UNIQUE(account_id, href)
);
)"),
        // v6: Eisenhower priority matrix quadrant on tasks
        QStringLiteral(R"(
ALTER TABLE tasks ADD COLUMN quadrant INTEGER NOT NULL DEFAULT 0;
)"),
        // v7: Task execution time, Pomodoro counter, and focus session history
        QStringLiteral(R"(
ALTER TABLE tasks ADD COLUMN estimated_minutes INTEGER NOT NULL DEFAULT 0;
ALTER TABLE tasks ADD COLUMN tracked_seconds INTEGER NOT NULL DEFAULT 0;
ALTER TABLE tasks ADD COLUMN pomodoros_completed INTEGER NOT NULL DEFAULT 0;
CREATE TABLE focus_sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    task_id INTEGER NOT NULL DEFAULT 0,
    task_uid TEXT NOT NULL DEFAULT '',
    task_title TEXT NOT NULL DEFAULT '',
    list_id INTEGER NOT NULL DEFAULT 0,
    quadrant INTEGER NOT NULL DEFAULT 0,
    mode INTEGER NOT NULL DEFAULT 0,
    duration_seconds INTEGER NOT NULL DEFAULT 0,
    completed_pomodoro INTEGER NOT NULL DEFAULT 0,
    started_at TEXT NOT NULL DEFAULT '',
    ended_at TEXT NOT NULL DEFAULT ''
);
CREATE INDEX focus_sessions_ended ON focus_sessions(ended_at);
CREATE INDEX focus_sessions_task ON focus_sessions(task_id);
)"),
        // v8: Earliest active start date for recurring routine templates
        QStringLiteral(R"(
ALTER TABLE routine_blocks ADD COLUMN start_date TEXT NOT NULL DEFAULT '';
)"),
    };
}

static QStringList allMigrations()
{
    return builtinMigrations() + s_extraMigrations;
}

void Database::setExtraMigrationsForTesting(const QStringList &migrations)
{
    s_extraMigrations = migrations;
}

int Database::latestSchemaVersion()
{
    return allMigrations().size();
}

QString dateToDb(const QDateTime &dt, bool allDay)
{
    if (!dt.isValid()) {
        return {};
    }
    if (allDay) {
        return dt.date().toString(Qt::ISODate) + QStringLiteral("T00:00:00");
    }
    return dt.toUTC().toString(Qt::ISODate);
}

QDateTime dateFromDb(const QString &s, bool allDay)
{
    if (s.isEmpty()) {
        return {};
    }
    if (allDay) {
        return QDateTime(QDate::fromString(s.left(10), Qt::ISODate), QTime(0, 0));
    }
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    return dt.toUTC();
}

static QString tsToDb(const QDateTime &dt)
{
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODate) : QString();
}

static QDateTime tsFromDb(const QString &s)
{
    return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODate).toUTC();
}

Database::Database(const QString &path)
    : m_path(path)
{
    static QAtomicInt counter;
    m_connName = QStringLiteral("ktasks-%1-%2").arg(quintptr(QThread::currentThreadId())).arg(counter.fetchAndAddRelaxed(1));
}

Database::~Database()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connName, false);
        if (db.isValid()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connName);
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connName, false);
}

bool Database::isOpen() const
{
    return handle().isOpen();
}

bool Database::exec(QSqlQuery &q)
{
    // A null QString would be stored as NULL and break the NOT NULL columns
    // and the "= ''" comparisons; store it as an empty string instead.
    const QVariantList bound = q.boundValues();
    for (int i = 0; i < bound.size(); ++i) {
        const QVariant &v = bound.at(i);
        if (v.typeId() == QMetaType::QString && v.toString().isNull()) {
            q.bindValue(i, QStringLiteral(""));
        }
    }
    if (!q.exec()) {
        m_lastError = q.lastError().text();
        qCWarning(lcDb) << "query failed:" << m_lastError << "--" << q.lastQuery().left(120);
        return false;
    }
    return true;
}

bool Database::execSql(const QString &sql)
{
    QSqlQuery q(handle());
    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        qCWarning(lcDb) << "statement failed:" << m_lastError << "--" << sql.left(120);
        return false;
    }
    return true;
}

bool Database::open(QString *error)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(m_path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
    if (!db.open()) {
        m_lastError = db.lastError().text();
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    execSql(QStringLiteral("PRAGMA journal_mode=WAL"));
    execSql(QStringLiteral("PRAGMA synchronous=NORMAL"));
    execSql(QStringLiteral("PRAGMA temp_store=FILE"));
    execSql(QStringLiteral("PRAGMA cache_size=-1500"));
    execSql(QStringLiteral("PRAGMA mmap_size=0"));
    execSql(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (!migrate(error)) {
        db.close();
        return false;
    }
    return true;
}

int Database::schemaVersion()
{
    QSqlQuery q(handle());
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

bool Database::backupBeforeMigration(int fromVersion)
{
    execSql(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    const QFileInfo fi(m_path);
    const QString dir = fi.absolutePath() + QStringLiteral("/backups");
    QDir().mkpath(dir);
    const QString target = QStringLiteral("%1/%2-v%3-%4.db")
                               .arg(dir, fi.completeBaseName())
                               .arg(fromVersion)
                               .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    if (!QFile::copy(m_path, target)) {
        m_lastError = QStringLiteral("could not write backup %1").arg(target);
        return false;
    }
    qCInfo(lcDb) << "database backed up to" << target;
    return true;
}

bool Database::migrate(QString *error)
{
    const QStringList migrations = allMigrations();
    int version = schemaVersion();
    if (version > migrations.size()) {
        m_lastError = QStringLiteral("database was created by a newer version of the application (schema %1)").arg(version);
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    if (version == migrations.size()) {
        return true;
    }
    if (version > 0 && !backupBeforeMigration(version)) {
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    while (version < migrations.size()) {
        Transaction tx(this);
        bool ok = true;
        const QStringList statements = migrations.at(version).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString &raw : statements) {
            const QString sql = raw.trimmed();
            if (sql.isEmpty()) {
                continue;
            }
            if (!execSql(sql)) {
                ok = false;
                break;
            }
        }
        ok = ok && execSql(QStringLiteral("PRAGMA user_version=%1").arg(version + 1));
        if (!ok || !tx.commit()) {
            if (error) {
                *error = QStringLiteral("migration to schema %1 failed: %2").arg(version + 1).arg(m_lastError);
            }
            return false;
        }
        ++version;
        qCInfo(lcDb) << "schema migrated to version" << version;
    }
    return true;
}

// ---- Transaction ----

Database::Transaction::Transaction(Database *db)
    : m_db(db)
{
    if (m_db->m_txDepth == 0) {
        m_owner = m_db->execSql(QStringLiteral("BEGIN IMMEDIATE"));
    }
    ++m_db->m_txDepth;
}

Database::Transaction::~Transaction()
{
    --m_db->m_txDepth;
    if (m_owner && !m_done) {
        m_db->execSql(QStringLiteral("ROLLBACK"));
    }
}

bool Database::Transaction::commit()
{
    if (m_done) {
        return true;
    }
    m_done = true;
    if (!m_owner) {
        return true;
    }
    return m_db->execSql(QStringLiteral("COMMIT"));
}

// ---- Accounts ----

Account Database::accountFromQuery(const QSqlQuery &q)
{
    Account a;
    a.id = q.value(0).toLongLong();
    a.name = q.value(1).toString();
    a.serverUrl = q.value(2).toString();
    a.davUrl = q.value(3).toString();
    a.username = q.value(4).toString();
    a.principalUrl = q.value(5).toString();
    a.homeUrl = q.value(6).toString();
    a.lastSync = tsFromDb(q.value(7).toString());
    a.lastError = q.value(8).toString();
    return a;
}

static const char *kAccountCols = "id, name, server_url, dav_url, username, principal_url, home_url, last_sync, last_error";

QList<Account> Database::accounts()
{
    QList<Account> out;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM accounts ORDER BY id").arg(QLatin1String(kAccountCols)));
    if (exec(q)) {
        while (q.next()) {
            out.append(accountFromQuery(q));
        }
    }
    return out;
}

std::optional<Account> Database::account(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM accounts WHERE id=?").arg(QLatin1String(kAccountCols)));
    q.addBindValue(id);
    if (exec(q) && q.next()) {
        return accountFromQuery(q);
    }
    return std::nullopt;
}

bool Database::insertAccount(Account &a)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO accounts (name, server_url, dav_url, username, principal_url, home_url, last_sync, last_error) VALUES (?,?,?,?,?,?,?,?)"));
    q.addBindValue(a.name);
    q.addBindValue(a.serverUrl);
    q.addBindValue(a.davUrl);
    q.addBindValue(a.username);
    q.addBindValue(a.principalUrl);
    q.addBindValue(a.homeUrl);
    q.addBindValue(tsToDb(a.lastSync));
    q.addBindValue(a.lastError);
    if (!exec(q)) {
        return false;
    }
    a.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::updateAccount(const Account &a)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE accounts SET name=?, server_url=?, dav_url=?, username=?, principal_url=?, home_url=?, last_sync=?, last_error=? WHERE id=?"));
    q.addBindValue(a.name);
    q.addBindValue(a.serverUrl);
    q.addBindValue(a.davUrl);
    q.addBindValue(a.username);
    q.addBindValue(a.principalUrl);
    q.addBindValue(a.homeUrl);
    q.addBindValue(tsToDb(a.lastSync));
    q.addBindValue(a.lastError);
    q.addBindValue(a.id);
    return exec(q);
}

bool Database::removeAccount(qint64 id)
{
    if (id <= 0) {
        return false;
    }
    Transaction tx(this);
    QSqlQuery q(handle());
    // Preserve journal notes and routine blocks locally as offline-only rather than discarding personal data.
    q.prepare(QStringLiteral("DELETE FROM routine_blocks WHERE sync_account_id=? AND deleted=1"));
    q.addBindValue(id);
    bool ok = exec(q);
    q.prepare(QStringLiteral("UPDATE routine_blocks SET sync_account_id=0, calendar_href='', href='', etag='', base_ics='', sync_state=0 WHERE sync_account_id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("DELETE FROM routine_remote_deletes WHERE account_id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("UPDATE journal_notebooks SET sync_account_id=0 WHERE sync_account_id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("UPDATE journal_entries SET sync_account_id=0, remote_id=0, etag='', remote_modified=0, sync_state=0 WHERE sync_account_id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("DELETE FROM journal_remote_deletes WHERE account_id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("DELETE FROM tasks WHERE list_id IN (SELECT id FROM lists WHERE account_id=?)"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("DELETE FROM lists WHERE account_id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    q.prepare(QStringLiteral("DELETE FROM accounts WHERE id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    return ok && tx.commit();
}

// ---- Lists ----

static const char *kListCols = "id, account_id, name, color, icon, href, ctag, sync_token, sort_order, sync_state, read_only";

TaskList Database::listFromQuery(const QSqlQuery &q)
{
    TaskList l;
    l.id = q.value(0).toLongLong();
    l.accountId = q.value(1).toLongLong();
    l.name = q.value(2).toString();
    l.color = q.value(3).toString();
    l.icon = q.value(4).toString();
    l.href = q.value(5).toString();
    l.ctag = q.value(6).toString();
    l.syncToken = q.value(7).toString();
    l.sortOrder = q.value(8).toDouble();
    l.syncState = SyncState(q.value(9).toInt());
    l.readOnly = q.value(10).toBool();
    return l;
}

QList<TaskList> Database::lists(bool includeDeleted)
{
    QList<TaskList> out;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM lists %2 ORDER BY account_id, sort_order, id")
                  .arg(QLatin1String(kListCols), includeDeleted ? QString() : QStringLiteral("WHERE sync_state != 3")));
    if (exec(q)) {
        while (q.next()) {
            out.append(listFromQuery(q));
        }
    }
    return out;
}

std::optional<TaskList> Database::list(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM lists WHERE id=?").arg(QLatin1String(kListCols)));
    q.addBindValue(id);
    if (exec(q) && q.next()) {
        return listFromQuery(q);
    }
    return std::nullopt;
}

bool Database::insertList(TaskList &l)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO lists (account_id, name, color, icon, href, ctag, sync_token, sort_order, sync_state, read_only) VALUES (?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(l.accountId);
    q.addBindValue(l.name);
    q.addBindValue(l.color);
    q.addBindValue(l.icon);
    q.addBindValue(l.href);
    q.addBindValue(l.ctag);
    q.addBindValue(l.syncToken);
    q.addBindValue(l.sortOrder);
    q.addBindValue(int(l.syncState));
    q.addBindValue(l.readOnly);
    if (!exec(q)) {
        return false;
    }
    l.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::updateListUser(const TaskList &l)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE lists SET name=?, color=?, icon=?, sort_order=?, "
                             "sync_state = CASE WHEN account_id = 0 OR href = '' THEN 1 ELSE 2 END WHERE id=?"));
    q.addBindValue(l.name);
    q.addBindValue(l.color);
    q.addBindValue(l.icon);
    q.addBindValue(l.sortOrder);
    q.addBindValue(l.id);
    return exec(q);
}

bool Database::updateListFull(const TaskList &l)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE lists SET account_id=?, name=?, color=?, icon=?, href=?, ctag=?, sync_token=?, sort_order=?, sync_state=?, read_only=? WHERE id=?"));
    q.addBindValue(l.accountId);
    q.addBindValue(l.name);
    q.addBindValue(l.color);
    q.addBindValue(l.icon);
    q.addBindValue(l.href);
    q.addBindValue(l.ctag);
    q.addBindValue(l.syncToken);
    q.addBindValue(l.sortOrder);
    q.addBindValue(int(l.syncState));
    q.addBindValue(l.readOnly);
    q.addBindValue(l.id);
    return exec(q);
}

bool Database::purgeList(qint64 id)
{
    Transaction tx(this);
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM tasks WHERE list_id=?"));
    q.addBindValue(id);
    bool ok = exec(q);
    q.prepare(QStringLiteral("DELETE FROM lists WHERE id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    return ok && tx.commit();
}

bool Database::removeListUser(qint64 id)
{
    const auto l = list(id);
    if (!l) {
        return false;
    }
    if (l->accountId == 0 || l->href.isEmpty()) {
        return purgeList(id);
    }
    // Tombstone: the tasks go right away, the collection is deleted on the next sync.
    Transaction tx(this);
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM tasks WHERE list_id=?"));
    q.addBindValue(id);
    bool ok = exec(q);
    q.prepare(QStringLiteral("UPDATE lists SET sync_state=3 WHERE id=?"));
    q.addBindValue(id);
    ok = ok && exec(q);
    return ok && tx.commit();
}

bool Database::moveListToAccount(qint64 listId, qint64 targetAccountId)
{
    const auto cur = list(listId);
    if (!cur || cur->readOnly) {
        return false;
    }
    if (cur->accountId == targetAccountId) {
        return true;
    }
    if (targetAccountId > 0 && !account(targetAccountId)) {
        return false;
    }
    Transaction tx(this);
    // If leaving a Nextcloud list offline, queue removal of the remote collection while keeping local tasks safe.
    if (cur->accountId > 0 && !cur->href.isEmpty()) {
        TaskList tomb;
        tomb.accountId = cur->accountId;
        tomb.name = cur->name;
        tomb.color = cur->color;
        tomb.icon = cur->icon;
        tomb.href = cur->href;
        tomb.sortOrder = cur->sortOrder;
        tomb.syncState = SyncState::Deleted;
        if (!insertList(tomb)) {
            return false;
        }
    }
    TaskList updated = *cur;
    updated.accountId = targetAccountId > 0 ? targetAccountId : 0;
    updated.href.clear();
    updated.ctag.clear();
    updated.syncToken.clear();
    updated.syncState = updated.accountId > 0 ? SyncState::New : SyncState::Synced;
    if (!updateListFull(updated)) {
        return false;
    }
    QSqlQuery qt0(handle());
    qt0.prepare(QStringLiteral("DELETE FROM tasks WHERE list_id=? AND sync_state=3"));
    qt0.addBindValue(listId);
    if (!exec(qt0)) {
        return false;
    }
    QSqlQuery qt(handle());
    qt.prepare(QStringLiteral("UPDATE tasks SET href='', etag='', base_ics='', sync_state=?, rev = rev + 1 WHERE list_id=?"));
    qt.addBindValue(updated.accountId > 0 ? int(SyncState::New) : int(SyncState::Synced));
    qt.addBindValue(listId);
    return exec(qt) && tx.commit();
}

// ---- Tasks ----

static const char *kTaskCols =
    "id, uid, list_id, parent_uid, title, notes, priority, start_at, due_at, all_day, completed_at, status, percent, "
    "tags, rrule, alarms, sort_order, created_at, modified_at, collapsed, href, etag, base_ics, sync_state, rev, quadrant, "
    "estimated_minutes, tracked_seconds, pomodoros_completed";

Task Database::taskFromQuery(const QSqlQuery &q)
{
    Task t;
    t.id = q.value(0).toLongLong();
    t.uid = q.value(1).toString();
    t.listId = q.value(2).toLongLong();
    t.parentUid = q.value(3).toString();
    t.title = q.value(4).toString();
    t.notes = q.value(5).toString();
    t.priority = q.value(6).toInt();
    t.allDay = q.value(9).toBool();
    t.start = dateFromDb(q.value(7).toString(), t.allDay);
    t.due = dateFromDb(q.value(8).toString(), t.allDay);
    t.completedAt = tsFromDb(q.value(10).toString());
    t.status = TaskStatus(q.value(11).toInt());
    t.percent = q.value(12).toInt();
    const QJsonArray tags = QJsonDocument::fromJson(q.value(13).toString().toUtf8()).array();
    for (const QJsonValue &v : tags) {
        t.tags.append(v.toString());
    }
    t.rrule = q.value(14).toString();
    t.alarms = alarmsFromJson(q.value(15).toString());
    t.sortOrder = q.value(16).toDouble();
    t.created = tsFromDb(q.value(17).toString());
    t.modified = tsFromDb(q.value(18).toString());
    t.collapsed = q.value(19).toBool();
    t.href = q.value(20).toString();
    t.etag = q.value(21).toString();
    t.baseIcs = q.value(22).toString();
    t.syncState = SyncState(q.value(23).toInt());
    t.rev = q.value(24).toLongLong();
    t.quadrant = q.value(25).toInt();
    t.estimatedMinutes = q.value(26).toInt();
    t.trackedSeconds = q.value(27).toInt();
    t.pomodorosCompleted = q.value(28).toInt();
    return t;
}

static QString tagsToJson(const QStringList &tags)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(tags)).toJson(QJsonDocument::Compact));
}

// Binds the user-visible columns in the order used by insert/update statements.
void Database::bindTask(QSqlQuery &q, const Task &t, bool full)
{
    q.addBindValue(t.uid);
    q.addBindValue(t.listId);
    q.addBindValue(t.parentUid);
    q.addBindValue(t.title);
    q.addBindValue(t.notes);
    q.addBindValue(t.priority);
    q.addBindValue(dateToDb(t.start, t.allDay));
    q.addBindValue(dateToDb(t.due, t.allDay));
    q.addBindValue(t.allDay);
    q.addBindValue(tsToDb(t.completedAt));
    q.addBindValue(int(t.status));
    q.addBindValue(t.percent);
    q.addBindValue(tagsToJson(t.tags));
    q.addBindValue(t.rrule);
    q.addBindValue(alarmsToJson(t.alarms));
    q.addBindValue(t.sortOrder);
    q.addBindValue(tsToDb(t.created));
    q.addBindValue(tsToDb(t.modified));
    q.addBindValue(t.collapsed);
    q.addBindValue(qBound(0, t.quadrant, 4));
    q.addBindValue(qMax(0, t.estimatedMinutes));
    q.addBindValue(qMax(0, t.trackedSeconds));
    q.addBindValue(qMax(0, t.pomodorosCompleted));
    if (full) {
        q.addBindValue(t.href);
        q.addBindValue(t.etag);
        q.addBindValue(t.baseIcs);
        q.addBindValue(int(t.syncState));
        q.addBindValue(t.rev);
    }
}

QList<Task> Database::tasks(bool includeDeleted, bool includeBaseIcs)
{
    QList<Task> out;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    QString cols = QString::fromLatin1(kTaskCols);
    if (!includeBaseIcs) {
        cols.replace(QStringLiteral("base_ics"), QStringLiteral("''"));
    }
    q.prepare(QStringLiteral("SELECT %1 FROM tasks %2 ORDER BY id")
                  .arg(cols, includeDeleted ? QString() : QStringLiteral("WHERE sync_state != 3")));
    if (exec(q)) {
        while (q.next()) {
            out.append(taskFromQuery(q));
        }
    }
    return out;
}

QList<Task> Database::tasksInList(qint64 listId, bool includeDeleted)
{
    QList<Task> out;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT %1 FROM tasks WHERE list_id=? %2 ORDER BY id")
                  .arg(QLatin1String(kTaskCols), includeDeleted ? QString() : QStringLiteral("AND sync_state != 3")));
    q.addBindValue(listId);
    if (exec(q)) {
        while (q.next()) {
            out.append(taskFromQuery(q));
        }
    }
    return out;
}

std::optional<Task> Database::task(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM tasks WHERE id=?").arg(QLatin1String(kTaskCols)));
    q.addBindValue(id);
    if (exec(q) && q.next()) {
        return taskFromQuery(q);
    }
    return std::nullopt;
}

std::optional<Task> Database::taskByHref(qint64 listId, const QString &href)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM tasks WHERE list_id=? AND href=? ORDER BY sync_state LIMIT 1").arg(QLatin1String(kTaskCols)));
    q.addBindValue(listId);
    q.addBindValue(href);
    if (exec(q) && q.next()) {
        return taskFromQuery(q);
    }
    return std::nullopt;
}

std::optional<Task> Database::taskByUid(qint64 listId, const QString &uid)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT %1 FROM tasks WHERE list_id=? AND uid=? AND sync_state != 3 LIMIT 1").arg(QLatin1String(kTaskCols)));
    q.addBindValue(listId);
    q.addBindValue(uid);
    if (exec(q) && q.next()) {
        return taskFromQuery(q);
    }
    return std::nullopt;
}

bool Database::insertTask(Task &t)
{
    if (t.uid.isEmpty()) {
        t.uid = newUid();
    }
    if (!t.created.isValid()) {
        t.created = QDateTime::currentDateTimeUtc();
    }
    if (!t.modified.isValid()) {
        t.modified = t.created;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO tasks (uid, list_id, parent_uid, title, notes, priority, start_at, due_at, all_day, completed_at, status, percent, "
                             "tags, rrule, alarms, sort_order, created_at, modified_at, collapsed, quadrant, estimated_minutes, tracked_seconds, pomodoros_completed, "
                             "href, etag, base_ics, sync_state, rev) "
                             "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    bindTask(q, t, true);
    if (!exec(q)) {
        return false;
    }
    t.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::updateTaskUser(const Task &t)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE tasks SET uid=?, list_id=?, parent_uid=?, title=?, notes=?, priority=?, start_at=?, due_at=?, all_day=?, completed_at=?, "
                             "status=?, percent=?, tags=?, rrule=?, alarms=?, sort_order=?, created_at=?, modified_at=?, collapsed=?, quadrant=?, "
                             "estimated_minutes=?, tracked_seconds=?, pomodoros_completed=?, "
                             "rev = rev + 1, sync_state = CASE WHEN href = '' THEN 1 ELSE 2 END WHERE id=?"));
    bindTask(q, t, false);
    q.addBindValue(t.id);
    return exec(q);
}

bool Database::updateTaskFull(const Task &t)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE tasks SET uid=?, list_id=?, parent_uid=?, title=?, notes=?, priority=?, start_at=?, due_at=?, all_day=?, completed_at=?, "
                             "status=?, percent=?, tags=?, rrule=?, alarms=?, sort_order=?, created_at=?, modified_at=?, collapsed=?, quadrant=?, "
                             "estimated_minutes=?, tracked_seconds=?, pomodoros_completed=?, "
                             "href=?, etag=?, base_ics=?, sync_state=?, rev=? WHERE id=?"));
    bindTask(q, t, true);
    q.addBindValue(t.id);
    return exec(q);
}

bool Database::markUploaded(qint64 id, qint64 revAtUpload, const QString &href, const QString &etag, const QString &ics)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE tasks SET href=?, etag=?, base_ics=?, sync_state = CASE WHEN sync_state = 3 THEN 3 WHEN rev = ? THEN 0 ELSE 2 END WHERE id=?"));
    q.addBindValue(href);
    q.addBindValue(etag);
    q.addBindValue(ics);
    q.addBindValue(revAtUpload);
    q.addBindValue(id);
    return exec(q);
}

bool Database::removeTaskUser(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM tasks WHERE id=? AND href = ''"));
    q.addBindValue(id);
    if (!exec(q)) {
        return false;
    }
    q.prepare(QStringLiteral("UPDATE tasks SET sync_state=3, rev = rev + 1 WHERE id=?"));
    q.addBindValue(id);
    return exec(q);
}

bool Database::purgeTask(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM tasks WHERE id=?"));
    q.addBindValue(id);
    return exec(q);
}

bool Database::setCollapsed(qint64 id, bool collapsed)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE tasks SET collapsed=? WHERE id=?"));
    q.addBindValue(collapsed);
    q.addBindValue(id);
    return exec(q);
}

bool Database::moveTask(qint64 id, qint64 newListId)
{
    Transaction tx(this);
    auto cur = task(id);
    if (!cur) {
        return false;
    }
    if (cur->listId == newListId) {
        return tx.commit();
    }
    if (!cur->href.isEmpty()) {
        Task tomb = *cur;
        tomb.id = 0;
        tomb.syncState = SyncState::Deleted;
        if (!insertTask(tomb)) {
            return false;
        }
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE tasks SET list_id=?, href='', etag='', base_ics='', sync_state=1, rev = rev + 1 WHERE id=?"));
    q.addBindValue(newListId);
    q.addBindValue(id);
    return exec(q) && tx.commit();
}

bool Database::hasPendingChanges(qint64 accountId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT EXISTS(SELECT 1 FROM tasks t JOIN lists l ON l.id = t.list_id WHERE l.account_id=? AND t.sync_state != 0) "
                             "OR EXISTS(SELECT 1 FROM lists WHERE account_id=? AND sync_state != 0)"));
    q.addBindValue(accountId);
    q.addBindValue(accountId);
    return exec(q) && q.next() && q.value(0).toBool();
}

bool Database::hasPendingJournalChanges(qint64 accountId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT EXISTS(SELECT 1 FROM journal_entries WHERE deleted=0 AND is_locked=0 AND sync_account_id=? AND sync_state != 0) "
                             "OR EXISTS(SELECT 1 FROM journal_remote_deletes WHERE account_id=?)"));
    q.addBindValue(accountId);
    q.addBindValue(accountId);
    return exec(q) && q.next() && q.value(0).toBool();
}

bool Database::hasPendingRoutineChanges(qint64 accountId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT EXISTS(SELECT 1 FROM routine_blocks WHERE sync_account_id=? AND (sync_state != 0 OR deleted = 1)) "
                             "OR EXISTS(SELECT 1 FROM routine_remote_deletes WHERE account_id=?)"));
    q.addBindValue(accountId);
    q.addBindValue(accountId);
    return exec(q) && q.next() && q.value(0).toBool();
}

// ---- Reminders ----

bool Database::reminderFired(const QString &uid, const QString &key)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT 1 FROM reminder_log WHERE task_uid=? AND trigger_key=?"));
    q.addBindValue(uid);
    q.addBindValue(key);
    return exec(q) && q.next();
}

void Database::setReminderFired(const QString &uid, const QString &key)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO reminder_log (task_uid, trigger_key, fired_at) VALUES (?,?,?)"));
    q.addBindValue(uid);
    q.addBindValue(key);
    q.addBindValue(tsToDb(QDateTime::currentDateTimeUtc()));
    exec(q);
}

QDateTime Database::snoozedUntil(const QString &uid)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT until FROM snoozes WHERE task_uid=?"));
    q.addBindValue(uid);
    if (exec(q) && q.next()) {
        return tsFromDb(q.value(0).toString());
    }
    return {};
}

void Database::setSnooze(const QString &uid, const QDateTime &until)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO snoozes (task_uid, until) VALUES (?,?)"));
    q.addBindValue(uid);
    q.addBindValue(tsToDb(until));
    exec(q);
}

void Database::clearSnooze(const QString &uid)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM snoozes WHERE task_uid=?"));
    q.addBindValue(uid);
    exec(q);
}

QList<QPair<QString, QDateTime>> Database::snoozes()
{
    QList<QPair<QString, QDateTime>> out;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT task_uid, until FROM snoozes"));
    if (exec(q)) {
        while (q.next()) {
            out.append({q.value(0).toString(), tsFromDb(q.value(1).toString())});
        }
    }
    return out;
}

void Database::pruneReminderLog(const QDateTime &olderThan)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM reminder_log WHERE fired_at < ?"));
    q.addBindValue(tsToDb(olderThan));
    exec(q);
}

// ---- Meta ----

QString Database::meta(const QString &key, const QString &def)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=?"));
    q.addBindValue(key);
    if (exec(q) && q.next()) {
        return q.value(0).toString();
    }
    return def;
}

void Database::setMeta(const QString &key, const QString &value)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (?,?)"));
    q.addBindValue(key);
    q.addBindValue(value);
    exec(q);
}

// ---- Journal ----

static const QString kJournalCols = QStringLiteral(
    "id, uid, title, content, entry_date, tags, mood, is_locked, kdf_salt, password_hash, "
    "encrypted_content, created_at, modified_at, deleted, notebook_id, sync_account_id, remote_id, etag, remote_modified, sync_state");

QList<JournalNotebook> Database::journalNotebooks()
{
    QList<JournalNotebook> out;
    QSqlQuery q(handle());
    if (q.exec(QStringLiteral("SELECT id, name, color, icon, sort_order, created_at, sync_account_id FROM journal_notebooks ORDER BY sort_order ASC, id ASC"))) {
        while (q.next()) {
            JournalNotebook nb;
            nb.id = q.value(0).toLongLong();
            nb.name = q.value(1).toString();
            nb.color = q.value(2).toString();
            nb.icon = q.value(3).toString();
            nb.sortOrder = q.value(4).toDouble();
            nb.created = tsFromDb(q.value(5).toString());
            nb.syncAccountId = q.value(6).toLongLong();
            out.append(nb);
        }
    }
    return out;
}

bool Database::insertJournalNotebook(JournalNotebook &nb)
{
    if (!nb.created.isValid()) {
        nb.created = QDateTime::currentDateTimeUtc();
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO journal_notebooks (name, color, icon, sort_order, created_at, sync_account_id) VALUES (?,?,?,?,?,?)"));
    q.addBindValue(nb.name);
    q.addBindValue(nb.color);
    q.addBindValue(nb.icon);
    q.addBindValue(nb.sortOrder);
    q.addBindValue(tsToDb(nb.created));
    q.addBindValue(nb.syncAccountId);
    if (!exec(q)) {
        return false;
    }
    nb.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::updateJournalNotebook(const JournalNotebook &nb)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE journal_notebooks SET name=?, color=?, icon=?, sort_order=?, sync_account_id=? WHERE id=?"));
    q.addBindValue(nb.name);
    q.addBindValue(nb.color);
    q.addBindValue(nb.icon);
    q.addBindValue(nb.sortOrder);
    q.addBindValue(nb.syncAccountId);
    q.addBindValue(nb.id);
    return exec(q);
}

bool Database::removeJournalNotebook(qint64 id)
{
    Transaction tx(this);
    QSqlQuery q0(handle());
    q0.prepare(QStringLiteral("INSERT OR IGNORE INTO journal_remote_deletes (account_id, remote_id) "
                              "SELECT sync_account_id, remote_id FROM journal_entries "
                              "WHERE notebook_id=? AND sync_account_id > 0 AND remote_id > 0"));
    q0.addBindValue(id);
    if (!exec(q0)) {
        return false;
    }
    QSqlQuery q1(handle());
    q1.prepare(QStringLiteral("DELETE FROM journal_entries WHERE notebook_id=?"));
    q1.addBindValue(id);
    if (!exec(q1)) {
        return false;
    }
    QSqlQuery q2(handle());
    q2.prepare(QStringLiteral("DELETE FROM journal_notebooks WHERE id=?"));
    q2.addBindValue(id);
    return exec(q2) && tx.commit();
}

JournalEntry Database::journalFromQuery(const QSqlQuery &q)
{
    JournalEntry e;
    e.id = q.value(0).toLongLong();
    e.uid = q.value(1).toString();
    e.title = q.value(2).toString();
    e.content = q.value(3).toString();
    e.entryDate = QDate::fromString(q.value(4).toString(), Qt::ISODate);
    const QJsonArray arr = QJsonDocument::fromJson(q.value(5).toString().toUtf8()).array();
    for (const QJsonValue &v : arr) {
        const QString s = v.toString();
        if (!s.isEmpty()) {
            e.tags.append(s);
        }
    }
    e.mood = q.value(6).toString();
    e.isLocked = q.value(7).toBool();
    e.kdfSalt = q.value(8).toString();
    e.passwordHash = q.value(9).toString();
    e.encryptedContent = q.value(10).toString();
    e.created = tsFromDb(q.value(11).toString());
    e.modified = tsFromDb(q.value(12).toString());
    e.deleted = q.value(13).toBool();
    e.notebookId = q.value(14).toLongLong();
    e.syncAccountId = q.value(15).toLongLong();
    e.remoteId = q.value(16).toLongLong();
    e.etag = q.value(17).toString();
    e.remoteModified = q.value(18).toLongLong();
    e.syncState = static_cast<SyncState>(q.value(19).toInt());
    return e;
}

QList<JournalEntry> Database::journalEntries(bool includeDeleted)
{
    QList<JournalEntry> out;
    QSqlQuery q(handle());
    const QString sql = includeDeleted
        ? QStringLiteral("SELECT ") + kJournalCols + QStringLiteral(" FROM journal_entries ORDER BY entry_date DESC, id DESC")
        : QStringLiteral("SELECT ") + kJournalCols + QStringLiteral(" FROM journal_entries WHERE deleted=0 ORDER BY entry_date DESC, id DESC");
    if (q.exec(sql)) {
        while (q.next()) {
            out.append(journalFromQuery(q));
        }
    }
    return out;
}

std::optional<JournalEntry> Database::journalEntry(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT ") + kJournalCols + QStringLiteral(" FROM journal_entries WHERE id=?"));
    q.addBindValue(id);
    if (exec(q) && q.next()) {
        return journalFromQuery(q);
    }
    return std::nullopt;
}

std::optional<JournalEntry> Database::journalEntryByRemote(qint64 accountId, qint64 remoteId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT ") + kJournalCols + QStringLiteral(" FROM journal_entries WHERE sync_account_id=? AND remote_id=? LIMIT 1"));
    q.addBindValue(accountId);
    q.addBindValue(remoteId);
    if (exec(q) && q.next()) {
        return journalFromQuery(q);
    }
    return std::nullopt;
}

bool Database::insertJournalEntry(JournalEntry &e)
{
    if (e.uid.isEmpty()) {
        e.uid = newUid();
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!e.created.isValid()) {
        e.created = now;
    }
    if (!e.modified.isValid()) {
        e.modified = now;
    }
    if (!e.entryDate.isValid()) {
        e.entryDate = QDate::currentDate();
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
        "INSERT INTO journal_entries (uid, title, content, entry_date, tags, mood, is_locked, kdf_salt, password_hash, "
        "encrypted_content, created_at, modified_at, deleted, notebook_id, sync_account_id, remote_id, etag, remote_modified, sync_state) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(e.uid);
    q.addBindValue(e.title);
    q.addBindValue(e.isLocked ? QString() : e.content);
    q.addBindValue(e.entryDate.toString(Qt::ISODate));
    q.addBindValue(tagsToJson(e.tags));
    q.addBindValue(e.mood);
    q.addBindValue(e.isLocked ? 1 : 0);
    q.addBindValue(e.kdfSalt);
    q.addBindValue(e.passwordHash);
    q.addBindValue(e.encryptedContent);
    q.addBindValue(tsToDb(e.created));
    q.addBindValue(tsToDb(e.modified));
    q.addBindValue(e.deleted ? 1 : 0);
    q.addBindValue(e.notebookId > 0 ? e.notebookId : 1);
    q.addBindValue(e.syncAccountId);
    q.addBindValue(e.remoteId);
    q.addBindValue(e.etag);
    q.addBindValue(e.remoteModified);
    q.addBindValue(int(e.syncState));
    if (!exec(q)) {
        return false;
    }
    e.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::updateJournalEntry(const JournalEntry &e)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
        "UPDATE journal_entries SET title=?, content=?, entry_date=?, tags=?, mood=?, is_locked=?, kdf_salt=?, password_hash=?, "
        "encrypted_content=?, modified_at=?, deleted=?, notebook_id=?, sync_account_id=?, remote_id=?, etag=?, remote_modified=?, sync_state=? "
        "WHERE id=?"));
    q.addBindValue(e.title);
    q.addBindValue(e.isLocked ? QString() : e.content);
    q.addBindValue(e.entryDate.isValid() ? e.entryDate.toString(Qt::ISODate) : QDate::currentDate().toString(Qt::ISODate));
    q.addBindValue(tagsToJson(e.tags));
    q.addBindValue(e.mood);
    q.addBindValue(e.isLocked ? 1 : 0);
    q.addBindValue(e.kdfSalt);
    q.addBindValue(e.passwordHash);
    q.addBindValue(e.encryptedContent);
    q.addBindValue(tsToDb(e.modified.isValid() ? e.modified : QDateTime::currentDateTimeUtc()));
    q.addBindValue(e.deleted ? 1 : 0);
    q.addBindValue(e.notebookId > 0 ? e.notebookId : 1);
    q.addBindValue(e.syncAccountId);
    q.addBindValue(e.remoteId);
    q.addBindValue(e.etag);
    q.addBindValue(e.remoteModified);
    q.addBindValue(int(e.syncState));
    q.addBindValue(e.id);
    return exec(q);
}

bool Database::removeJournalEntry(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM journal_entries WHERE id=?"));
    q.addBindValue(id);
    return exec(q);
}

bool Database::removeJournalEntryUser(qint64 id)
{
    Transaction tx(this);
    const auto cur = journalEntry(id);
    if (!cur) {
        return false;
    }
    if (cur->syncAccountId > 0 && cur->remoteId > 0) {
        if (!queueJournalRemoteDelete(cur->syncAccountId, cur->remoteId)) {
            return false;
        }
    }
    return removeJournalEntry(id) && tx.commit();
}

bool Database::queueJournalRemoteDelete(qint64 accountId, qint64 remoteId)
{
    if (accountId <= 0 || remoteId <= 0) {
        return true;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO journal_remote_deletes (account_id, remote_id) VALUES (?,?)"));
    q.addBindValue(accountId);
    q.addBindValue(remoteId);
    return exec(q);
}

QList<qint64> Database::journalRemoteDeletes(qint64 accountId)
{
    QList<qint64> out;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT remote_id FROM journal_remote_deletes WHERE account_id=?"));
    q.addBindValue(accountId);
    if (exec(q)) {
        while (q.next()) {
            out.append(q.value(0).toLongLong());
        }
    }
    return out;
}

bool Database::clearJournalRemoteDelete(qint64 accountId, qint64 remoteId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM journal_remote_deletes WHERE account_id=? AND remote_id=?"));
    q.addBindValue(accountId);
    q.addBindValue(remoteId);
    return exec(q);
}

// ---- Routine ----

static const QString kRoutineCols = QStringLiteral(
    "id, uid, title, notes, start_minute, duration_minutes, days_mask, specific_date, start_date, linked_task_id, color, category, "
    "notify_enabled, notify_minutes_before, active, sort_order, created_at, modified_at, "
    "sync_account_id, calendar_href, href, etag, base_ics, sync_state, rev, deleted");

RoutineBlock Database::routineBlockFromQuery(const QSqlQuery &q)
{
    RoutineBlock b;
    b.id = q.value(0).toLongLong();
    b.uid = q.value(1).toString();
    b.title = q.value(2).toString();
    b.notes = q.value(3).toString();
    b.startMinute = q.value(4).toInt();
    b.durationMinutes = q.value(5).toInt();
    b.daysMask = q.value(6).toInt();
    const QString sd = q.value(7).toString();
    b.specificDate = sd.isEmpty() ? QDate() : QDate::fromString(sd, Qt::ISODate);
    const QString startD = q.value(8).toString();
    b.startDate = startD.isEmpty() ? QDate() : QDate::fromString(startD, Qt::ISODate);
    b.linkedTaskId = q.value(9).toLongLong();
    b.color = q.value(10).toString();
    b.category = q.value(11).toString();
    b.notifyEnabled = q.value(12).toBool();
    b.notifyMinutesBefore = q.value(13).toInt();
    b.active = q.value(14).toBool();
    b.sortOrder = q.value(15).toDouble();
    b.created = tsFromDb(q.value(16).toString());
    b.modified = tsFromDb(q.value(17).toString());
    b.syncAccountId = q.value(18).toLongLong();
    b.calendarHref = q.value(19).toString();
    b.href = q.value(20).toString();
    b.etag = q.value(21).toString();
    b.baseIcs = q.value(22).toString();
    b.syncState = SyncState(q.value(23).toInt());
    b.rev = q.value(24).toLongLong();
    b.deleted = q.value(25).toBool();
    return b;
}

QList<RoutineBlock> Database::routineBlocks(bool includeInactive, bool includeDeleted, bool includeBaseIcs)
{
    QList<RoutineBlock> out;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    QString cols = kRoutineCols;
    if (!includeBaseIcs) {
        cols.replace(QStringLiteral("base_ics"), QStringLiteral("''"));
    }
    QStringList where;
    if (!includeDeleted) {
        where << QStringLiteral("deleted=0 AND sync_state!=3");
    }
    if (!includeInactive) {
        where << QStringLiteral("active=1");
    }
    const QString clause = where.isEmpty() ? QString() : (QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND ")));
    const QString sql = QStringLiteral("SELECT ") + cols + QStringLiteral(" FROM routine_blocks") + clause
        + QStringLiteral(" ORDER BY start_minute ASC, sort_order ASC, id ASC");
    if (q.exec(sql)) {
        while (q.next()) {
            out.append(routineBlockFromQuery(q));
        }
    }
    return out;
}

std::optional<RoutineBlock> Database::routineBlock(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT ") + kRoutineCols + QStringLiteral(" FROM routine_blocks WHERE id=?"));
    q.addBindValue(id);
    if (exec(q) && q.next()) {
        return routineBlockFromQuery(q);
    }
    return std::nullopt;
}

std::optional<RoutineBlock> Database::routineBlockByHref(qint64 accountId, const QString &href)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT ") + kRoutineCols + QStringLiteral(" FROM routine_blocks WHERE sync_account_id=? AND href=? LIMIT 1"));
    q.addBindValue(accountId);
    q.addBindValue(href);
    if (exec(q) && q.next()) {
        return routineBlockFromQuery(q);
    }
    return std::nullopt;
}

std::optional<RoutineBlock> Database::routineBlockByUid(qint64 accountId, const QString &uid)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT ") + kRoutineCols + QStringLiteral(" FROM routine_blocks WHERE sync_account_id=? AND uid=? LIMIT 1"));
    q.addBindValue(accountId);
    q.addBindValue(uid);
    if (exec(q) && q.next()) {
        return routineBlockFromQuery(q);
    }
    return std::nullopt;
}

bool Database::insertRoutineBlock(RoutineBlock &b)
{
    if (b.uid.isEmpty()) {
        b.uid = newUid();
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!b.created.isValid()) {
        b.created = now;
    }
    if (!b.modified.isValid()) {
        b.modified = now;
    }
    if (b.syncAccountId > 0 && b.href.isEmpty() && b.syncState == SyncState::Synced) {
        b.syncState = SyncState::New;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
        "INSERT INTO routine_blocks (uid, title, notes, start_minute, duration_minutes, days_mask, specific_date, start_date, linked_task_id, color, category, notify_enabled, notify_minutes_before, active, sort_order, created_at, modified_at, sync_account_id, calendar_href, href, etag, base_ics, sync_state, rev, deleted) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(b.uid);
    q.addBindValue(b.title);
    q.addBindValue(b.notes);
    q.addBindValue(qBound(0, b.startMinute, 1439));
    q.addBindValue(qBound(5, b.durationMinutes, 1440));
    q.addBindValue(b.daysMask);
    q.addBindValue(b.specificDate.isValid() ? b.specificDate.toString(Qt::ISODate) : QString());
    q.addBindValue(b.startDate.isValid() ? b.startDate.toString(Qt::ISODate) : QString());
    q.addBindValue(b.linkedTaskId);
    q.addBindValue(b.color);
    q.addBindValue(b.category);
    q.addBindValue(b.notifyEnabled ? 1 : 0);
    q.addBindValue(qMax(0, b.notifyMinutesBefore));
    q.addBindValue(b.active ? 1 : 0);
    q.addBindValue(b.sortOrder);
    q.addBindValue(tsToDb(b.created));
    q.addBindValue(tsToDb(b.modified));
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.calendarHref);
    q.addBindValue(b.href);
    q.addBindValue(b.etag);
    q.addBindValue(b.baseIcs);
    q.addBindValue(int(b.syncState));
    q.addBindValue(b.rev);
    q.addBindValue(b.deleted ? 1 : 0);
    if (!exec(q)) {
        return false;
    }
    b.id = q.lastInsertId().toLongLong();
    return true;
}

bool Database::updateRoutineBlock(const RoutineBlock &b)
{
    return updateRoutineBlockUser(b);
}

bool Database::updateRoutineBlockUser(const RoutineBlock &b)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
        "UPDATE routine_blocks SET title=?, notes=?, start_minute=?, duration_minutes=?, days_mask=?, specific_date=?, start_date=?, linked_task_id=?, color=?, category=?, notify_enabled=?, notify_minutes_before=?, active=?, sort_order=?, modified_at=?, sync_account_id=?, "
        "calendar_href = CASE WHEN ? = 0 THEN '' ELSE calendar_href END, "
        "href = CASE WHEN ? = 0 THEN '' ELSE href END, "
        "etag = CASE WHEN ? = 0 THEN '' ELSE etag END, "
        "base_ics = CASE WHEN ? = 0 THEN '' ELSE base_ics END, "
        "rev = rev + 1, "
        "sync_state = CASE WHEN ? = 0 THEN 0 WHEN href = '' THEN 1 ELSE 2 END "
        "WHERE id=?"));
    q.addBindValue(b.title);
    q.addBindValue(b.notes);
    q.addBindValue(qBound(0, b.startMinute, 1439));
    q.addBindValue(qBound(5, b.durationMinutes, 1440));
    q.addBindValue(b.daysMask);
    q.addBindValue(b.specificDate.isValid() ? b.specificDate.toString(Qt::ISODate) : QString());
    q.addBindValue(b.startDate.isValid() ? b.startDate.toString(Qt::ISODate) : QString());
    q.addBindValue(b.linkedTaskId);
    q.addBindValue(b.color);
    q.addBindValue(b.category);
    q.addBindValue(b.notifyEnabled ? 1 : 0);
    q.addBindValue(qMax(0, b.notifyMinutesBefore));
    q.addBindValue(b.active ? 1 : 0);
    q.addBindValue(b.sortOrder);
    q.addBindValue(tsToDb(QDateTime::currentDateTimeUtc()));
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.id);
    return exec(q);
}

bool Database::updateRoutineBlockFull(const RoutineBlock &b)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
        "UPDATE routine_blocks SET title=?, notes=?, start_minute=?, duration_minutes=?, days_mask=?, specific_date=?, start_date=?, linked_task_id=?, color=?, category=?, notify_enabled=?, notify_minutes_before=?, active=?, sort_order=?, modified_at=?, "
        "sync_account_id=?, calendar_href=?, href=?, etag=?, base_ics=?, sync_state=?, rev=?, deleted=? WHERE id=?"));
    q.addBindValue(b.title);
    q.addBindValue(b.notes);
    q.addBindValue(qBound(0, b.startMinute, 1439));
    q.addBindValue(qBound(5, b.durationMinutes, 1440));
    q.addBindValue(b.daysMask);
    q.addBindValue(b.specificDate.isValid() ? b.specificDate.toString(Qt::ISODate) : QString());
    q.addBindValue(b.startDate.isValid() ? b.startDate.toString(Qt::ISODate) : QString());
    q.addBindValue(b.linkedTaskId);
    q.addBindValue(b.color);
    q.addBindValue(b.category);
    q.addBindValue(b.notifyEnabled ? 1 : 0);
    q.addBindValue(qMax(0, b.notifyMinutesBefore));
    q.addBindValue(b.active ? 1 : 0);
    q.addBindValue(b.sortOrder);
    q.addBindValue(tsToDb(b.modified.isValid() ? b.modified : QDateTime::currentDateTimeUtc()));
    q.addBindValue(b.syncAccountId);
    q.addBindValue(b.calendarHref);
    q.addBindValue(b.href);
    q.addBindValue(b.etag);
    q.addBindValue(b.baseIcs);
    q.addBindValue(int(b.syncState));
    q.addBindValue(b.rev);
    q.addBindValue(b.deleted ? 1 : 0);
    q.addBindValue(b.id);
    return exec(q);
}

bool Database::markRoutineUploaded(qint64 id, qint64 revAtUpload, const QString &calendarHref, const QString &href, const QString &etag, const QString &ics)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE routine_blocks SET calendar_href=?, href=?, etag=?, base_ics=?, "
                              "sync_state = CASE WHEN sync_state = 3 THEN 3 WHEN rev = ? THEN 0 ELSE 2 END WHERE id=?"));
    q.addBindValue(calendarHref);
    q.addBindValue(href);
    q.addBindValue(etag);
    q.addBindValue(ics);
    q.addBindValue(revAtUpload);
    q.addBindValue(id);
    return exec(q);
}

bool Database::removeRoutineBlock(qint64 id)
{
    Transaction tx(this);
    QSqlQuery q1(handle());
    q1.prepare(QStringLiteral("DELETE FROM routine_logs WHERE block_id=?"));
    q1.addBindValue(id);
    if (!exec(q1)) {
        return false;
    }
    QSqlQuery q2(handle());
    q2.prepare(QStringLiteral("DELETE FROM routine_blocks WHERE id=?"));
    q2.addBindValue(id);
    return exec(q2) && tx.commit();
}

bool Database::removeRoutineBlockUser(qint64 id)
{
    const auto cur = routineBlock(id);
    if (!cur) {
        return false;
    }
    if (cur->syncAccountId == 0 || cur->href.isEmpty()) {
        return removeRoutineBlock(id);
    }
    Transaction tx(this);
    QSqlQuery q1(handle());
    q1.prepare(QStringLiteral("DELETE FROM routine_logs WHERE block_id=?"));
    q1.addBindValue(id);
    if (!exec(q1)) {
        return false;
    }
    QSqlQuery q2(handle());
    q2.prepare(QStringLiteral("UPDATE routine_blocks SET deleted=1, sync_state=3 WHERE id=?"));
    q2.addBindValue(id);
    return exec(q2) && tx.commit();
}

bool Database::queueRoutineRemoteDelete(qint64 accountId, const QString &href, const QString &etag)
{
    if (accountId <= 0 || href.isEmpty()) {
        return true;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO routine_remote_deletes (account_id, href, etag) VALUES (?,?,?)"));
    q.addBindValue(accountId);
    q.addBindValue(href);
    q.addBindValue(etag);
    return exec(q);
}

QList<QPair<QString, QString>> Database::routineRemoteDeletes(qint64 accountId)
{
    QList<QPair<QString, QString>> out;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT href, etag FROM routine_remote_deletes WHERE account_id=?"));
    q.addBindValue(accountId);
    if (exec(q)) {
        while (q.next()) {
            out.append({q.value(0).toString(), q.value(1).toString()});
        }
    }
    return out;
}

bool Database::clearRoutineRemoteDelete(qint64 accountId, const QString &href)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM routine_remote_deletes WHERE account_id=? AND href=?"));
    q.addBindValue(accountId);
    q.addBindValue(href);
    return exec(q);
}

QList<RoutineLog> Database::routineLogsForBlock(qint64 blockId, const QDate &since)
{
    QList<RoutineLog> out;
    QSqlQuery q(handle());
    if (since.isValid()) {
        q.prepare(QStringLiteral("SELECT block_id, log_date, status, completed_at FROM routine_logs WHERE block_id=? AND log_date >= ? ORDER BY log_date DESC"));
        q.addBindValue(blockId);
        q.addBindValue(since.toString(Qt::ISODate));
    } else {
        q.prepare(QStringLiteral("SELECT block_id, log_date, status, completed_at FROM routine_logs WHERE block_id=? ORDER BY log_date DESC"));
        q.addBindValue(blockId);
    }
    if (exec(q)) {
        while (q.next()) {
            RoutineLog l;
            l.blockId = q.value(0).toLongLong();
            l.logDate = QDate::fromString(q.value(1).toString(), Qt::ISODate);
            l.status = RoutineLogStatus(q.value(2).toInt());
            l.completedAt = tsFromDb(q.value(3).toString());
            out.append(l);
        }
    }
    return out;
}

QList<RoutineLog> Database::routineLogsForDate(const QDate &date)
{
    QList<RoutineLog> out;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT block_id, log_date, status, completed_at FROM routine_logs WHERE log_date=?"));
    q.addBindValue(date.toString(Qt::ISODate));
    if (exec(q)) {
        while (q.next()) {
            RoutineLog l;
            l.blockId = q.value(0).toLongLong();
            l.logDate = QDate::fromString(q.value(1).toString(), Qt::ISODate);
            l.status = RoutineLogStatus(q.value(2).toInt());
            l.completedAt = tsFromDb(q.value(3).toString());
            out.append(l);
        }
    }
    return out;
}

bool Database::setRoutineLog(qint64 blockId, const QDate &date, RoutineLogStatus status)
{
    QSqlQuery q(handle());
    if (status == RoutineLogStatus::Pending) {
        q.prepare(QStringLiteral("DELETE FROM routine_logs WHERE block_id=? AND log_date=?"));
        q.addBindValue(blockId);
        q.addBindValue(date.toString(Qt::ISODate));
        return exec(q);
    }
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO routine_logs (block_id, log_date, status, completed_at) VALUES (?,?,?,?)"));
    q.addBindValue(blockId);
    q.addBindValue(date.toString(Qt::ISODate));
    q.addBindValue(int(status));
    q.addBindValue(tsToDb(QDateTime::currentDateTimeUtc()));
    return exec(q);
}

// ---- Focus Sessions ----

bool Database::insertFocusSession(FocusSession &s)
{
    if (!s.endedAt.isValid()) {
        s.endedAt = QDateTime::currentDateTimeUtc();
    }
    if (!s.startedAt.isValid()) {
        s.startedAt = s.endedAt.addSecs(-qMax(0, s.durationSeconds));
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO focus_sessions (task_id, task_uid, task_title, list_id, quadrant, mode, "
                             "duration_seconds, completed_pomodoro, started_at, ended_at) VALUES (?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(s.taskId);
    q.addBindValue(s.taskUid);
    q.addBindValue(s.taskTitle);
    q.addBindValue(s.listId);
    q.addBindValue(qBound(0, s.quadrant, 4));
    q.addBindValue(int(s.mode));
    q.addBindValue(qMax(0, s.durationSeconds));
    q.addBindValue(s.completedPomodoro ? 1 : 0);
    q.addBindValue(tsToDb(s.startedAt));
    q.addBindValue(tsToDb(s.endedAt));
    if (!exec(q)) {
        return false;
    }
    s.id = q.lastInsertId().toLongLong();
    return true;
}

QList<FocusSession> Database::focusSessions(const QDateTime &since, int limit)
{
    QList<FocusSession> out;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    if (since.isValid()) {
        q.prepare(QStringLiteral("SELECT id, task_id, task_uid, task_title, list_id, quadrant, mode, duration_seconds, completed_pomodoro, started_at, ended_at "
                                 "FROM focus_sessions WHERE ended_at >= ? ORDER BY ended_at DESC, id DESC LIMIT ?"));
        q.addBindValue(tsToDb(since));
        q.addBindValue(qMax(1, limit));
    } else {
        q.prepare(QStringLiteral("SELECT id, task_id, task_uid, task_title, list_id, quadrant, mode, duration_seconds, completed_pomodoro, started_at, ended_at "
                                 "FROM focus_sessions ORDER BY ended_at DESC, id DESC LIMIT ?"));
        q.addBindValue(qMax(1, limit));
    }
    if (exec(q)) {
        while (q.next()) {
            FocusSession s;
            s.id = q.value(0).toLongLong();
            s.taskId = q.value(1).toLongLong();
            s.taskUid = q.value(2).toString();
            s.taskTitle = q.value(3).toString();
            s.listId = q.value(4).toLongLong();
            s.quadrant = q.value(5).toInt();
            s.mode = FocusMode(q.value(6).toInt());
            s.durationSeconds = q.value(7).toInt();
            s.completedPomodoro = q.value(8).toBool();
            s.startedAt = tsFromDb(q.value(9).toString());
            s.endedAt = tsFromDb(q.value(10).toString());
            out.append(s);
        }
    }
    return out;
}

bool Database::removeFocusSession(qint64 id)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM focus_sessions WHERE id=?"));
    q.addBindValue(id);
    return exec(q);
}

// ---- Backup & Restore ----

bool Database::backupTo(const QString &destPath, QString *error)
{
    if (!isOpen()) {
        m_lastError = QStringLiteral("database is not open");
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    const QFileInfo fi(destPath);
    QDir().mkpath(fi.absolutePath());
    const QString tmp = destPath + QStringLiteral(".tmp");
    QFile::remove(tmp);

    QSqlQuery q(handle());
    q.prepare(QStringLiteral("VACUUM INTO ?"));
    q.addBindValue(tmp);
    if (!exec(q)) {
        QFile::remove(tmp);
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    if (!validateBackup(tmp, error)) {
        QFile::remove(tmp);
        return false;
    }
    QFile::remove(destPath);
    if (!QFile::rename(tmp, destPath)) {
        m_lastError = QStringLiteral("could not move backup file to %1").arg(destPath);
        QFile::remove(tmp);
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    qCInfo(lcDb) << "backup created at" << destPath;
    return true;
}

bool Database::validateBackup(const QString &filePath, QString *error)
{
    if (!QFileInfo::exists(filePath)) {
        if (error) {
            *error = QStringLiteral("backup file does not exist");
        }
        return false;
    }
    const QString conn = QStringLiteral("ktasks-verify-%1").arg(newUid());
    bool valid = false;
    QString errMsg;
    {
        QSqlDatabase check = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        check.setDatabaseName(filePath);
        check.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!check.open()) {
            errMsg = check.lastError().text();
        } else {
            QSqlQuery q(check);
            if (!q.exec(QStringLiteral("PRAGMA integrity_check")) || !q.next() || q.value(0).toString().toLower() != QLatin1String("ok")) {
                errMsg = QStringLiteral("backup integrity check failed");
            } else {
                QSqlQuery t(check);
                QStringList tables;
                if (t.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table'"))) {
                    while (t.next()) {
                        tables.append(t.value(0).toString());
                    }
                }
                if (!tables.contains(QStringLiteral("tasks")) || !tables.contains(QStringLiteral("lists")) || !tables.contains(QStringLiteral("meta"))) {
                    errMsg = QStringLiteral("backup is missing required tables");
                } else {
                    QSqlQuery v(check);
                    int ver = 0;
                    if (v.exec(QStringLiteral("PRAGMA user_version")) && v.next()) {
                        ver = v.value(0).toInt();
                    }
                    if (ver <= 0 || ver > latestSchemaVersion()) {
                        errMsg = QStringLiteral("unsupported backup schema version %1").arg(ver);
                    } else {
                        valid = true;
                    }
                }
            }
            check.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    if (!valid && error) {
        *error = errMsg;
    }
    return valid;
}

bool Database::restoreFrom(const QString &sourcePath, QString *error)
{
    if (!validateBackup(sourcePath, error)) {
        return false;
    }
    if (isOpen()) {
        // Safety copy of current data before replacing.
        backupBeforeMigration(schemaVersion());
        {
            QSqlDatabase db = QSqlDatabase::database(m_connName, false);
            if (db.isValid()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connName);
    }

    const QString tmp = m_path + QStringLiteral(".restore.tmp");
    QFile::remove(tmp);
    if (!QFile::copy(sourcePath, tmp)) {
        m_lastError = QStringLiteral("could not copy backup file");
        if (error) {
            *error = m_lastError;
        }
        open(nullptr);
        return false;
    }
    QFile::remove(m_path + QStringLiteral("-wal"));
    QFile::remove(m_path + QStringLiteral("-shm"));
    QFile::remove(m_path);
    if (!QFile::rename(tmp, m_path)) {
        m_lastError = QStringLiteral("could not replace database file");
        QFile::remove(tmp);
        if (error) {
            *error = m_lastError;
        }
        return false;
    }
    return open(error);
}

}
