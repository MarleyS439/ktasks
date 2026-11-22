// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskstore.h"
#include "logging.h"
#include "recurrence.h"

#include <KLocalizedString>
#include <QSet>
#include <QSqlQuery>

using namespace KTasks;

static const int kMaxUndo = 100;

static QString childKey(qint64 listId, const QString &parentUid)
{
    return QString::number(listId) + QLatin1Char('/') + parentUid;
}

TaskStore::TaskStore(Database *db, QObject *parent)
    : QObject(parent)
    , m_db(db)
{
    reload();
}

void TaskStore::reload()
{
    m_tasks = m_db->tasks(false, false);
    m_lists = m_db->lists(false);
    m_accounts = m_db->accounts();
    rebuildIndex();
    Q_EMIT changed();
}

void TaskStore::rebuildIndex()
{
    m_taskIndex.clear();
    m_children.clear();
    m_taskIndex.reserve(m_tasks.size());
    for (int i = 0; i < m_tasks.size(); ++i) {
        const Task &t = m_tasks.at(i);
        m_taskIndex.insert(t.id, i);
        if (!t.parentUid.isEmpty()) {
            m_children[childKey(t.listId, t.parentUid)].append(i);
        }
    }
}

const Task *TaskStore::task(qint64 id) const
{
    const auto it = m_taskIndex.constFind(id);
    return it == m_taskIndex.constEnd() ? nullptr : &m_tasks.at(*it);
}

const Task *TaskStore::taskByUid(qint64 listId, const QString &uid) const
{
    if (uid.isEmpty()) {
        return nullptr;
    }
    for (const Task &t : m_tasks) {
        if (t.listId == listId && t.uid == uid) {
            return &t;
        }
    }
    return nullptr;
}

const TaskList *TaskStore::list(qint64 id) const
{
    for (const TaskList &l : m_lists) {
        if (l.id == id) {
            return &l;
        }
    }
    return nullptr;
}

const Account *TaskStore::account(qint64 id) const
{
    for (const Account &a : m_accounts) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

QList<const Task *> TaskStore::childrenOf(const Task &parent) const
{
    QList<const Task *> out;
    const auto it = m_children.constFind(childKey(parent.listId, parent.uid));
    if (it != m_children.constEnd()) {
        for (int i : *it) {
            out.append(&m_tasks.at(i));
        }
    }
    return out;
}

QList<const Task *> TaskStore::descendantsOf(const Task &parent) const
{
    QList<const Task *> out;
    QSet<qint64> seen{parent.id}; // guards against cycles in foreign data
    QList<const Task *> queue = childrenOf(parent);
    while (!queue.isEmpty()) {
        const Task *t = queue.takeFirst();
        if (seen.contains(t->id)) {
            continue;
        }
        seen.insert(t->id);
        out.append(t);
        queue.append(childrenOf(*t));
    }
    return out;
}

QStringList TaskStore::allTags() const
{
    QStringList tags;
    for (const Task &t : m_tasks) {
        for (const QString &tag : t.tags) {
            if (!tags.contains(tag, Qt::CaseInsensitive)) {
                tags.append(tag);
            }
        }
    }
    tags.sort(Qt::CaseInsensitive);
    return tags;
}

qint64 TaskStore::fallbackListId(qint64 preferred)
{
    if (const TaskList *l = list(preferred); l && !l->readOnly) {
        return l->id;
    }
    for (const TaskList &l : m_lists) {
        if (!l.readOnly) {
            return l.id;
        }
    }
    return addList(i18nc("name of the default task list", "Tasks"), QString(), 0);
}

void TaskStore::normalize(Task &t)
{
    t.title = t.title.trimmed();
    if (t.status == TaskStatus::Completed) {
        t.percent = 100;
        if (!t.completedAt.isValid()) {
            t.completedAt = QDateTime::currentDateTimeUtc();
        }
    } else {
        t.completedAt = QDateTime();
        t.percent = qBound(0, t.percent, 99);
        if (t.status == TaskStatus::NeedsAction && t.percent > 0) {
            t.status = TaskStatus::InProcess;
        } else if (t.status == TaskStatus::InProcess && t.percent == 0) {
            t.status = TaskStatus::NeedsAction;
        }
    }
    if (!t.due.isValid() && !t.start.isValid()) {
        t.allDay = true;
    }
    if (!t.rrule.isEmpty()) {
        // A recurring task needs an anchor date.
        if (!t.due.isValid() && !t.start.isValid()) {
            t.due = QDateTime(QDate::currentDate(), QTime(0, 0));
            t.allDay = true;
        }
        if (!t.start.isValid()) {
            t.start = t.due;
        }
    }
    if (t.start.isValid() && t.due.isValid() && t.start > t.due) {
        t.start = t.due;
    }
    // Reminders relative to a date make no sense without one.
    if (!t.due.isValid() && !t.start.isValid()) {
        t.alarms.removeIf([](const Alarm &a) {
            return a.kind != Alarm::Absolute;
        });
    }
}

// Brings the row with @p id to @p target (nullopt: deleted) through the same
// paths a user edit takes, so the sync bookkeeping stays correct.
bool TaskStore::writeState(qint64 id, std::optional<Task> &target)
{
    if (!target) {
        return id <= 0 || m_db->removeTaskUser(id);
    }
    Task &t = *target;
    const auto cur = id > 0 ? m_db->task(id) : std::nullopt;
    if (!cur) {
        // New, or brought back after its deletion was already synced.
        t.href.clear();
        t.etag.clear();
        t.baseIcs.clear();
        t.syncState = SyncState::New;
        t.rev = 0;
        t.id = 0;
        if (!m_db->insertTask(t)) {
            return false;
        }
        if (id > 0 && t.id != id) {
            QSqlQuery q(m_db->handle());
            q.prepare(QStringLiteral("UPDATE tasks SET id=? WHERE id=?"));
            q.addBindValue(id);
            q.addBindValue(t.id);
            if (q.exec()) {
                t.id = id;
            }
        }
        return true;
    }
    if (cur->listId != t.listId && !m_db->moveTask(id, t.listId)) {
        return false;
    }
    t.id = id;
    return m_db->updateTaskUser(t);
}

void TaskStore::commit(const QString &text, QList<Change> changes)
{
    if (changes.isEmpty()) {
        return;
    }
    {
        Database::Transaction tx(m_db);
        for (Change &c : changes) {
            const qint64 id = c.before ? c.before->id : (c.after ? c.after->id : 0);
            if (c.after) {
                normalize(*c.after);
                c.after->modified = QDateTime::currentDateTimeUtc();
            }
            if (!writeState(id, c.after)) {
                qCWarning(lcApp) << "could not save change:" << m_db->lastError();
                return; // rolls back
            }
        }
        if (!tx.commit()) {
            return;
        }
    }
    for (Change &c : changes) {
        if (c.before) {
            c.before->baseIcs = QString();
        }
        if (c.after) {
            c.after->baseIcs = QString();
        }
    }
    m_undo.append(Step{text, changes});
    while (m_undo.size() > kMaxUndo) {
        m_undo.removeFirst();
    }
    m_redo.clear();
    reload();
    Q_EMIT undoChanged();
    Q_EMIT localChange();
}

void TaskStore::applyStep(const Step &step, bool forward)
{
    {
        Database::Transaction tx(m_db);
        for (const Change &c : step.changes) {
            std::optional<Task> target = forward ? c.after : c.before;
            const qint64 id = c.before ? c.before->id : (c.after ? c.after->id : 0);
            if (target) {
                target->modified = QDateTime::currentDateTimeUtc();
            }
            writeState(id, target);
        }
        tx.commit();
    }
    reload();
    Q_EMIT undoChanged();
    Q_EMIT localChange();
}

QString TaskStore::undoText() const
{
    return m_undo.isEmpty() ? QString() : m_undo.last().text;
}

QString TaskStore::redoText() const
{
    return m_redo.isEmpty() ? QString() : m_redo.last().text;
}

void TaskStore::undo()
{
    if (m_undo.isEmpty()) {
        return;
    }
    const Step step = m_undo.takeLast();
    m_redo.append(step);
    applyStep(step, false);
}

void TaskStore::redo()
{
    if (m_redo.isEmpty()) {
        return;
    }
    const Step step = m_redo.takeLast();
    m_undo.append(step);
    applyStep(step, true);
}

// ---- tasks ----

qint64 TaskStore::addTask(Task task)
{
    task.id = 0;
    if (task.uid.isEmpty()) {
        task.uid = newUid();
    }
    task.listId = fallbackListId(task.listId);
    task.created = QDateTime::currentDateTimeUtc();
    // New tasks go to the end of the manual order.
    double order = defaultSortOrder(task.created);
    for (const Task &t : std::as_const(m_tasks)) {
        if (t.listId == task.listId && t.sortOrder >= order) {
            order = t.sortOrder + 1;
        }
    }
    task.sortOrder = order;
    const QString uid = task.uid;
    const qint64 listId = task.listId;
    commit(i18nc("undo step", "Add task"), {Change{std::nullopt, task}});
    const Task *added = taskByUid(listId, uid);
    if (added && !m_undo.isEmpty()) {
        // Record the id so that undo/redo address the same row.
        m_undo.last().changes.first().after->id = added->id;
    }
    return added ? added->id : 0;
}

void TaskStore::updateTask(const Task &task, const QString &what)
{
    updateTasks({task}, what);
}

void TaskStore::updateTasks(const QList<Task> &tasks, const QString &what)
{
    QList<Change> changes;
    for (const Task &t : tasks) {
        if (const Task *cur = task(t.id)) {
            changes.append(Change{*cur, t});
        }
    }
    commit(what, changes);
}

void TaskStore::appendCompletionChanges(const Task &cur, bool done, const QDateTime &now, QList<Change> &changes, QSet<qint64> &seen, bool *repeatedOut) const
{
    if (seen.contains(cur.id) || cur.isDone() == done) {
        return;
    }
    seen.insert(cur.id);
    if (done) {
        Task t = cur;
        bool repeated = false;
        if (!t.rrule.isEmpty()) {
            repeated = Recurrence::advance(t, QDateTime::currentDateTime());
            if (!repeated) {
                t.rrule.clear(); // last occurrence: complete it for good
            }
        }
        if (repeatedOut && repeated) {
            *repeatedOut = true;
        }
        if (repeated) {
            changes.append(Change{cur, t});
            for (const Task *child : descendantsOf(cur)) {
                if (child->isDone() && !seen.contains(child->id)) {
                    seen.insert(child->id);
                    Task c = *child;
                    c.status = TaskStatus::NeedsAction;
                    c.percent = 0;
                    changes.append(Change{*child, c});
                }
            }
            return;
        }
        t.status = TaskStatus::Completed;
        t.completedAt = now;
        changes.append(Change{cur, t});
        for (const Task *child : descendantsOf(cur)) {
            if (!child->isDone() && !seen.contains(child->id)) {
                seen.insert(child->id);
                Task c = *child;
                c.status = TaskStatus::Completed;
                c.completedAt = now;
                changes.append(Change{*child, c});
            }
        }
    } else {
        Task t = cur;
        t.status = TaskStatus::NeedsAction;
        t.percent = 0;
        changes.append(Change{cur, t});
        const Task *p = taskByUid(cur.listId, cur.parentUid);
        QSet<qint64> ancSeen{cur.id};
        while (p && !ancSeen.contains(p->id)) {
            ancSeen.insert(p->id);
            if (p->isDone() && !seen.contains(p->id)) {
                seen.insert(p->id);
                Task pt = *p;
                pt.status = TaskStatus::NeedsAction;
                pt.percent = 0;
                changes.append(Change{*p, pt});
            }
            p = taskByUid(p->listId, p->parentUid);
        }
    }
}

void TaskStore::setCompleted(qint64 id, bool done)
{
    setCompletedMany({id}, done);
}

void TaskStore::setCompletedMany(const QList<qint64> &ids, bool done)
{
    QList<Change> changes;
    QSet<qint64> seen;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    bool anyRepeated = false;
    for (qint64 id : ids) {
        if (const Task *cur = task(id)) {
            appendCompletionChanges(*cur, done, now, changes, seen, &anyRepeated);
        }
    }
    if (changes.isEmpty()) {
        return;
    }
    const QString what = !done
        ? (ids.size() > 1 ? i18nc("undo step", "Reopen tasks") : i18nc("undo step", "Reopen task"))
        : anyRepeated && ids.size() == 1
        ? i18nc("undo step", "Complete task (repeats)")
        : (ids.size() > 1 ? i18nc("undo step", "Complete tasks") : i18nc("undo step", "Complete task"));
    commit(what, changes);
}

void TaskStore::recordFocus(qint64 id, int addSeconds, int addPomodoros)
{
    const int idx = m_taskIndex.value(id, -1);
    if (idx < 0 || idx >= m_tasks.size() || (addSeconds <= 0 && addPomodoros <= 0)) {
        return;
    }
    Task t = m_tasks.at(idx);
    t.trackedSeconds += qMax(0, addSeconds);
    t.pomodorosCompleted += qMax(0, addPomodoros);
    t.modified = QDateTime::currentDateTimeUtc();
    if (!m_db->updateTaskUser(t)) {
        return;
    }
    const TaskList *l = list(t.listId);
    m_tasks[idx] = t;
    rebuildIndex();
    Q_EMIT changed();
    if (l && l->accountId > 0) {
        Q_EMIT localChange();
    }
}

void TaskStore::removeTask(qint64 id)
{
    removeTasks({id}, i18nc("undo step", "Delete task"));
}

void TaskStore::removeTasks(const QList<qint64> &ids, const QString &what)
{
    QList<Change> changes;
    QSet<qint64> queued;
    for (qint64 id : ids) {
        if (queued.contains(id)) {
            continue;
        }
        const Task *cur = task(id);
        if (!cur) {
            continue;
        }
        queued.insert(id);
        changes.append(Change{*cur, std::nullopt});
        for (const Task *child : descendantsOf(*cur)) {
            if (!queued.contains(child->id)) {
                queued.insert(child->id);
                changes.append(Change{*child, std::nullopt});
            }
        }
    }
    if (!changes.isEmpty()) {
        commit(what, changes);
    }
}

void TaskStore::moveToList(qint64 id, qint64 listId)
{
    moveToListMany({id}, listId);
}

void TaskStore::moveToListMany(const QList<qint64> &ids, qint64 listId)
{
    if (!list(listId)) {
        return;
    }
    QList<Change> changes;
    QSet<qint64> seen;
    for (qint64 id : ids) {
        if (seen.contains(id)) {
            continue;
        }
        const Task *cur = task(id);
        if (!cur || cur->listId == listId) {
            continue;
        }
        seen.insert(id);
        Task t = *cur;
        t.listId = listId;
        t.parentUid.clear();
        changes.append(Change{*cur, t});
        for (const Task *child : descendantsOf(*cur)) {
            if (!seen.contains(child->id)) {
                seen.insert(child->id);
                Task c = *child;
                c.listId = listId;
                changes.append(Change{*child, c});
            }
        }
    }
    if (!changes.isEmpty()) {
        commit(ids.size() > 1 ? i18nc("undo step", "Move tasks") : i18nc("undo step", "Move task"), changes);
    }
}

int TaskStore::removeCompleted(qint64 listId)
{
    QList<Change> changes;
    for (const Task &t : std::as_const(m_tasks)) {
        if (t.isDone() && (listId <= 0 || t.listId == listId)) {
            changes.append(Change{t, std::nullopt});
        }
    }
    const int n = changes.size();
    commit(i18nc("undo step", "Delete completed tasks"), changes);
    return n;
}

int TaskStore::importTasks(QList<Task> tasks, qint64 listId)
{
    listId = fallbackListId(listId);
    QList<Change> changes;
    QHash<QString, QString> renamed;
    for (Task &t : tasks) {
        // Never overwrite an existing task: import duplicates under a new UID.
        if (t.uid.isEmpty() || taskByUid(listId, t.uid)) {
            const QString fresh = newUid();
            renamed.insert(t.uid, fresh);
            t.uid = fresh;
        }
    }
    for (Task &t : tasks) {
        if (renamed.contains(t.parentUid)) {
            t.parentUid = renamed.value(t.parentUid);
        }
        t.id = 0;
        t.listId = listId;
        if (!t.created.isValid()) {
            t.created = QDateTime::currentDateTimeUtc();
        }
        changes.append(Change{std::nullopt, t});
    }
    const int n = changes.size();
    commit(i18nc("undo step", "Import tasks"), changes);
    return n;
}

void TaskStore::setCollapsed(qint64 id, bool collapsed)
{
    if (m_db->setCollapsed(id, collapsed)) {
        const auto it = m_taskIndex.constFind(id);
        if (it != m_taskIndex.constEnd()) {
            m_tasks[*it].collapsed = collapsed;
        }
        Q_EMIT changed();
    }
}

// ---- lists ----

qint64 TaskStore::addList(const QString &name, const QString &color, qint64 accountId)
{
    TaskList l;
    l.name = name.trimmed();
    l.color = color;
    l.accountId = account(accountId) ? accountId : 0;
    l.syncState = SyncState::New;
    double order = 0;
    for (const TaskList &other : std::as_const(m_lists)) {
        order = qMax(order, other.sortOrder);
    }
    l.sortOrder = order + 1;
    if (!m_db->insertList(l)) {
        return 0;
    }
    reload();
    Q_EMIT localChange();
    return l.id;
}

void TaskStore::updateList(qint64 id, const QString &name, const QString &color)
{
    const TaskList *cur = list(id);
    if (!cur) {
        return;
    }
    TaskList l = *cur;
    l.name = name.trimmed();
    l.color = color;
    if (m_db->updateListUser(l)) {
        reload();
        Q_EMIT localChange();
    }
}

bool TaskStore::moveListToAccount(qint64 id, qint64 targetAccountId)
{
    if (!m_db->moveListToAccount(id, targetAccountId)) {
        return false;
    }
    m_undo.clear();
    m_redo.clear();
    reload();
    Q_EMIT undoChanged();
    Q_EMIT localChange();
    return true;
}

void TaskStore::removeList(qint64 id)
{
    if (!m_db->removeListUser(id)) {
        return;
    }
    // Undo steps that touch the removed tasks can no longer be applied.
    m_undo.clear();
    m_redo.clear();
    reload();
    Q_EMIT undoChanged();
    Q_EMIT localChange();
}
