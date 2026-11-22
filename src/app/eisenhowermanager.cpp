// SPDX-License-Identifier: GPL-3.0-or-later
#include "eisenhowermanager.h"
#include "focusmanager.h"
#include "settings.h"
#include "taskmodel.h"
#include "taskstore.h"

#include <KLocalizedString>
#include <algorithm>

using namespace KTasks;

bool MatrixTaskEntry::operator==(const MatrixTaskEntry &other) const
{
    return id == other.id
        && title == other.title
        && notes == other.notes
        && done == other.done
        && priority == other.priority
        && quadrant == other.quadrant
        && bucket == other.bucket
        && explicitQuadrant == other.explicitQuadrant
        && suggestedQuadrant == other.suggestedQuadrant
        && listId == other.listId
        && listName == other.listName
        && listColor == other.listColor
        && dueText == other.dueText
        && overdue == other.overdue
        && urgentDue == other.urgentDue
        && suggestPromote == other.suggestPromote
        && tags == other.tags
        && hasNotes == other.hasNotes
        && recurring == other.recurring
        && estimatedMinutes == other.estimatedMinutes
        && trackedSeconds == other.trackedSeconds
        && trackedText == other.trackedText
        && pomodorosCompleted == other.pomodorosCompleted
        && readOnly == other.readOnly;
}

QVariantMap MatrixTaskEntry::toMap() const
{
    return {
        {QStringLiteral("id"), id},
        {QStringLiteral("taskId"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("notes"), notes},
        {QStringLiteral("done"), done},
        {QStringLiteral("priority"), priority},
        {QStringLiteral("quadrant"), quadrant},
        {QStringLiteral("bucket"), bucket},
        {QStringLiteral("explicitQuadrant"), explicitQuadrant},
        {QStringLiteral("suggestedQuadrant"), suggestedQuadrant},
        {QStringLiteral("listId"), listId},
        {QStringLiteral("listName"), listName},
        {QStringLiteral("listColor"), listColor},
        {QStringLiteral("dueText"), dueText},
        {QStringLiteral("overdue"), overdue},
        {QStringLiteral("urgentDue"), urgentDue},
        {QStringLiteral("suggestPromote"), suggestPromote},
        {QStringLiteral("tags"), tags},
        {QStringLiteral("hasNotes"), hasNotes},
        {QStringLiteral("recurring"), recurring},
        {QStringLiteral("estimatedMinutes"), estimatedMinutes},
        {QStringLiteral("trackedSeconds"), trackedSeconds},
        {QStringLiteral("trackedText"), trackedText},
        {QStringLiteral("pomodorosCompleted"), pomodorosCompleted},
        {QStringLiteral("readOnly"), readOnly},
    };
}

QuadrantTaskModel::QuadrantTaskModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int QuadrantTaskModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant QuadrantTaskModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }
    const MatrixTaskEntry &e = m_entries.at(index.row());
    switch (role) {
    case TaskIdRole:
        return e.id;
    case TitleRole:
        return e.title;
    case NotesRole:
        return e.notes;
    case DoneRole:
        return e.done;
    case PriorityRole:
        return e.priority;
    case QuadrantRole:
        return e.quadrant;
    case BucketRole:
        return e.bucket;
    case ExplicitQuadrantRole:
        return e.explicitQuadrant;
    case SuggestedQuadrantRole:
        return e.suggestedQuadrant;
    case ListIdRole:
        return e.listId;
    case ListNameRole:
        return e.listName;
    case ListColorRole:
        return e.listColor;
    case DueTextRole:
        return e.dueText;
    case OverdueRole:
        return e.overdue;
    case UrgentDueRole:
        return e.urgentDue;
    case SuggestPromoteRole:
        return e.suggestPromote;
    case TagsRole:
        return e.tags;
    case HasNotesRole:
        return e.hasNotes;
    case RecurringRole:
        return e.recurring;
    case EstimatedMinutesRole:
        return e.estimatedMinutes;
    case TrackedSecondsRole:
        return e.trackedSeconds;
    case TrackedTextRole:
        return e.trackedText;
    case PomodorosCompletedRole:
        return e.pomodorosCompleted;
    case ReadOnlyRole:
        return e.readOnly;
    default:
        return {};
    }
}

QHash<int, QByteArray> QuadrantTaskModel::roleNames() const
{
    return {
        {TaskIdRole, "taskId"},
        {TitleRole, "title"},
        {NotesRole, "notes"},
        {DoneRole, "done"},
        {PriorityRole, "priority"},
        {QuadrantRole, "quadrant"},
        {BucketRole, "bucket"},
        {ExplicitQuadrantRole, "explicitQuadrant"},
        {SuggestedQuadrantRole, "suggestedQuadrant"},
        {ListIdRole, "listId"},
        {ListNameRole, "listName"},
        {ListColorRole, "listColor"},
        {DueTextRole, "dueText"},
        {OverdueRole, "overdue"},
        {UrgentDueRole, "urgentDue"},
        {SuggestPromoteRole, "suggestPromote"},
        {TagsRole, "tags"},
        {HasNotesRole, "hasNotes"},
        {RecurringRole, "recurring"},
        {EstimatedMinutesRole, "estimatedMinutes"},
        {TrackedSecondsRole, "trackedSeconds"},
        {TrackedTextRole, "trackedText"},
        {PomodorosCompletedRole, "pomodorosCompleted"},
        {ReadOnlyRole, "readOnly"},
    };
}

void QuadrantTaskModel::setEntries(QList<MatrixTaskEntry> next)
{
    if (m_entries.isEmpty() && next.isEmpty()) {
        return;
    }
    const int oldCount = m_entries.size();
    if (next.isEmpty()) {
        beginRemoveRows({}, 0, m_entries.size() - 1);
        m_entries.clear();
        m_entries.squeeze();
        endRemoveRows();
        Q_EMIT countChanged();
        return;
    }
    if (m_entries.isEmpty()) {
        beginInsertRows({}, 0, next.size() - 1);
        m_entries = std::move(next);
        endInsertRows();
        Q_EMIT countChanged();
        return;
    }

    QSet<qint64> nextIds;
    nextIds.reserve(next.size());
    for (const MatrixTaskEntry &e : std::as_const(next)) {
        nextIds.insert(e.id);
    }

    for (int i = m_entries.size() - 1; i >= 0; --i) {
        if (!nextIds.contains(m_entries.at(i).id)) {
            beginRemoveRows({}, i, i);
            m_entries.removeAt(i);
            endRemoveRows();
        }
    }

    for (int i = 0; i < next.size(); ++i) {
        const qint64 wantedId = next.at(i).id;
        int cur = -1;
        for (int j = i; j < m_entries.size(); ++j) {
            if (m_entries.at(j).id == wantedId) {
                cur = j;
                break;
            }
        }
        if (cur < 0) {
            beginInsertRows({}, i, i);
            m_entries.insert(i, next.at(i));
            endInsertRows();
        } else {
            if (cur != i) {
                beginMoveRows({}, cur, cur, {}, i);
                m_entries.move(cur, i);
                endMoveRows();
            }
            if (m_entries.at(i) != next.at(i)) {
                m_entries[i] = next.at(i);
                const QModelIndex idx = index(i);
                Q_EMIT dataChanged(idx, idx);
            }
        }
    }

    if (m_entries.size() != oldCount) {
        Q_EMIT countChanged();
    }
}

void QuadrantTaskModel::clearAndFree()
{
    if (m_entries.isEmpty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    m_entries.squeeze();
    endResetModel();
    Q_EMIT countChanged();
}

QVariantList QuadrantTaskModel::toVariantList() const
{
    QVariantList out;
    out.reserve(m_entries.size());
    for (const MatrixTaskEntry &e : m_entries) {
        out.append(e.toMap());
    }
    return out;
}

QVariantMap QuadrantTaskModel::get(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return {};
    }
    return m_entries.at(row).toMap();
}

qlonglong QuadrantTaskModel::idAt(int row) const
{
    if (row < 0 || row >= m_entries.size()) {
        return 0;
    }
    return m_entries.at(row).id;
}

static QString foldSearch(const QString &s)
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

EisenhowerManager::EisenhowerManager(TaskStore *store, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_settings(settings)
{
    connect(m_store, &TaskStore::changed, this, &EisenhowerManager::markDirtyOrRebuild);
    connect(m_settings, &Settings::eisenhowerUrgencyHoursChanged, this, &EisenhowerManager::markDirtyOrRebuild);
}

void EisenhowerManager::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    if (m_active) {
        if (m_dirty) {
            rebuildInternal(true);
        }
    } else {
        for (int i = 0; i < 5; ++i) {
            m_models[i].clearAndFree();
            m_counts[i] = 0;
        }
        for (int i = 0; i < 4; ++i) {
            m_qIds[i].clear();
        }
        m_totalOpen = 0;
        m_dirty = true;
    }
}

void EisenhowerManager::markDirtyOrRebuild()
{
    m_dirty = true;
    if (m_active) {
        rebuildInternal(true);
    }
}

void EisenhowerManager::ensureFresh() const
{
    if (m_dirty) {
        const_cast<EisenhowerManager *>(this)->rebuildInternal(false);
    }
}

QVariantList EisenhowerManager::triageTasks() const
{
    ensureFresh();
    return m_models[0].toVariantList();
}

QVariantList EisenhowerManager::q1Tasks() const
{
    ensureFresh();
    return m_models[1].toVariantList();
}

QVariantList EisenhowerManager::q2Tasks() const
{
    ensureFresh();
    return m_models[2].toVariantList();
}

QVariantList EisenhowerManager::q3Tasks() const
{
    ensureFresh();
    return m_models[3].toVariantList();
}

QVariantList EisenhowerManager::q4Tasks() const
{
    ensureFresh();
    return m_models[4].toVariantList();
}

int EisenhowerManager::triageCount() const
{
    ensureFresh();
    return m_counts[0];
}

int EisenhowerManager::q1Count() const
{
    ensureFresh();
    return m_counts[1];
}

int EisenhowerManager::q2Count() const
{
    ensureFresh();
    return m_counts[2];
}

int EisenhowerManager::q3Count() const
{
    ensureFresh();
    return m_counts[3];
}

int EisenhowerManager::q4Count() const
{
    ensureFresh();
    return m_counts[4];
}

int EisenhowerManager::totalOpen() const
{
    ensureFresh();
    return m_totalOpen;
}

void EisenhowerManager::setFilterListId(qlonglong listId)
{
    if (m_filterListId != listId) {
        m_filterListId = listId;
        markDirtyOrRebuild();
    }
}

void EisenhowerManager::setFilterTag(const QString &tag)
{
    if (m_filterTag != tag) {
        m_filterTag = tag;
        markDirtyOrRebuild();
    }
}

void EisenhowerManager::setSearchText(const QString &text)
{
    if (m_searchText != text) {
        m_searchText = text;
        markDirtyOrRebuild();
    }
}

void EisenhowerManager::setShowCompleted(bool show)
{
    if (m_showCompleted != show) {
        m_showCompleted = show;
        markDirtyOrRebuild();
    }
}

int EisenhowerManager::urgencyHours() const
{
    return qMax(12, m_settings->eisenhowerUrgencyHours());
}

void EisenhowerManager::setUrgencyHours(int hours)
{
    m_settings->setEisenhowerUrgencyHours(qBound(12, hours, 24 * 30));
}

bool EisenhowerManager::isTaskUrgent(const Task &t, const QDateTime &now, int urgencyHours)
{
    if (!t.due.isValid() || t.isDone()) {
        return false;
    }
    if (t.allDay) {
        const qint64 daysLeft = now.date().daysTo(t.due.date());
        const qint64 maxDays = urgencyHours <= 24 ? 0 : (urgencyHours <= 48 ? 1 : qMax<qint64>(1, urgencyHours / 24));
        return daysLeft <= maxDays;
    }
    return now.secsTo(t.due.toLocalTime()) <= qint64(urgencyHours) * 3600;
}

int EisenhowerManager::classifyBucket(const Task &t, const QDateTime &now, int urgencyHours,
                                      int *suggestedQuadrant, bool *urgentDue, bool *suggestPromote)
{
    const bool urgent = isTaskUrgent(t, now, urgencyHours);
    if (urgentDue) {
        *urgentDue = urgent;
    }
    if (suggestPromote) {
        *suggestPromote = false;
    }
    if (t.quadrant >= 1 && t.quadrant <= 4) {
        if (suggestedQuadrant) {
            *suggestedQuadrant = t.quadrant;
        }
        if (suggestPromote && t.quadrant == 2 && urgent && !t.isDone()) {
            *suggestPromote = true;
        }
        return t.quadrant;
    }

    const int p = t.priorityLevel();
    int bucket = 0;
    int suggested = urgent ? 1 : 2;
    if (p == 3) {
        bucket = 1;
        suggested = 1;
    } else if (p == 2) {
        bucket = urgent ? 1 : 2;
        suggested = bucket;
    } else if (p == 1) {
        bucket = urgent ? 3 : 4;
        suggested = bucket;
    }
    if (suggestedQuadrant) {
        *suggestedQuadrant = suggested;
    }
    return bucket;
}

void EisenhowerManager::sortQuadrantTasks(QList<const Task *> &tasks)
{
    std::sort(tasks.begin(), tasks.end(), [](const Task *a, const Task *b) {
        if (a->isDone() != b->isDone()) {
            return !a->isDone();
        }
        if (a->sortOrder != b->sortOrder) {
            return a->sortOrder < b->sortOrder;
        }
        return a->id < b->id;
    });
}

MatrixTaskEntry EisenhowerManager::buildEntry(const Task &t, const QDateTime &now, int bucket, int suggested, bool urgentDue, bool suggestPromote) const
{
    const TaskList *l = m_store->list(t.listId);
    MatrixTaskEntry e;
    e.id = t.id;
    e.title = t.title;
    e.notes = t.notes;
    e.done = t.isDone();
    e.priority = t.priorityLevel();
    e.quadrant = bucket;
    e.bucket = bucket;
    e.explicitQuadrant = (t.quadrant >= 1 && t.quadrant <= 4);
    e.suggestedQuadrant = suggested;
    e.urgentDue = urgentDue;
    e.suggestPromote = suggestPromote;
    e.dueText = TaskModel::formatDue(t, now);
    e.overdue = !t.isDone() && t.due.isValid() && (t.allDay ? t.due.date() < now.date() : t.due < now);
    e.listId = t.listId;
    e.listName = l ? l->name : QString();
    e.listColor = l ? l->color : QString();
    e.tags = t.tags;
    e.hasNotes = !t.notes.trimmed().isEmpty();
    e.recurring = !t.rrule.isEmpty();
    e.estimatedMinutes = t.estimatedMinutes;
    e.trackedSeconds = t.trackedSeconds;
    e.trackedText = FocusManager::formatDuration(t.trackedSeconds);
    e.pomodorosCompleted = t.pomodorosCompleted;
    e.readOnly = (l && l->readOnly);
    return e;
}

void EisenhowerManager::rebuild()
{
    rebuildInternal(true);
}

void EisenhowerManager::rebuildInternal(bool emitChanged)
{
    const QDateTime now = QDateTime::currentDateTime();
    const int hours = urgencyHours();
    const QString query = foldSearch(m_searchText.trimmed());

    struct ClassifiedItem {
        const Task *task = nullptr;
        int bucket = 0;
        int suggested = 2;
        bool urgentDue = false;
        bool suggestPromote = false;
    };

    QList<ClassifiedItem> triageRaw;
    QList<ClassifiedItem> qRaw[5];
    m_totalOpen = 0;

    for (const Task &t : m_store->tasks()) {
        if (t.isDone() && !m_showCompleted) {
            continue;
        }
        if (m_filterListId > 0 && t.listId != m_filterListId) {
            continue;
        }
        if (!m_filterTag.isEmpty() && !t.tags.contains(m_filterTag, Qt::CaseInsensitive)) {
            continue;
        }
        if (!query.isEmpty()) {
            const bool matchTitle = foldSearch(t.title).contains(query);
            const bool matchNotes = foldSearch(t.notes).contains(query);
            bool matchTag = false;
            for (const QString &tg : t.tags) {
                if (foldSearch(tg).contains(query)) {
                    matchTag = true;
                    break;
                }
            }
            if (!matchTitle && !matchNotes && !matchTag) {
                continue;
            }
        }
        if (!t.isDone()) {
            ++m_totalOpen;
        }
        int suggested = 2;
        bool urgentDue = false;
        bool promote = false;
        const int bucket = classifyBucket(t, now, hours, &suggested, &urgentDue, &promote);
        const ClassifiedItem item{&t, bucket, suggested, urgentDue, promote};
        if (bucket >= 1 && bucket <= 4) {
            qRaw[bucket].append(item);
        } else {
            triageRaw.append(item);
        }
    }

    std::sort(triageRaw.begin(), triageRaw.end(), [](const ClassifiedItem &a, const ClassifiedItem &b) {
        if (a.urgentDue != b.urgentDue) {
            return a.urgentDue;
        }
        if (a.task->due.isValid() != b.task->due.isValid()) {
            return a.task->due.isValid();
        }
        if (a.task->due != b.task->due) {
            return a.task->due < b.task->due;
        }
        return a.task->id < b.task->id;
    });

    QList<MatrixTaskEntry> triageEntries;
    triageEntries.reserve(triageRaw.size());
    for (const ClassifiedItem &c : std::as_const(triageRaw)) {
        triageEntries.append(buildEntry(*c.task, now, c.bucket, c.suggested, c.urgentDue, c.suggestPromote));
    }
    m_counts[0] = triageEntries.size();
    m_models[0].setEntries(std::move(triageEntries));

    for (int q = 1; q <= 4; ++q) {
        QList<ClassifiedItem> &raw = qRaw[q];
        std::sort(raw.begin(), raw.end(), [](const ClassifiedItem &a, const ClassifiedItem &b) {
            if (a.task->isDone() != b.task->isDone()) {
                return !a.task->isDone();
            }
            if (a.task->sortOrder != b.task->sortOrder) {
                return a.task->sortOrder < b.task->sortOrder;
            }
            return a.task->id < b.task->id;
        });

        QList<MatrixTaskEntry> entries;
        entries.reserve(raw.size());
        m_qIds[q - 1].clear();
        m_qIds[q - 1].reserve(raw.size());
        for (const ClassifiedItem &c : std::as_const(raw)) {
            entries.append(buildEntry(*c.task, now, q, c.suggested, c.urgentDue, c.suggestPromote));
            m_qIds[q - 1].append(c.task->id);
        }
        m_counts[q] = entries.size();
        m_models[q].setEntries(std::move(entries));
    }

    m_dirty = false;
    if (emitChanged) {
        Q_EMIT changed();
    }
}

void EisenhowerManager::classifyTask(qlonglong id, int quadrant, bool setDueToday)
{
    const Task *cur = m_store->task(id);
    if (!cur || quadrant < 1 || quadrant > 4) {
        return;
    }
    const TaskList *l = m_store->list(cur->listId);
    if (l && l->readOnly) {
        return;
    }
    Task updated = *cur;
    updated.quadrant = quadrant;
    updated.priority = Task::priorityForQuadrant(quadrant);
    if (setDueToday && !updated.due.isValid()) {
        updated.allDay = true;
        updated.due = QDateTime(QDate::currentDate(), QTime(0, 0));
    }
    if (updated.quadrant == cur->quadrant && updated.priority == cur->priority && updated.due == cur->due) {
        return;
    }
    m_store->updateTask(updated, i18nc("undo step", "Prioritize task"));
}

void EisenhowerManager::moveToQuadrantAt(qlonglong id, int quadrant, qlonglong beforeTaskId)
{
    ensureFresh();
    const Task *cur = m_store->task(id);
    if (!cur || quadrant < 1 || quadrant > 4) {
        return;
    }
    const TaskList *l = m_store->list(cur->listId);
    if (l && l->readOnly) {
        return;
    }

    const QList<qint64> &quadIds = m_qIds[quadrant - 1];
    QList<qint64> targetIds;
    targetIds.reserve(quadIds.size());
    for (qint64 tid : quadIds) {
        if (tid != id) {
            targetIds.append(tid);
        }
    }

    int insertIdx = targetIds.size();
    if (beforeTaskId > 0) {
        const int idx = targetIds.indexOf(beforeTaskId);
        if (idx >= 0) {
            insertIdx = idx;
        }
    }
    targetIds.insert(insertIdx, id);

    // Check whether existing integer sortOrders have clean room for midpoint insertion,
    // or if any items share close/equal sortOrder (common with synced/quick-added tasks).
    bool needRenumber = false;
    for (int i = 0; i < targetIds.size() - 1; ++i) {
        if (targetIds.at(i) == id || targetIds.at(i + 1) == id) {
            continue;
        }
        const Task *a = m_store->task(targetIds.at(i));
        const Task *b = m_store->task(targetIds.at(i + 1));
        if (!a || !b || qint64(b->sortOrder) - qint64(a->sortOrder) < 2) {
            needRenumber = true;
            break;
        }
    }
    if (!needRenumber && targetIds.size() > 1) {
        const Task *prev = insertIdx > 0 ? m_store->task(targetIds.at(insertIdx - 1)) : nullptr;
        const Task *next = (insertIdx + 1 < targetIds.size()) ? m_store->task(targetIds.at(insertIdx + 1)) : nullptr;
        if (prev && next && qint64(next->sortOrder) - qint64(prev->sortOrder) < 2) {
            needRenumber = true;
        }
    }

    QList<Task> updates;
    if (needRenumber) {
        for (int i = 0; i < targetIds.size(); ++i) {
            if (const Task *curT = m_store->task(targetIds.at(i))) {
                Task u = *curT;
                const double targetOrder = double((i + 1) * 100);
                const bool isMoved = (u.id == id);
                if (isMoved) {
                    u.quadrant = quadrant;
                    u.priority = Task::priorityForQuadrant(quadrant);
                }
                if (isMoved || !qFuzzyCompare(u.sortOrder, targetOrder)) {
                    u.sortOrder = targetOrder;
                    updates.append(u);
                }
            }
        }
    } else {
        double newOrder = cur->sortOrder;
        if (targetIds.size() == 1) {
            newOrder = cur->sortOrder > 0 ? cur->sortOrder : 1000.0;
        } else {
            const Task *prev = insertIdx > 0 ? m_store->task(targetIds.at(insertIdx - 1)) : nullptr;
            const Task *next = (insertIdx + 1 < targetIds.size()) ? m_store->task(targetIds.at(insertIdx + 1)) : nullptr;
            if (!prev && next) {
                newOrder = double(qint64(next->sortOrder) - 100);
            } else if (prev && !next) {
                newOrder = double(qint64(prev->sortOrder) + 100);
            } else if (prev && next) {
                newOrder = double((qint64(prev->sortOrder) + qint64(next->sortOrder)) / 2);
            }
        }
        Task updated = *cur;
        updated.quadrant = quadrant;
        updated.priority = Task::priorityForQuadrant(quadrant);
        updated.sortOrder = newOrder;
        if (updated.quadrant == cur->quadrant && updated.priority == cur->priority && qFuzzyCompare(updated.sortOrder, cur->sortOrder)) {
            return;
        }
        updates.append(updated);
    }

    if (!updates.isEmpty()) {
        m_store->updateTasks(updates, i18nc("undo step", "Prioritize task"));
    }
}

void EisenhowerManager::moveTaskStep(qlonglong id, int delta)
{
    ensureFresh();
    const Task *cur = m_store->task(id);
    if (!cur || (delta != -1 && delta != 1)) {
        return;
    }
    int foundQ = 0;
    int pos = -1;
    for (int i = 0; i < 4; ++i) {
        const int idx = m_qIds[i].indexOf(id);
        if (idx >= 0) {
            foundQ = i + 1;
            pos = idx;
            break;
        }
    }
    if (foundQ <= 0) {
        return;
    }
    const QList<qint64> &ids = m_qIds[foundQ - 1];
    const int targetPos = pos + delta;
    if (targetPos < 0 || targetPos >= ids.size()) {
        return;
    }
    const qlonglong beforeId = (delta < 0) ? ids.at(targetPos)
                                            : ((targetPos + 1 < ids.size()) ? ids.at(targetPos + 1) : 0);
    moveToQuadrantAt(id, foundQ, beforeId);
}

void EisenhowerManager::resetQuadrant(qlonglong id)
{
    const Task *cur = m_store->task(id);
    if (!cur) {
        return;
    }
    const TaskList *l = m_store->list(cur->listId);
    if (l && l->readOnly) {
        return;
    }
    Task updated = *cur;
    updated.quadrant = 0;
    updated.priority = 0;
    if (updated.quadrant == cur->quadrant && updated.priority == cur->priority) {
        return;
    }
    m_store->updateTask(updated, i18nc("undo step", "Reset task priority"));
}

int EisenhowerManager::clearQ4()
{
    ensureFresh();
    QList<qint64> toDelete;
    for (qint64 id : std::as_const(m_qIds[3])) {
        const Task *t = m_store->task(id);
        if (!t) {
            continue;
        }
        const TaskList *l = m_store->list(t->listId);
        if (l && l->readOnly) {
            continue;
        }
        toDelete.append(id);
    }
    if (toDelete.isEmpty()) {
        return 0;
    }
    m_store->removeTasks(toDelete, i18nc("undo step", "Clear Q4 tasks"));
    Q_EMIT message(i18np("%1 task removed from Q4", "%1 tasks removed from Q4", toDelete.size()), true);
    return toDelete.size();
}
