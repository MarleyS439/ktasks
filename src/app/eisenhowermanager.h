// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "task.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Settings;
class TaskStore;

struct MatrixTaskEntry {
    qint64 id = 0;
    QString title;
    QString notes;
    bool done = false;
    int priority = 0;
    int quadrant = 0;
    int bucket = 0;
    bool explicitQuadrant = false;
    int suggestedQuadrant = 2;
    qint64 listId = 0;
    QString listName;
    QString listColor;
    QString dueText;
    bool overdue = false;
    bool urgentDue = false;
    bool suggestPromote = false;
    QStringList tags;
    bool hasNotes = false;
    bool recurring = false;
    int estimatedMinutes = 0;
    int trackedSeconds = 0;
    QString trackedText;
    int pomodorosCompleted = 0;
    bool readOnly = false;

    bool operator==(const MatrixTaskEntry &other) const;
    bool operator!=(const MatrixTaskEntry &other) const { return !(*this == other); }
    QVariantMap toMap() const;
};

/**
 * Incremental diff-based list model for a single Eisenhower bucket (Triage or Q1..Q4).
 * Emits minimal insert/move/remove/dataChanged notifications so scrolling and dragging
 * never destroy unrelated delegate items.
 */
class QuadrantTaskModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        TaskIdRole = Qt::UserRole + 1,
        TitleRole,
        NotesRole,
        DoneRole,
        PriorityRole,
        QuadrantRole,
        BucketRole,
        ExplicitQuadrantRole,
        SuggestedQuadrantRole,
        ListIdRole,
        ListNameRole,
        ListColorRole,
        DueTextRole,
        OverdueRole,
        UrgentDueRole,
        SuggestPromoteRole,
        TagsRole,
        HasNotesRole,
        RecurringRole,
        EstimatedMinutesRole,
        TrackedSecondsRole,
        TrackedTextRole,
        PomodorosCompletedRole,
        ReadOnlyRole,
    };

    explicit QuadrantTaskModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(QList<MatrixTaskEntry> next);
    void clearAndFree();

    const QList<MatrixTaskEntry> &entries() const { return m_entries; }
    QVariantList toVariantList() const;
    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE qlonglong idAt(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    QList<MatrixTaskEntry> m_entries;
};

/**
 * Computes and mutates the Eisenhower Priority Matrix (Triage, Q1..Q4) over
 * TaskStore while keeping RFC 5545 VTODO PRIORITY and X-KTASKS-QUADRANT in sync.
 */
class EisenhowerManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QAbstractListModel *triageModel READ triageModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *q1Model READ q1Model CONSTANT)
    Q_PROPERTY(QAbstractListModel *q2Model READ q2Model CONSTANT)
    Q_PROPERTY(QAbstractListModel *q3Model READ q3Model CONSTANT)
    Q_PROPERTY(QAbstractListModel *q4Model READ q4Model CONSTANT)
    Q_PROPERTY(int triageCount READ triageCount NOTIFY changed)
    Q_PROPERTY(int q1Count READ q1Count NOTIFY changed)
    Q_PROPERTY(int q2Count READ q2Count NOTIFY changed)
    Q_PROPERTY(int q3Count READ q3Count NOTIFY changed)
    Q_PROPERTY(int q4Count READ q4Count NOTIFY changed)
    Q_PROPERTY(int totalOpen READ totalOpen NOTIFY changed)
    Q_PROPERTY(qlonglong filterListId READ filterListId WRITE setFilterListId NOTIFY changed)
    Q_PROPERTY(QString filterTag READ filterTag WRITE setFilterTag NOTIFY changed)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY changed)
    Q_PROPERTY(bool showCompleted READ showCompleted WRITE setShowCompleted NOTIFY changed)
    Q_PROPERTY(int urgencyHours READ urgencyHours WRITE setUrgencyHours NOTIFY changed)

public:
    EisenhowerManager(TaskStore *store, Settings *settings, QObject *parent = nullptr);

    bool active() const { return m_active; }
    void setActive(bool active);
    bool isDirty() const { return m_dirty; }

    QAbstractListModel *triageModel() { return &m_models[0]; }
    QAbstractListModel *q1Model() { return &m_models[1]; }
    QAbstractListModel *q2Model() { return &m_models[2]; }
    QAbstractListModel *q3Model() { return &m_models[3]; }
    QAbstractListModel *q4Model() { return &m_models[4]; }

    Q_INVOKABLE QVariantList triageTasks() const;
    Q_INVOKABLE QVariantList q1Tasks() const;
    Q_INVOKABLE QVariantList q2Tasks() const;
    Q_INVOKABLE QVariantList q3Tasks() const;
    Q_INVOKABLE QVariantList q4Tasks() const;
    int triageCount() const;
    int q1Count() const;
    int q2Count() const;
    int q3Count() const;
    int q4Count() const;
    int totalOpen() const;

    qlonglong filterListId() const { return m_filterListId; }
    void setFilterListId(qlonglong listId);

    QString filterTag() const { return m_filterTag; }
    void setFilterTag(const QString &tag);

    QString searchText() const { return m_searchText; }
    void setSearchText(const QString &text);

    bool showCompleted() const { return m_showCompleted; }
    void setShowCompleted(bool show);

    int urgencyHours() const;
    void setUrgencyHours(int hours);

    /** Classify @p id into quadrant 1..4 (updates priority and records one undo step). */
    Q_INVOKABLE void classifyTask(qlonglong id, int quadrant, bool setDueToday = false);
    /** Move @p id into @p quadrant right before @p beforeTaskId (0 = append to end). */
    Q_INVOKABLE void moveToQuadrantAt(qlonglong id, int quadrant, qlonglong beforeTaskId);
    /** Move @p id one step up (-1) or down (+1) inside its current quadrant. */
    Q_INVOKABLE void moveTaskStep(qlonglong id, int delta);
    /** Clears explicit quadrant and priority so the task returns to Triage. */
    Q_INVOKABLE void resetQuadrant(qlonglong id);
    /** Deletes all visible writable Q4 tasks in a single undoable step. Returns count deleted. */
    Q_INVOKABLE int clearQ4();

    /** Returns true when @p t is overdue or due within @p urgencyHours. */
    static bool isTaskUrgent(const KTasks::Task &t, const QDateTime &now, int urgencyHours);
    /**
     * Classifies @p t into bucket 0 (Triage) or 1..4 (Q1..Q4).
     * Explicit quadrant values (1..4) always win; unprioritized tasks without explicit
     * quadrant land in Triage (0) with @p suggestedQuadrant populated.
     */
    static int classifyBucket(const KTasks::Task &t, const QDateTime &now, int urgencyHours,
                              int *suggestedQuadrant = nullptr, bool *urgentDue = nullptr, bool *suggestPromote = nullptr);

public Q_SLOTS:
    void rebuild();

Q_SIGNALS:
    void activeChanged();
    void changed();
    void message(const QString &text, bool withUndo);

private:
    void markDirtyOrRebuild();
    void ensureFresh() const;
    void rebuildInternal(bool emitChanged);
    MatrixTaskEntry buildEntry(const KTasks::Task &t, const QDateTime &now, int bucket, int suggested, bool urgentDue, bool suggestPromote) const;
    static void sortQuadrantTasks(QList<const KTasks::Task *> &tasks);

    TaskStore *m_store;
    Settings *m_settings;
    bool m_active = false;
    bool m_dirty = true;
    qlonglong m_filterListId = 0;
    QString m_filterTag;
    QString m_searchText;
    bool m_showCompleted = false;

    QuadrantTaskModel m_models[5];
    QList<qint64> m_qIds[4];
    int m_counts[5] = {0, 0, 0, 0, 0};
    int m_totalOpen = 0;
};
