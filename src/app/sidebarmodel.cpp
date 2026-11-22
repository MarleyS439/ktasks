// SPDX-License-Identifier: GPL-3.0-or-later
#include "sidebarmodel.h"
#include "taskmodel.h"

#include <KLocalizedString>
#include <QMap>

using namespace KTasks;

SidebarModel::SidebarModel(TaskStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
    connect(store, &TaskStore::changed, this, &SidebarModel::rebuild);
    rebuild();
}

QHash<int, QByteArray> SidebarModel::roleNames() const
{
    return {{KeyRole, "key"},         {NameRole, "name"}, {IconRole, "iconName"}, {ColorRole, "color"},       {CountRole, "count"},
            {SectionRole, "section"}, {KindRole, "kind"}, {ListIdRole, "listId"}, {ReadOnlyRole, "readOnly"}, {PendingRole, "pending"}};
}

int SidebarModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant SidebarModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size()) {
        return {};
    }
    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case KeyRole:
        return e.key;
    case Qt::DisplayRole:
    case NameRole:
        return e.name;
    case IconRole:
        return e.icon;
    case ColorRole:
        return e.color;
    case CountRole:
        return e.count;
    case SectionRole:
        return e.section;
    case KindRole:
        return e.kind;
    case ListIdRole:
        return e.listId;
    case ReadOnlyRole:
        return e.readOnly;
    case PendingRole:
        return e.pending;
    }
    return {};
}

void SidebarModel::rebuild()
{
    const QDateTime now = QDateTime::currentDateTime();
    QList<Entry> entries;
    auto smart = [&](const char *key, const QString &name, const char *icon) {
        Entry e;
        e.key = QString::fromLatin1(key);
        e.name = name;
        e.icon = QString::fromLatin1(icon);
        e.kind = QStringLiteral("smart");
        e.section = i18nc("sidebar section", "Tasks");
        for (const Task &t : m_store->tasks()) {
            const bool counts = e.key == QLatin1String("completed") ? true : !t.isDone();
            e.count += counts && TaskModel::matchesView(t, e.key, now);
        }
        entries.append(e);
        return e.count;
    };
    const int due = smart("today", i18nc("smart view", "Today"), "view-calendar-day");
    smart("week", i18nc("smart view", "Next 7 days"), "view-calendar-week");
    const int overdue = smart("overdue", i18nc("smart view", "Overdue"), "data-warning");
    smart("all", i18nc("smart view", "All tasks"), "view-task");
    smart("nodate", i18nc("smart view", "No date"), "view-calendar-list");
    smart("completed", i18nc("smart view", "Completed"), "task-complete");
    // "Completed" is an archive, a counter there is only noise.
    entries.last().count = 0;

    QHash<qint64, int> open;
    QHash<qint64, bool> pending;
    QMap<QString, int> tags;
    for (const Task &t : m_store->tasks()) {
        if (!t.isDone()) {
            ++open[t.listId];
            for (const QString &tag : t.tags) {
                ++tags[tag];
            }
        }
        if (t.syncState != SyncState::Synced) {
            pending[t.listId] = true;
        }
    }
    for (const TaskList &l : m_store->lists()) {
        Entry e;
        e.key = QStringLiteral("list:%1").arg(l.id);
        e.name = l.name;
        e.icon = l.readOnly ? QStringLiteral("object-locked") : QStringLiteral("view-list-details");
        e.color = l.color;
        e.count = open.value(l.id);
        e.kind = QStringLiteral("list");
        e.listId = l.id;
        e.readOnly = l.readOnly;
        const Account *a = m_store->account(l.accountId);
        e.section = a ? a->name : i18nc("section for lists that are not synchronized", "On this device");
        e.pending = a && (pending.value(l.id) || l.syncState != SyncState::Synced);
        entries.append(e);
    }
    for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
        Entry e;
        e.key = QStringLiteral("tag:") + it.key();
        e.name = it.key();
        e.icon = QStringLiteral("tag");
        e.count = it.value();
        e.kind = QStringLiteral("tag");
        e.section = i18nc("sidebar section", "Tags");
        entries.append(e);
    }

    if (entries != m_entries) {
        if (entries.size() == m_entries.size()) {
            m_entries = entries;
            Q_EMIT dataChanged(index(0), index(m_entries.size() - 1));
        } else {
            beginResetModel();
            m_entries = entries;
            endResetModel();
        }
    }
    if (due != m_dueCount || overdue != m_overdueCount) {
        m_dueCount = due;
        m_overdueCount = overdue;
        Q_EMIT countsChanged();
    }
}
