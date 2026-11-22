// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "taskstore.h"

#include <QAbstractListModel>

/** Entries of the sidebar: smart views, the lists of each account, tags. */
class SidebarModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { KeyRole = Qt::UserRole + 1, NameRole, IconRole, ColorRole, CountRole, SectionRole, KindRole, ListIdRole, ReadOnlyRole, PendingRole };

    explicit SidebarModel(TaskStore *store, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** Open tasks due today or overdue; used for the tray and the launcher badge. */
    int dueCount() const
    {
        return m_dueCount;
    }
    int overdueCount() const
    {
        return m_overdueCount;
    }

Q_SIGNALS:
    void countsChanged();

private:
    struct Entry {
        QString key;
        QString name;
        QString icon;
        QString color;
        int count = 0;
        QString section;
        QString kind;
        qint64 listId = 0;
        bool readOnly = false;
        bool pending = false;
        bool operator==(const Entry &o) const = default;
    };
    void rebuild();
    TaskStore *m_store;
    QList<Entry> m_entries;
    int m_dueCount = 0;
    int m_overdueCount = 0;
};
