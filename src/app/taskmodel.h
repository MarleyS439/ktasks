// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "taskstore.h"

#include <QAbstractListModel>

class Settings;

/**
 * The task list shown in the main view: the tasks of the current view
 * (a smart view, a list or a tag), filtered, sorted and, for lists,
 * flattened from the subtask tree.
 */
class TaskModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(bool showCompleted READ showCompleted WRITE setShowCompleted NOTIFY showCompletedChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool isListView READ isListView NOTIFY viewChanged)
    Q_PROPERTY(qlonglong listId READ listId NOTIFY viewChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY titleChanged)
    Q_PROPERTY(bool canReorder READ canReorder NOTIFY canReorderChanged)
    Q_PROPERTY(int hiddenCompleted READ hiddenCompleted NOTIFY countChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        DoneRole,
        PriorityRole,
        DueTextRole,
        OverdueRole,
        DueTodayRole,
        ListNameRole,
        ListColorRole,
        TagsRole,
        DepthRole,
        HasChildrenRole,
        CollapsedRole,
        ChildCountRole,
        ChildDoneRole,
        RecurringRole,
        HasReminderRole,
        HasNotesRole,
        SectionRole,
        PercentRole,
        ReadOnlyRole,
    };
    enum SortMode { SortDefault = 0, SortDue, SortPriority, SortTitle, SortCreated, SortManual };
    Q_ENUM(SortMode)

    TaskModel(TaskStore *store, Settings *settings, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString view() const
    {
        return m_view;
    }
    void setView(const QString &view);
    QString title() const;
    QString searchText() const
    {
        return m_search;
    }
    void setSearchText(const QString &text);
    bool showCompleted() const;
    void setShowCompleted(bool show);
    int sortMode() const
    {
        return m_sortMode;
    }
    void setSortMode(int mode);
    int count() const
    {
        return m_rows.size();
    }
    bool isListView() const
    {
        return m_view.startsWith(QLatin1String("list:"));
    }
    qlonglong listId() const
    {
        return isListView() ? m_view.mid(5).toLongLong() : 0;
    }
    bool readOnly() const;
    bool canReorder() const;
    int hiddenCompleted() const
    {
        return m_hiddenCompleted;
    }

    Q_INVOKABLE int indexOfTask(qlonglong id) const;
    Q_INVOKABLE qlonglong taskIdAt(int row) const;
    /** Visual move while dragging; commitMove() makes it permanent. */
    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void commitMove();
    Q_INVOKABLE void toggleCollapsed(qlonglong id);

    /** Whether @p t belongs to @p view ("today", "week", "overdue", "nodate", "all", "completed", "list:N", "tag:X"). */
    static bool matchesView(const KTasks::Task &t, const QString &view, const QDateTime &now);
    static QString formatDue(const KTasks::Task &t, const QDateTime &now);

Q_SIGNALS:
    void viewChanged();
    void titleChanged();
    void searchTextChanged();
    void showCompletedChanged();
    void sortModeChanged();
    void countChanged();
    void canReorderChanged();

private:
    struct Row {
        qint64 id = 0;
        int depth = 0;
        QString section;
        bool hasChildren = false;
        int childCount = 0;
        int childDone = 0;
    };
    void rebuild();
    void setRows(const QList<Row> &rows);
    int effectiveSort() const;
    bool treeMode() const;
    QString sectionFor(const KTasks::Task &t, const QDateTime &now) const;

    TaskStore *m_store;
    Settings *m_settings;
    QString m_view = QStringLiteral("today");
    QString m_search;
    int m_sortMode = SortDefault;
    QList<Row> m_rows;
    int m_hiddenCompleted = 0;
    int m_draggedFrom = -1;
    qint64 m_draggedId = 0;
};
