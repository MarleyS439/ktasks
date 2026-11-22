// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"

#include <QAbstractListModel>
#include <QDate>
#include <QHash>
#include <QVariantList>
#include <QVariantMap>

/**
 * Manages daily routines and hourly day planning (time-blocking).
 * Routine blocks can be recurring templates (daysMask bitmask) or single-day
 * planned blocks (specificDate), optionally linked to an existing Task.
 */
class RoutineManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString selectedDate READ selectedDateIso WRITE setSelectedDateIso NOTIFY selectedDateChanged)
    Q_PROPERTY(QString selectedDateText READ selectedDateText NOTIFY selectedDateChanged)
    Q_PROPERTY(bool isToday READ isToday NOTIFY selectedDateChanged)
    Q_PROPERTY(int totalBlocks READ totalBlocks NOTIFY changed)
    Q_PROPERTY(int completedBlocks READ completedBlocks NOTIFY changed)
    Q_PROPERTY(int plannedMinutes READ plannedMinutes NOTIFY changed)
    Q_PROPERTY(QVariantList templates READ templates NOTIFY changed)

public:
    enum Roles {
        BlockIdRole = Qt::UserRole + 1,
        UidRole,
        TitleRole,
        NotesRole,
        StartMinuteRole,
        DurationMinutesRole,
        TimeRangeRole,
        ColorRole,
        CategoryRole,
        StatusRole, // 0 pending, 1 done, 2 skipped
        IsTemplateRole,
        DaysMaskRole,
        LinkedTaskIdRole,
        NotifyEnabledRole,
        NotifyMinutesBeforeRole,
        SyncAccountIdRole,
        SyncAccountNameRole,
        SyncedRole,
        SyncPendingRole,
    };

    explicit RoutineManager(KTasks::Database *db, QObject *parent = nullptr);

    void reload();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString selectedDateIso() const
    {
        return m_selectedDate.toString(Qt::ISODate);
    }
    void setSelectedDateIso(const QString &iso);
    QDate selectedDate() const
    {
        return m_selectedDate;
    }
    QString selectedDateText() const;
    bool isToday() const
    {
        return m_selectedDate == QDate::currentDate();
    }
    int totalBlocks() const
    {
        return m_dayItems.size();
    }
    int completedBlocks() const;
    int plannedMinutes() const;
    QVariantList templates() const;

    const QList<KTasks::RoutineBlock> &allBlocks() const
    {
        return m_blocks;
    }
    QList<KTasks::RoutineBlock> blocksForDate(const QDate &date) const;
    KTasks::RoutineLogStatus statusForBlock(qint64 blockId, const QDate &date) const;

    Q_INVOKABLE void goToday();
    Q_INVOKABLE void shiftDays(int delta);
    Q_INVOKABLE void shiftWeeks(int delta);
    Q_INVOKABLE void shiftMonths(int delta);
    Q_INVOKABLE qlonglong addBlock(const QVariantMap &fields);
    Q_INVOKABLE bool updateBlock(qlonglong id, const QVariantMap &fields);
    Q_INVOKABLE bool setBlockSyncAccount(qlonglong id, qlonglong accountId);
    Q_INVOKABLE bool removeBlock(qlonglong id);
    Q_INVOKABLE void setBlockStatus(qlonglong id, int status);
    Q_INVOKABLE void setBlockStatusForDate(qlonglong id, const QDate &date, int status);
    Q_INVOKABLE qlonglong scheduleTask(qlonglong taskId, const QString &taskTitle, int startMinute, int durationMinutes, const QString &color = QString());
    Q_INVOKABLE QVariantMap blockInfo(qlonglong id) const;
    Q_INVOKABLE QVariantList weekDays() const;
    Q_INVOKABLE QVariantList monthCells() const;
    Q_INVOKABLE QString monthTitle() const;
    Q_INVOKABLE QString weekTitle() const;

    static QString formatMinutes(int minutes);
    static bool matchesDate(const KTasks::RoutineBlock &b, const QDate &date);

Q_SIGNALS:
    void selectedDateChanged();
    void changed();
    void syncNeeded();

private:
    struct DayItem {
        KTasks::RoutineBlock block;
        KTasks::RoutineLogStatus status = KTasks::RoutineLogStatus::Pending;
    };
    void rebuildDay();
    QString accountNameFor(qint64 accountId) const;

    KTasks::Database *m_db;
    QDate m_selectedDate;
    QList<KTasks::RoutineBlock> m_blocks;
    QList<KTasks::Account> m_accounts;
    QList<DayItem> m_dayItems;
};
