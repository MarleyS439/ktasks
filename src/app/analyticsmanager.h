// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class FocusManager;
class RoutineManager;
class TaskStore;

/**
 * Computes productivity metrics, daily trends, Eisenhower quadrant focus balance,
 * list breakdown, estimation accuracy, routine adherence, and actionable insights.
 */
class AnalyticsManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int periodDays READ periodDays WRITE setPeriodDays NOTIFY periodDaysChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList dailySeries READ dailySeries NOTIFY changed)
    Q_PROPERTY(QVariantList quadrantBreakdown READ quadrantBreakdown NOTIFY changed)
    Q_PROPERTY(QVariantList listBreakdown READ listBreakdown NOTIFY changed)
    Q_PROPERTY(QVariantList recentSessions READ recentSessions NOTIFY changed)
    Q_PROPERTY(QVariantList insights READ insights NOTIFY changed)

public:
    AnalyticsManager(TaskStore *store, RoutineManager *routine, FocusManager *focus, QObject *parent = nullptr);

    bool active() const { return m_active; }
    void setActive(bool active);
    bool isDirty() const { return m_dirty; }

    int periodDays() const { return m_periodDays; }
    void setPeriodDays(int days);

    QVariantMap summary() const;
    QVariantList dailySeries() const;
    QVariantList quadrantBreakdown() const;
    QVariantList listBreakdown() const;
    QVariantList recentSessions() const;
    QVariantList insights() const;

public Q_SLOTS:
    void rebuild();

Q_SIGNALS:
    void activeChanged();
    void periodDaysChanged();
    void changed();

private:
    void markDirtyOrRebuild();
    void ensureFresh() const;
    void rebuildInternal(bool emitChanged);

    TaskStore *m_store;
    RoutineManager *m_routine;
    FocusManager *m_focus;
    bool m_active = false;
    bool m_dirty = true;
    int m_periodDays = 7;

    QVariantMap m_summary;
    QVariantList m_dailySeries;
    QVariantList m_quadrantBreakdown;
    QVariantList m_listBreakdown;
    QVariantList m_recentSessions;
    QVariantList m_insights;
};
