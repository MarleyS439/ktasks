// SPDX-License-Identifier: GPL-3.0-or-later
#include "analyticsmanager.h"
#include "database.h"
#include "eisenhowermanager.h"
#include "focusmanager.h"
#include "routinemanager.h"
#include "taskstore.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QHash>
#include <QLocale>
#include <QSet>
#include <algorithm>

using namespace KTasks;

AnalyticsManager::AnalyticsManager(TaskStore *store, RoutineManager *routine, FocusManager *focus, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_routine(routine)
    , m_focus(focus)
{
    connect(m_store, &TaskStore::changed, this, &AnalyticsManager::markDirtyOrRebuild);
    connect(m_routine, &RoutineManager::changed, this, &AnalyticsManager::markDirtyOrRebuild);
    connect(m_focus, &FocusManager::sessionsChanged, this, &AnalyticsManager::markDirtyOrRebuild);
}

void AnalyticsManager::setActive(bool active)
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
        m_summary.clear();
        m_dailySeries.clear();
        m_dailySeries.squeeze();
        m_quadrantBreakdown.clear();
        m_listBreakdown.clear();
        m_recentSessions.clear();
        m_insights.clear();
        m_dirty = true;
    }
}

void AnalyticsManager::markDirtyOrRebuild()
{
    m_dirty = true;
    if (m_active) {
        rebuildInternal(true);
    }
}

void AnalyticsManager::ensureFresh() const
{
    if (m_dirty) {
        const_cast<AnalyticsManager *>(this)->rebuildInternal(false);
    }
}

QVariantMap AnalyticsManager::summary() const
{
    ensureFresh();
    return m_summary;
}

QVariantList AnalyticsManager::dailySeries() const
{
    ensureFresh();
    return m_dailySeries;
}

QVariantList AnalyticsManager::quadrantBreakdown() const
{
    ensureFresh();
    return m_quadrantBreakdown;
}

QVariantList AnalyticsManager::listBreakdown() const
{
    ensureFresh();
    return m_listBreakdown;
}

QVariantList AnalyticsManager::recentSessions() const
{
    ensureFresh();
    return m_recentSessions;
}

QVariantList AnalyticsManager::insights() const
{
    ensureFresh();
    return m_insights;
}

void AnalyticsManager::setPeriodDays(int days)
{
    const int normalized = (days <= 1) ? 1 : (days <= 7 ? 7 : 30);
    if (m_periodDays != normalized) {
        m_periodDays = normalized;
        Q_EMIT periodDaysChanged();
        markDirtyOrRebuild();
    }
}

void AnalyticsManager::rebuild()
{
    rebuildInternal(true);
}

void AnalyticsManager::rebuildInternal(bool emitChanged)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const QLocale locale;

    const QDate startDate = today.addDays(-(m_periodDays - 1));
    const QDate prevStartDate = startDate.addDays(-m_periodDays);
    const QDate prevEndDate = startDate.addDays(-1);
    const QDateTime fetchSince(today.addDays(-90), QTime(0, 0));

    // Fetch sessions up to 90 days for streaks & comparisons
    const QList<FocusSession> allSessions = m_store->database()->focusSessions(fetchSince.toUTC(), 1000);

    // Map day -> counts
    QHash<QDate, int> completedByDay;
    QHash<QDate, int> focusSecsByDay;
    QHash<QDate, int> pomodorosByDay;
    QHash<QDate, int> routineDoneByDay;
    QSet<QDate> activeDays;

    int openTasks = 0;
    int overdueTasks = 0;
    int completedPeriod = 0;
    int prevCompleted = 0;
    int unclassifiedOpen = 0;
    int openQ[5] = {0};
    int doneQ[5] = {0};
    int trackedSecsQ[5] = {0};
    int estTasksCount = 0;
    int estTotalMin = 0;
    double actualOnEstMin = 0.0;

    QHash<qint64, int> listOpen;
    QHash<qint64, int> listDone;
    QHash<qint64, int> listFocusSecs;

    for (const Task &t : m_store->tasks()) {
        const int effQ = EisenhowerManager::classifyBucket(t, now, 48);
        if (t.isDone()) {
            const QDate compDay = t.completedAt.isValid() ? t.completedAt.toLocalTime().date() : QDate();
            if (compDay.isValid()) {
                completedByDay[compDay] += 1;
                activeDays.insert(compDay);
                if (compDay >= startDate && compDay <= today) {
                    ++completedPeriod;
                    ++doneQ[effQ];
                    ++listDone[t.listId];
                } else if (compDay >= prevStartDate && compDay <= prevEndDate) {
                    ++prevCompleted;
                }
            }
            if (t.estimatedMinutes > 0 && t.trackedSeconds > 0) {
                ++estTasksCount;
                estTotalMin += t.estimatedMinutes;
                actualOnEstMin += t.trackedSeconds / 60.0;
            }
        } else {
            ++openTasks;
            if (effQ == 0) {
                ++unclassifiedOpen;
            } else {
                ++openQ[effQ];
            }
            ++listOpen[t.listId];
            if (t.due.isValid()) {
                const bool over = t.allDay ? t.due.date() < today : t.due < now;
                if (over) {
                    ++overdueTasks;
                }
            }
        }
    }

    int totalFocusSecs = 0;
    int periodPomodoros = 0;
    int periodSessions = 0;
    for (const FocusSession &s : allSessions) {
        if (s.mode == FocusMode::Break) {
            continue;
        }
        const QDate d = s.endedAt.toLocalTime().date();
        if (!d.isValid()) {
            continue;
        }
        focusSecsByDay[d] += s.durationSeconds;
        if (s.completedPomodoro) {
            pomodorosByDay[d] += 1;
        }
        if (s.durationSeconds >= 30) {
            activeDays.insert(d);
        }
        if (d >= startDate && d <= today) {
            totalFocusSecs += s.durationSeconds;
            ++periodSessions;
            if (s.completedPomodoro) {
                ++periodPomodoros;
            }
            int q = s.quadrant;
            if (q <= 0 && s.taskId > 0) {
                if (const Task *t = m_store->task(s.taskId)) {
                    q = EisenhowerManager::classifyBucket(*t, now, 48);
                }
            }
            if (q >= 1 && q <= 4) {
                trackedSecsQ[q] += s.durationSeconds;
            }
            if (s.listId > 0) {
                listFocusSecs[s.listId] += s.durationSeconds;
            }
        }
    }

    // Routine adherence across period
    int routineDoneTotal = 0;
    int routinePlannedTotal = 0;
    for (QDate d = startDate; d <= today; d = d.addDays(1)) {
        const auto blocks = m_routine->blocksForDate(d);
        for (const RoutineBlock &b : blocks) {
            const auto st = m_routine->statusForBlock(b.id, d);
            if (st != RoutineLogStatus::Skipped) {
                ++routinePlannedTotal;
                if (st == RoutineLogStatus::Done) {
                    ++routineDoneTotal;
                    routineDoneByDay[d] += 1;
                    activeDays.insert(d);
                }
            }
        }
    }

    // Current streak of productive days
    int streakDays = 0;
    QDate cursor = activeDays.contains(today) ? today : today.addDays(-1);
    while (activeDays.contains(cursor) && streakDays < 365) {
        ++streakDays;
        cursor = cursor.addDays(-1);
    }

    // Daily chart series (for periodDays == 1, show last 7 days trend on chart so bars stay meaningful!)
    const int chartDays = (m_periodDays == 1) ? 7 : m_periodDays;
    const QDate chartStart = today.addDays(-(chartDays - 1));
    int maxDailyMinutes = 1;
    int maxDailyCompleted = 1;
    int weekdayScore[8] = {0};

    m_dailySeries.clear();
    for (int i = 0; i < chartDays; ++i) {
        const QDate d = chartStart.addDays(i);
        const int mins = qRound(focusSecsByDay.value(d) / 60.0);
        const int comp = completedByDay.value(d);
        const int poms = pomodorosByDay.value(d);
        const int rDone = routineDoneByDay.value(d);
        maxDailyMinutes = qMax(maxDailyMinutes, mins);
        maxDailyCompleted = qMax(maxDailyCompleted, comp);
        weekdayScore[d.dayOfWeek()] += comp * 15 + mins + rDone * 10;

        m_dailySeries.append(QVariantMap{
            {QStringLiteral("dateIso"), d.toString(Qt::ISODate)},
            {QStringLiteral("shortLabel"), chartDays <= 7 ? locale.dayName(d.dayOfWeek(), QLocale::ShortFormat) : QString::number(d.day())},
            {QStringLiteral("fullLabel"), locale.toString(d, QStringLiteral("ddd, d MMM"))},
            {QStringLiteral("isToday"), d == today},
            {QStringLiteral("completedCount"), comp},
            {QStringLiteral("focusMinutes"), mins},
            {QStringLiteral("pomodoros"), poms},
            {QStringLiteral("routineDone"), rDone},
        });
    }

    int bestWeekday = 0;
    int bestWeekdayVal = 0;
    for (int wd = 1; wd <= 7; ++wd) {
        if (weekdayScore[wd] > bestWeekdayVal) {
            bestWeekdayVal = weekdayScore[wd];
            bestWeekday = wd;
        }
    }

    // Strategic focus (Q1 + Q2 vs all Q1..Q4 completed / focused)
    const int qTotalComp = doneQ[1] + doneQ[2] + doneQ[3] + doneQ[4];
    const int qTotalSecs = trackedSecsQ[1] + trackedSecsQ[2] + trackedSecsQ[3] + trackedSecsQ[4];
    int strategicFocusPct = 0;
    int q2PlanningPct = 0;
    if (qTotalSecs > 0) {
        strategicFocusPct = qRound(100.0 * double(trackedSecsQ[1] + trackedSecsQ[2]) / double(qTotalSecs));
        q2PlanningPct = qRound(100.0 * double(trackedSecsQ[2]) / double(qTotalSecs));
    } else if (qTotalComp > 0) {
        strategicFocusPct = qRound(100.0 * double(doneQ[1] + doneQ[2]) / double(qTotalComp));
        q2PlanningPct = qRound(100.0 * double(doneQ[2]) / double(qTotalComp));
    }

    const int completionRate = (completedPeriod + openTasks) > 0
        ? qRound(100.0 * double(completedPeriod) / double(completedPeriod + openTasks))
        : 0;
    const int routineConsistencyPct = routinePlannedTotal > 0
        ? qRound(100.0 * double(routineDoneTotal) / double(routinePlannedTotal))
        : 0;
    int estimationAccuracyPct = 0;
    if (estTotalMin > 0 && actualOnEstMin > 0.0) {
        const double ratio = qMin(double(estTotalMin), actualOnEstMin) / qMax(double(estTotalMin), actualOnEstMin);
        estimationAccuracyPct = qRound(100.0 * ratio);
    }

    m_summary = QVariantMap{
        {QStringLiteral("completedTasks"), completedPeriod},
        {QStringLiteral("prevCompletedTasks"), prevCompleted},
        {QStringLiteral("openTasks"), openTasks},
        {QStringLiteral("overdueTasks"), overdueTasks},
        {QStringLiteral("completionRate"), completionRate},
        {QStringLiteral("trackedSeconds"), totalFocusSecs},
        {QStringLiteral("trackedText"), FocusManager::formatDuration(totalFocusSecs)},
        {QStringLiteral("avgDailyFocusMinutes"), qRound(totalFocusSecs / 60.0 / double(qMax(1, m_periodDays)))},
        {QStringLiteral("pomodorosCount"), periodPomodoros},
        {QStringLiteral("focusSessionsCount"), periodSessions},
        {QStringLiteral("estimatedTasksCount"), estTasksCount},
        {QStringLiteral("estimatedTotalMin"), estTotalMin},
        {QStringLiteral("actualOnEstimatedMin"), qRound(actualOnEstMin)},
        {QStringLiteral("estimationAccuracyPct"), estimationAccuracyPct},
        {QStringLiteral("unclassifiedOpen"), unclassifiedOpen},
        {QStringLiteral("strategicFocusPct"), strategicFocusPct},
        {QStringLiteral("q2PlanningPct"), q2PlanningPct},
        {QStringLiteral("routineDoneCount"), routineDoneTotal},
        {QStringLiteral("routineTotalCount"), routinePlannedTotal},
        {QStringLiteral("routineConsistencyPct"), routineConsistencyPct},
        {QStringLiteral("streakDays"), streakDays},
        {QStringLiteral("maxDailyMinutes"), maxDailyMinutes},
        {QStringLiteral("maxDailyCompleted"), maxDailyCompleted},
        {QStringLiteral("bestWeekdayName"), bestWeekday > 0 ? locale.dayName(bestWeekday, QLocale::LongFormat) : QString()},
    };

    // Quadrant breakdown Q1..Q4
    const QString qTitles[5] = {
        QString(),
        i18nc("quadrant short label", "Q1 · Do Now"),
        i18nc("quadrant short label", "Q2 · Schedule"),
        i18nc("quadrant short label", "Q3 · Delegate"),
        i18nc("quadrant short label", "Q4 · Eliminate"),
    };
    const QString qColors[5] = {QString(), QStringLiteral("#e93d58"), QStringLiteral("#2980b9"), QStringLiteral("#f67400"), QStringLiteral("#7f8c8d")};
    m_quadrantBreakdown.clear();
    const int weightDenom = (qTotalSecs > 0) ? qTotalSecs : qMax(1, qTotalComp + openQ[1] + openQ[2] + openQ[3] + openQ[4]);
    for (int q = 1; q <= 4; ++q) {
        const int num = (qTotalSecs > 0) ? trackedSecsQ[q] : (doneQ[q] + openQ[q]);
        m_quadrantBreakdown.append(QVariantMap{
            {QStringLiteral("quadrant"), q},
            {QStringLiteral("label"), qTitles[q]},
            {QStringLiteral("color"), qColors[q]},
            {QStringLiteral("openCount"), openQ[q]},
            {QStringLiteral("completedCount"), doneQ[q]},
            {QStringLiteral("trackedSeconds"), trackedSecsQ[q]},
            {QStringLiteral("trackedText"), FocusManager::formatDuration(trackedSecsQ[q])},
            {QStringLiteral("sharePct"), weightDenom > 0 ? qRound(100.0 * double(num) / double(weightDenom)) : 0},
        });
    }

    // List breakdown sorted by tracked seconds then completed tasks
    struct ListRow {
        const TaskList *list;
        int open;
        int done;
        int secs;
    };
    QList<ListRow> rows;
    int maxListWeight = 1;
    for (const TaskList &l : m_store->lists()) {
        const int op = listOpen.value(l.id);
        const int dn = listDone.value(l.id);
        const int sc = listFocusSecs.value(l.id);
        if (op == 0 && dn == 0 && sc == 0) {
            continue;
        }
        maxListWeight = qMax(maxListWeight, sc > 0 ? sc : (dn * 60 + op * 10));
        rows.append({&l, op, dn, sc});
    }
    std::sort(rows.begin(), rows.end(), [](const ListRow &a, const ListRow &b) {
        if (a.secs != b.secs) {
            return a.secs > b.secs;
        }
        if (a.done != b.done) {
            return a.done > b.done;
        }
        return a.open > b.open;
    });
    m_listBreakdown.clear();
    for (int i = 0; i < rows.size() && i < 6; ++i) {
        const auto &r = rows.at(i);
        const int w = r.secs > 0 ? r.secs : (r.done * 60 + r.open * 10);
        m_listBreakdown.append(QVariantMap{
            {QStringLiteral("listId"), r.list->id},
            {QStringLiteral("name"), r.list->name},
            {QStringLiteral("color"), r.list->color.isEmpty() ? QStringLiteral("#3daee9") : r.list->color},
            {QStringLiteral("openCount"), r.open},
            {QStringLiteral("completedCount"), r.done},
            {QStringLiteral("trackedSeconds"), r.secs},
            {QStringLiteral("trackedText"), FocusManager::formatDuration(r.secs)},
            {QStringLiteral("sharePct"), qRound(100.0 * double(w) / double(maxListWeight))},
        });
    }

    m_recentSessions = m_focus->recentSessions(12);

    // Automated actionable productivity insights
    m_insights.clear();
    auto addInsight = [&](const QString &severity, const QString &icon, const QString &title, const QString &detail, const QString &actionSection = QString()) {
        m_insights.append(QVariantMap{
            {QStringLiteral("severity"), severity},
            {QStringLiteral("icon"), icon},
            {QStringLiteral("title"), title},
            {QStringLiteral("detail"), detail},
            {QStringLiteral("actionSection"), actionSection},
        });
    };

    if (unclassifiedOpen > 0) {
        addInsight(QStringLiteral("info"),
                   QStringLiteral("view-grid"),
                   i18ncp("insight title", "%1 task awaits prioritization", "%1 tasks await prioritization", unclassifiedOpen),
                   i18nc("insight detail", "Organize open tasks inside the Priority Matrix so high-impact work gets scheduled before urgent distractions."),
                   QStringLiteral("matrix"));
    }

    if (overdueTasks > 0) {
        addInsight(QStringLiteral("warning"),
                   QStringLiteral("data-warning"),
                   i18ncp("insight title", "%1 overdue task needs review", "%1 overdue tasks need review", overdueTasks),
                   i18nc("insight detail", "Either move urgent deadlines to Q1 and focus now, or reschedule unrealistic dates."),
                   QStringLiteral("tasks"));
    }

    if (qTotalComp + qTotalSecs > 0) {
        if (q2PlanningPct >= 35) {
            addInsight(QStringLiteral("positive"),
                       QStringLiteral("emblem-favorite"),
                       i18nc("insight title", "Strong strategic Q2 focus (%1%)", q2PlanningPct),
                       i18nc("insight detail", "You are dedicating real execution time to important goals before they turn into emergencies."));
        } else if (doneQ[1] > doneQ[2] && openQ[1] > openQ[2]) {
            addInsight(QStringLiteral("warning"),
                       QStringLiteral("chronometer"),
                       i18nc("insight title", "High firefighting load in Q1"),
                       i18nc("insight detail", "Most activity is going to urgent Q1 fires. Blocking time for Q2 work helps prevent future crunches."),
                       QStringLiteral("matrix"));
        }
    }

    if (estTasksCount >= 2 && estTotalMin > 0) {
        const double diffPct = 100.0 * (actualOnEstMin - double(estTotalMin)) / double(estTotalMin);
        if (qAbs(diffPct) <= 15.0) {
            addInsight(QStringLiteral("positive"),
                       QStringLiteral("chronometer-lap"),
                       i18nc("insight title", "Time estimates match reality (%1% accurate)", estimationAccuracyPct),
                       i18nc("insight detail", "Across %1 estimated tasks, actual execution time stayed close to your plan.", estTasksCount));
        } else if (diffPct > 15.0) {
            addInsight(QStringLiteral("info"),
                       QStringLiteral("chronometer-lap"),
                       i18nc("insight title", "Tasks take about %1% longer than estimated", qRound(diffPct)),
                       i18nc("insight detail", "Try adding a 20–30% time buffer when scheduling tasks in your day planner."));
        } else {
            addInsight(QStringLiteral("positive"),
                       QStringLiteral("chronometer-lap"),
                       i18nc("insight title", "Finishing %1% faster than estimated", qRound(-diffPct)),
                       i18nc("insight detail", "Your focused sessions are finishing ahead of planned time estimates."));
        }
    }

    if (routinePlannedTotal > 0) {
        addInsight(routineConsistencyPct >= 70 ? QStringLiteral("positive") : QStringLiteral("info"),
                   QStringLiteral("view-calendar-week"),
                   i18nc("insight title", "Routine consistency at %1%", routineConsistencyPct),
                   i18nc("insight detail", "%1 of %2 scheduled routine blocks completed in this period.", routineDoneTotal, routinePlannedTotal),
                   QStringLiteral("routine"));
    }

    if (bestWeekday > 0) {
        addInsight(QStringLiteral("info"),
                   QStringLiteral("view-statistics"),
                   i18nc("insight title", "Most productive day: %1", locale.dayName(bestWeekday, QLocale::LongFormat)),
                   streakDays > 1
                       ? i18nc("insight detail", "Current active streak: %1 consecutive days.", streakDays)
                       : i18nc("insight detail", "Start a Pomodoro timer on your top Q1/Q2 task to build daily momentum."));
    }

    m_dirty = false;
    if (emitChanged) {
        Q_EMIT changed();
    }
}
