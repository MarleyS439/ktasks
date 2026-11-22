// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskmodel.h"
#include "settings.h"

#include <KLocalizedString>
#include <QLocale>
#include <QSet>
#include <algorithm>

using namespace KTasks;

static QString foldForSearch(const QString &s)
{
    QString out;
    const QString d = s.normalized(QString::NormalizationForm_D);
    out.reserve(d.size());
    for (const QChar c : d) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out;
}

static QDate dueDate(const Task &t)
{
    if (!t.due.isValid()) {
        return {};
    }
    return t.allDay ? t.due.date() : t.due.toLocalTime().date();
}

static bool isOverdue(const Task &t, const QDateTime &now)
{
    if (!t.due.isValid() || t.isDone()) {
        return false;
    }
    return t.allDay ? t.due.date() < now.date() : t.due < now;
}

TaskModel::TaskModel(TaskStore *store, Settings *settings, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
    , m_settings(settings)
{
    connect(store, &TaskStore::changed, this, [this] {
        rebuild();
        Q_EMIT titleChanged();
    });
    m_view = settings->lastView();
    m_sortMode = qMax(0, settings->sortMode(m_view));
    rebuild();
}

QHash<int, QByteArray> TaskModel::roleNames() const
{
    return {
        {IdRole, "taskId"},
        {TitleRole, "title"},
        {DoneRole, "done"},
        {PriorityRole, "priority"},
        {DueTextRole, "dueText"},
        {OverdueRole, "overdue"},
        {DueTodayRole, "dueToday"},
        {ListNameRole, "listName"},
        {ListColorRole, "listColor"},
        {TagsRole, "tags"},
        {DepthRole, "depth"},
        {HasChildrenRole, "hasChildren"},
        {CollapsedRole, "collapsed"},
        {ChildCountRole, "childCount"},
        {ChildDoneRole, "childDone"},
        {RecurringRole, "recurring"},
        {HasReminderRole, "hasReminder"},
        {HasNotesRole, "hasNotes"},
        {SectionRole, "section"},
        {PercentRole, "percent"},
        {ReadOnlyRole, "readOnly"},
    };
}

int TaskModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QString TaskModel::formatDue(const Task &t, const QDateTime &now)
{
    if (!t.due.isValid()) {
        return {};
    }
    const QDate d = dueDate(t);
    const QDate today = now.date();
    const qint64 days = today.daysTo(d);
    const QLocale locale;
    QString text;
    if (days == 0) {
        text = i18nc("due date", "Today");
    } else if (days == 1) {
        text = i18nc("due date", "Tomorrow");
    } else if (days == -1) {
        text = i18nc("due date", "Yesterday");
    } else if (days > 1 && days < 7) {
        text = locale.toString(d, QStringLiteral("dddd"));
    } else if (d.year() == today.year()) {
        text = locale.toString(d, QStringLiteral("ddd, d MMM"));
    } else {
        text = locale.toString(d, QStringLiteral("d MMM yyyy"));
    }
    if (!t.allDay) {
        text += QLatin1Char(' ') + locale.toString(t.due.toLocalTime().time(), QLocale::ShortFormat);
    }
    return text;
}

QVariant TaskModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const Row &row = m_rows.at(index.row());
    const Task *t = m_store->task(row.id);
    if (!t) {
        return {};
    }
    const QDateTime now = QDateTime::currentDateTime();
    switch (role) {
    case IdRole:
        return t->id;
    case Qt::DisplayRole:
    case TitleRole:
        return t->title;
    case DoneRole:
        return t->isDone();
    case PriorityRole:
        return t->priorityLevel();
    case DueTextRole:
        return formatDue(*t, now);
    case OverdueRole:
        return isOverdue(*t, now);
    case DueTodayRole:
        return dueDate(*t) == now.date();
    case ListNameRole: {
        const TaskList *l = m_store->list(t->listId);
        return l ? l->name : QString();
    }
    case ListColorRole: {
        const TaskList *l = m_store->list(t->listId);
        return l ? l->color : QString();
    }
    case TagsRole:
        return t->tags;
    case DepthRole:
        return row.depth;
    case HasChildrenRole:
        return row.hasChildren;
    case CollapsedRole:
        return t->collapsed;
    case ChildCountRole:
        return row.childCount;
    case ChildDoneRole:
        return row.childDone;
    case RecurringRole:
        return !t->rrule.isEmpty();
    case HasReminderRole:
        return !t->alarms.isEmpty();
    case HasNotesRole:
        return !t->notes.trimmed().isEmpty();
    case SectionRole:
        return row.section;
    case PercentRole:
        return t->percent;
    case ReadOnlyRole: {
        const TaskList *l = m_store->list(t->listId);
        return l && l->readOnly;
    }
    }
    return {};
}

void TaskModel::setView(const QString &view)
{
    if (m_view == view) {
        return;
    }
    m_view = view;
    m_settings->setLastView(view);
    m_sortMode = qMax(0, m_settings->sortMode(view));
    beginResetModel();
    m_rows.clear();
    endResetModel();
    rebuild();
    Q_EMIT viewChanged();
    Q_EMIT titleChanged();
    Q_EMIT sortModeChanged();
    Q_EMIT canReorderChanged();
}

QString TaskModel::title() const
{
    if (!m_search.trimmed().isEmpty()) {
        return i18nc("page title", "Search results");
    }
    if (isListView()) {
        const TaskList *l = m_store->list(listId());
        return l ? l->name : i18nc("page title", "Tasks");
    }
    if (m_view.startsWith(QLatin1String("tag:"))) {
        return QLatin1Char('#') + m_view.mid(4);
    }
    if (m_view == QLatin1String("today")) {
        return i18nc("smart view", "Today");
    }
    if (m_view == QLatin1String("week")) {
        return i18nc("smart view", "Next 7 days");
    }
    if (m_view == QLatin1String("overdue")) {
        return i18nc("smart view", "Overdue");
    }
    if (m_view == QLatin1String("nodate")) {
        return i18nc("smart view", "No date");
    }
    if (m_view == QLatin1String("completed")) {
        return i18nc("smart view", "Completed");
    }
    return i18nc("smart view", "All tasks");
}

void TaskModel::setSearchText(const QString &text)
{
    if (m_search == text) {
        return;
    }
    m_search = text;
    rebuild();
    Q_EMIT searchTextChanged();
    Q_EMIT titleChanged();
    Q_EMIT canReorderChanged();
}

bool TaskModel::showCompleted() const
{
    return m_settings->showCompleted();
}

void TaskModel::setShowCompleted(bool show)
{
    if (m_settings->showCompleted() == show) {
        return;
    }
    m_settings->setShowCompleted(show);
    rebuild();
    Q_EMIT showCompletedChanged();
}

void TaskModel::setSortMode(int mode)
{
    if (m_sortMode == mode) {
        return;
    }
    m_sortMode = mode;
    m_settings->setSortMode(m_view, mode);
    rebuild();
    Q_EMIT sortModeChanged();
    Q_EMIT canReorderChanged();
}

bool TaskModel::readOnly() const
{
    const TaskList *l = isListView() ? m_store->list(listId()) : nullptr;
    return l && l->readOnly;
}

bool TaskModel::treeMode() const
{
    return isListView() && m_search.trimmed().isEmpty();
}

int TaskModel::effectiveSort() const
{
    if (m_sortMode != SortDefault) {
        return m_sortMode;
    }
    return isListView() ? SortManual : SortDue;
}

bool TaskModel::canReorder() const
{
    return treeMode() && effectiveSort() == SortManual && !readOnly();
}

bool TaskModel::matchesView(const Task &t, const QString &view, const QDateTime &now)
{
    if (view == QLatin1String("completed")) {
        return t.isDone();
    }
    if (view == QLatin1String("all")) {
        return true;
    }
    if (view.startsWith(QLatin1String("list:"))) {
        return t.listId == view.mid(5).toLongLong();
    }
    if (view.startsWith(QLatin1String("tag:"))) {
        return t.tags.contains(view.mid(4), Qt::CaseInsensitive);
    }
    const QDate d = dueDate(t);
    const QDate today = now.date();
    if (view == QLatin1String("nodate")) {
        return !d.isValid();
    }
    if (!d.isValid()) {
        return false;
    }
    if (view == QLatin1String("today")) {
        return d <= today;
    }
    if (view == QLatin1String("week")) {
        return d >= today && d <= today.addDays(6);
    }
    if (view == QLatin1String("overdue")) {
        return t.allDay ? d < today : t.due < now;
    }
    return false;
}

QString TaskModel::sectionFor(const Task &t, const QDateTime &now) const
{
    if (m_view == QLatin1String("completed")) {
        return QString();
    }
    if (t.isDone()) {
        return i18nc("section header", "Completed");
    }
    const int sort = effectiveSort();
    if (sort == SortPriority) {
        switch (t.priorityLevel()) {
        case 3:
            return i18nc("section header", "High priority");
        case 2:
            return i18nc("section header", "Medium priority");
        case 1:
            return i18nc("section header", "Low priority");
        default:
            return i18nc("section header", "No priority");
        }
    }
    if (sort != SortDue) {
        return QString();
    }
    const QDate d = dueDate(t);
    if (!d.isValid()) {
        return i18nc("section header", "No date");
    }
    const QDate today = now.date();
    if (isOverdue(t, now) && d < today) {
        return i18nc("section header", "Overdue");
    }
    if (d == today) {
        return i18nc("section header", "Today");
    }
    if (d == today.addDays(1)) {
        return i18nc("section header", "Tomorrow");
    }
    if (d < today.addDays(7)) {
        QString name = QLocale().toString(d, QStringLiteral("dddd, d MMM"));
        if (!name.isEmpty()) {
            name[0] = name.at(0).toUpper();
        }
        return name;
    }
    return i18nc("section header", "Later");
}

void TaskModel::rebuild()
{
    const QDateTime now = QDateTime::currentDateTime();
    const bool showDone = showCompleted() || m_view == QLatin1String("completed");
    const QString needle = foldForSearch(m_search.trimmed());
    const int sort = effectiveSort();
    const bool completedView = m_view == QLatin1String("completed");

    auto less = [&](const Task *a, const Task *b) {
        if (a->isDone() != b->isDone()) {
            return b->isDone();
        }
        if (completedView || (a->isDone() && b->isDone())) {
            if (a->completedAt != b->completedAt) {
                return a->completedAt > b->completedAt;
            }
        }
        auto byDue = [](const Task *x, const Task *y, bool &decided) {
            const QDateTime dx = x->effectiveDue(), dy = y->effectiveDue();
            decided = true;
            if (dx.isValid() != dy.isValid()) {
                return dx.isValid();
            }
            if (dx.isValid() && dx != dy) {
                return dx < dy;
            }
            decided = false;
            return false;
        };
        bool decided = false;
        switch (sort) {
        case SortPriority:
            if (a->priorityLevel() != b->priorityLevel()) {
                return a->priorityLevel() > b->priorityLevel();
            }
            break;
        case SortTitle: {
            const int c = QString::localeAwareCompare(a->title, b->title);
            if (c != 0) {
                return c < 0;
            }
            break;
        }
        case SortCreated:
            if (a->created != b->created) {
                return a->created > b->created;
            }
            break;
        case SortManual:
            if (a->sortOrder != b->sortOrder) {
                return a->sortOrder < b->sortOrder;
            }
            return a->id < b->id;
        default:
            break;
        }
        const bool r = byDue(a, b, decided);
        if (decided) {
            return r;
        }
        if (a->priorityLevel() != b->priorityLevel()) {
            return a->priorityLevel() > b->priorityLevel();
        }
        if (a->sortOrder != b->sortOrder) {
            return a->sortOrder < b->sortOrder;
        }
        return a->id < b->id;
    };

    QList<Row> rows;
    int hidden = 0;

    if (treeMode()) {
        const qint64 list = listId();
        QList<const Task *> roots;
        QSet<QString> uids;
        for (const Task &t : m_store->tasks()) {
            if (t.listId == list) {
                uids.insert(t.uid);
            }
        }
        for (const Task &t : m_store->tasks()) {
            if (t.listId != list) {
                continue;
            }
            if (t.isDone() && !showDone) {
                ++hidden;
            }
            if (t.parentUid.isEmpty() || !uids.contains(t.parentUid) || t.parentUid == t.uid) {
                roots.append(&t);
            }
        }
        std::sort(roots.begin(), roots.end(), less);
        QSet<qint64> visited;
        std::function<void(const Task *, int, const QString &)> visit = [&](const Task *t, int depth, const QString &section) {
            if (visited.contains(t->id) || (t->isDone() && !showDone)) {
                return;
            }
            visited.insert(t->id);
            QList<const Task *> children = m_store->childrenOf(*t);
            Row row;
            row.id = t->id;
            row.depth = depth;
            row.section = section;
            row.childCount = children.size();
            for (const Task *c : std::as_const(children)) {
                row.childDone += c->isDone();
            }
            row.hasChildren = showDone ? row.childCount > 0 : row.childCount > row.childDone;
            rows.append(row);
            if (t->collapsed) {
                return;
            }
            std::sort(children.begin(), children.end(), less);
            for (const Task *c : std::as_const(children)) {
                visit(c, depth + 1, section);
            }
        };
        for (const Task *root : std::as_const(roots)) {
            visit(root, 0, sectionFor(*root, now));
        }
    } else {
        QList<const Task *> matches;
        for (const Task &t : m_store->tasks()) {
            if (needle.isEmpty()) {
                if (!matchesView(t, m_view, now)) {
                    continue;
                }
            } else {
                bool found = foldForSearch(t.title).contains(needle) || foldForSearch(t.notes).contains(needle);
                for (const QString &tag : t.tags) {
                    found = found || foldForSearch(tag).contains(needle);
                }
                if (!found) {
                    continue;
                }
            }
            if (t.isDone() && !showDone) {
                ++hidden;
                continue;
            }
            matches.append(&t);
        }
        std::sort(matches.begin(), matches.end(), less);
        for (const Task *t : std::as_const(matches)) {
            Row row;
            row.id = t->id;
            row.section = sectionFor(*t, now);
            const auto children = m_store->childrenOf(*t);
            row.childCount = children.size();
            for (const Task *c : children) {
                row.childDone += c->isDone();
            }
            rows.append(row);
        }
    }

    const int oldCount = m_rows.size();
    const int oldHidden = m_hiddenCompleted;
    m_hiddenCompleted = hidden;
    setRows(rows);
    if (oldCount != m_rows.size() || oldHidden != hidden) {
        Q_EMIT countChanged();
    }
}

// Updates the rows with the smallest signals that describe the change, so
// the view keeps its scroll position and can animate.
void TaskModel::setRows(const QList<Row> &rows)
{
    QSet<qint64> wanted;
    for (const Row &r : rows) {
        wanted.insert(r.id);
    }
    for (int i = m_rows.size() - 1; i >= 0; --i) {
        if (!wanted.contains(m_rows.at(i).id)) {
            beginRemoveRows({}, i, i);
            m_rows.removeAt(i);
            endRemoveRows();
        }
    }
    QSet<qint64> present;
    for (const Row &r : std::as_const(m_rows)) {
        present.insert(r.id);
    }
    bool ordered = true;
    for (int i = 0; i < rows.size() && ordered; ++i) {
        if (i < m_rows.size() && m_rows.at(i).id == rows.at(i).id) {
            continue;
        }
        if (!present.contains(rows.at(i).id)) {
            beginInsertRows({}, i, i);
            m_rows.insert(i, rows.at(i));
            endInsertRows();
            present.insert(rows.at(i).id);
        } else {
            ordered = false; // rows changed places
        }
    }
    if (!ordered || m_rows.size() != rows.size()) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        return;
    }
    m_rows = rows;
    if (!m_rows.isEmpty()) {
        Q_EMIT dataChanged(index(0), index(m_rows.size() - 1));
    }
}

int TaskModel::indexOfTask(qlonglong id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

qlonglong TaskModel::taskIdAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).id : 0;
}

void TaskModel::toggleCollapsed(qlonglong id)
{
    if (const Task *t = m_store->task(id)) {
        m_store->setCollapsed(id, !t->collapsed);
    }
}

void TaskModel::move(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= m_rows.size() || to >= m_rows.size()) {
        return;
    }
    if (m_draggedFrom < 0) {
        m_draggedFrom = from;
        m_draggedId = m_rows.at(from).id;
    }
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) {
        return;
    }
    m_rows.move(from, to);
    endMoveRows();
}

void TaskModel::commitMove()
{
    const qint64 id = m_draggedId;
    m_draggedFrom = -1;
    m_draggedId = 0;
    const int row = indexOfTask(id);
    const Task *moved = m_store->task(id);
    if (row < 0 || !moved || !canReorder()) {
        rebuild();
        return;
    }
    // The dragged row's own subtasks are still listed where it came from; ignore them.
    QSet<qint64> own;
    for (const Task *d : m_store->descendantsOf(*moved)) {
        own.insert(d->id);
    }
    auto neighbour = [&](int from, int step) -> const Task * {
        for (int i = from; i >= 0 && i < m_rows.size(); i += step) {
            if (!own.contains(m_rows.at(i).id) && m_rows.at(i).id != id) {
                return m_store->task(m_rows.at(i).id);
            }
        }
        return nullptr;
    };
    const Task *prev = neighbour(row - 1, -1);
    const Task *next = neighbour(row + 1, +1);
    const int prevDepth = prev ? m_rows.at(indexOfTask(prev->id)).depth : -1;
    const int nextDepth = next ? m_rows.at(indexOfTask(next->id)).depth : -1;

    // Dropped between two rows: it becomes a sibling of the deeper one, so
    // dropping among the subtasks of X makes it a subtask of X.
    QString parentUid;
    if (next && nextDepth > 0 && nextDepth >= prevDepth) {
        parentUid = next->parentUid;
    } else if (prev && prevDepth > 0 && next && nextDepth > 0) {
        parentUid = prev->parentUid;
    } else if (prev && prevDepth > 0 && !next) {
        parentUid = moved->parentUid == prev->parentUid ? prev->parentUid : QString();
    }
    if (parentUid == moved->uid) {
        parentUid = moved->parentUid;
    }

    // New position: between the nearest siblings above and below.
    auto siblingFrom = [&](int from, int step) -> const Task * {
        for (int i = from; i >= 0 && i < m_rows.size(); i += step) {
            const Task *t = m_store->task(m_rows.at(i).id);
            if (!t || t->id == id || own.contains(t->id)) {
                continue;
            }
            const QString p = m_rows.at(i).depth == 0 ? QString() : t->parentUid;
            if (p == parentUid && t->isDone() == moved->isDone()) {
                return t;
            }
            if (m_rows.at(i).depth == 0 && !parentUid.isEmpty()) {
                break; // left the parent's block
            }
        }
        return nullptr;
    };
    const Task *above = siblingFrom(row - 1, -1);
    const Task *below = siblingFrom(row + 1, +1);

    QList<Task> updates;
    Task t = *moved;
    t.parentUid = parentUid;
    if (above && below) {
        if (below->sortOrder - above->sortOrder >= 2) {
            t.sortOrder = std::floor((above->sortOrder + below->sortOrder) / 2);
        } else {
            // No room left between the two: renumber the siblings.
            QList<const Task *> siblings;
            for (const Row &r : std::as_const(m_rows)) {
                const Task *s = m_store->task(r.id);
                if (s && (s->id == id || ((r.depth == 0 ? QString() : s->parentUid) == parentUid && !own.contains(s->id)))) {
                    siblings.append(s);
                }
            }
            double order = siblings.isEmpty() ? 0 : std::floor(m_store->task(siblings.first()->id)->sortOrder);
            for (const Task *s : std::as_const(siblings)) {
                Task u = s->id == id ? t : *s;
                u.sortOrder = order;
                order += 1024;
                if (s->id == id) {
                    t = u;
                } else if (u.sortOrder != s->sortOrder) {
                    updates.append(u);
                }
            }
        }
    } else if (above) {
        t.sortOrder = above->sortOrder + 1024;
    } else if (below) {
        t.sortOrder = below->sortOrder - 1024;
    }
    updates.prepend(t);
    if (t.parentUid == moved->parentUid && t.sortOrder == moved->sortOrder && updates.size() == 1) {
        rebuild();
        return;
    }
    m_store->updateTasks(updates, i18nc("undo step", "Reorder tasks"));
}
