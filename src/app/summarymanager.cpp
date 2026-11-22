// SPDX-License-Identifier: GPL-3.0-or-later
#include "summarymanager.h"
#include "analyticsmanager.h"
#include "database.h"
#include "eisenhowermanager.h"
#include "focusmanager.h"
#include "journalmanager.h"
#include "routinemanager.h"
#include "settings.h"
#include "taskmodel.h"
#include "taskstore.h"

#include <KCalendarCore/Event>
#include <KCalendarCore/FileStorage>
#include <KCalendarCore/ICalFormat>
#include <KCalendarCore/MemoryCalendar>
#include <KCalendarCore/OccurrenceIterator>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KUser>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrlQuery>
#include <algorithm>

#ifdef KTASKS_HAVE_AKONADI
#include <Akonadi/CollectionFetchJob>
#include <Akonadi/CollectionFetchScope>
#include <Akonadi/EntityDisplayAttribute>
#include <Akonadi/ItemFetchJob>
#include <Akonadi/ItemFetchScope>
#include <Akonadi/ServerManager>
#endif

using namespace KTasks;

SummaryManager::SummaryManager(TaskStore *store, RoutineManager *routine, AnalyticsManager *analytics, FocusManager *focus, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_routine(routine)
    , m_analytics(analytics)
    , m_focus(focus)
    , m_settings(settings)
{
    connect(m_store, &TaskStore::changed, this, &SummaryManager::markDirtyOrRebuild);
    connect(m_routine, &RoutineManager::changed, this, &SummaryManager::markDirtyOrRebuild);
    connect(m_focus, &FocusManager::sessionsChanged, this, &SummaryManager::markDirtyOrRebuild);
    connect(m_settings, &Settings::integrateKOrganizerChanged, this, &SummaryManager::markDirtyOrRebuild);
    connect(m_settings, &Settings::extraIcsPathsChanged, this, &SummaryManager::markDirtyOrRebuild);
    connect(m_settings, &Settings::profileNameChanged, this, &SummaryManager::markDirtyOrRebuild);

    // Refresh weather every 1 hour automatically while the app runs
    m_weatherTimer.setInterval(60 * 60 * 1000);
    m_weatherTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_weatherTimer, &QTimer::timeout, this, [this] {
        refreshWeather(true);
    });
    if (!QStandardPaths::isTestModeEnabled()) {
        m_weatherTimer.start();
    }
}

SummaryManager::~SummaryManager() = default;

void SummaryManager::setJournalManager(JournalManager *journal)
{
    if (m_journal == journal) {
        return;
    }
    m_journal = journal;
    if (m_journal) {
        connect(m_journal, &JournalManager::entriesChanged, this, &SummaryManager::markDirtyOrRebuild);
        connect(m_journal, &JournalManager::notebooksChanged, this, &SummaryManager::markDirtyOrRebuild);
        connect(m_journal, &JournalManager::unlockedChanged, this, &SummaryManager::markDirtyOrRebuild);
    }
    markDirtyOrRebuild();
}

void SummaryManager::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    Q_EMIT activeChanged();
    if (m_active) {
        if (m_dirty) {
            rebuildInternal(true, false);
        }
        refreshWeather(false);
    } else {
        m_priorityTasks.clear();
        m_priorityTasks.squeeze();
        m_todayRoutines.clear();
        m_externalEvents.clear();
        m_recentJournals.clear();
        m_topInsights.clear();
        m_stats.clear();
        m_nextSchedule.clear();
        m_citySearchResults.clear();
        m_dirty = true;
    }
}

void SummaryManager::markDirtyOrRebuild()
{
    m_dirty = true;
    if (m_active) {
        rebuildInternal(true, false);
    }
}

void SummaryManager::ensureFresh() const
{
    if (m_dirty) {
        const_cast<SummaryManager *>(this)->rebuildInternal(false, false);
    }
}

QString SummaryManager::greetingText() const { ensureFresh(); return m_greetingText; }
QString SummaryManager::weekdayText() const { ensureFresh(); return m_weekdayText; }
QString SummaryManager::fullDateText() const { ensureFresh(); return m_fullDateText; }
QVariantMap SummaryManager::stats() const { ensureFresh(); return m_stats; }
QVariantList SummaryManager::priorityTasks() const { ensureFresh(); return m_priorityTasks; }
QVariantList SummaryManager::todayRoutines() const { ensureFresh(); return m_todayRoutines; }
QVariantList SummaryManager::externalEvents() const { ensureFresh(); return m_externalEvents; }
QString SummaryManager::externalCalStatus() const { ensureFresh(); return m_externalCalStatus; }
QVariantMap SummaryManager::nextSchedule() const { ensureFresh(); return m_nextSchedule; }
QVariantList SummaryManager::recentJournals() const { ensureFresh(); return m_recentJournals; }
QVariantList SummaryManager::topInsights() const { ensureFresh(); return m_topInsights; }

void SummaryManager::refresh()
{
    rebuildInternal(true, true);
    refreshWeather(true);
}

static QString userDisplayName(const Settings *settings)
{
    const QString custom = settings ? settings->profileName().trimmed() : QString();
    if (!custom.isEmpty()) {
        return custom;
    }
    const KUser user;
    const QString full = user.property(KUser::FullName).toString().trimmed();
    return full.isEmpty() ? user.loginName() : full;
}

static void appendCalendarOccurrences(const KCalendarCore::Calendar::Ptr &cal,
                                      const QString &sourceName,
                                      const QDate &today,
                                      const QDateTime &nowLocal,
                                      QList<SummaryManager::ExternalEventRecord> &out)
{
    const QDate endDate = today.addDays(3);
    const QDateTime rangeStart(today, QTime(0, 0), QTimeZone::systemTimeZone());
    const QDateTime rangeEnd(endDate, QTime(23, 59, 59), QTimeZone::systemTimeZone());
    const QLocale locale;

    KCalendarCore::OccurrenceIterator occIt(*cal, rangeStart, rangeEnd);
    while (occIt.hasNext()) {
        occIt.next();
        const KCalendarCore::Incidence::Ptr inc = occIt.incidence();
        if (!inc || inc->type() != KCalendarCore::Incidence::TypeEvent) {
            continue;
        }
        const KCalendarCore::Event::Ptr ev = inc.dynamicCast<KCalendarCore::Event>();
        if (!ev) {
            continue;
        }
        const QString summary = ev->summary().trimmed();
        if (summary.isEmpty()) {
            continue;
        }
        const bool allDay = ev->allDay();
        const QDateTime occStart = allDay
            ? QDateTime(occIt.occurrenceStartDate().date(), QTime(0, 0))
            : occIt.occurrenceStartDate().toLocalTime();
        QDateTime occEnd = occIt.occurrenceEndDate();
        if (!occEnd.isValid()) {
            occEnd = occStart.addSecs(allDay ? 86340 : 3600);
        } else if (!allDay) {
            occEnd = occEnd.toLocalTime();
        }
        const QDate evDate = occStart.date();
        if (evDate < today || evDate > endDate) {
            continue;
        }

        const QString dateLabel = evDate == today ? i18nc("date label", "Today")
            : evDate == today.addDays(1)          ? i18nc("date label", "Tomorrow")
                                                  : i18nc("short day label", "%1 %2",
                                                          locale.dayName(evDate.dayOfWeek(), QLocale::ShortFormat),
                                                          evDate.day());
        const QString startTimeText = allDay ? QString() : occStart.time().toString(QStringLiteral("HH:mm"));
        const QString timeRange = allDay
            ? i18nc("all day event", "All day")
            : (locale.toString(occStart.time(), QLocale::ShortFormat)
               + (occEnd.isValid() && occEnd.time() != occStart.time()
                      ? QStringLiteral("–") + locale.toString(occEnd.time(), QLocale::ShortFormat)
                      : QString()));

        int startMin = -1;
        int endMin = -1;
        if (!allDay) {
            startMin = occStart.time().hour() * 60 + occStart.time().minute();
            endMin = (occEnd.date() > evDate) ? 1440 : (occEnd.time().hour() * 60 + occEnd.time().minute());
            if (endMin <= startMin) {
                endMin = qMin(1440, startMin + 30);
            }
        }

        SummaryManager::ExternalEventRecord rec;
        rec.startSort = occStart;
        rec.date = evDate;
        rec.startMinute = startMin;
        rec.endMinute = endMin;
        rec.allDay = allDay;
        rec.uid = ev->uid();
        rec.title = summary;
        rec.location = ev->location().trimmed();
        rec.sourceName = sourceName;
        rec.isToday = (evDate == today);
        rec.isPast = !allDay && occEnd.isValid() && occEnd < nowLocal;
        rec.dateIso = evDate.toString(Qt::ISODate);
        rec.dateLabel = dateLabel;
        rec.timeRange = timeRange;
        rec.startTimeText = startTimeText;
        out.append(rec);
    }
}

QString SummaryManager::formatRemainingText(int startMinute, int endMinute, int nowMinute)
{
    if (startMinute < 0) {
        return {};
    }
    if (nowMinute >= startMinute && nowMinute < endMinute) {
        return i18nc("event timing", "Now");
    }
    if (startMinute > nowMinute) {
        const int diff = startMinute - nowMinute;
        if (diff < 60) {
            return i18nc("event countdown minutes", "In %1 min", diff);
        }
        const int hrs = diff / 60;
        const int mins = diff % 60;
        return mins > 0 ? i18nc("event countdown h+m", "In %1h %2m", hrs, mins)
                        : i18nc("event countdown h", "In %1h", hrs);
    }
    return {};
}

void SummaryManager::rebuildInternal(bool emitChanged, bool forceExternalRefresh)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate today = now.date();
    const QTime curTime = now.time();
    const QLocale locale;

    const QString name = userDisplayName(m_settings);
    const QString firstName = name.section(QLatin1Char(' '), 0, 0).trimmed();
    const QString who = firstName.isEmpty() ? name : firstName;

    if (curTime.hour() < 12) {
        m_greetingText = who.isEmpty() ? i18nc("greeting", "Good morning") : i18nc("greeting with name", "Good morning, %1!", who);
    } else if (curTime.hour() < 18) {
        m_greetingText = who.isEmpty() ? i18nc("greeting", "Good afternoon") : i18nc("greeting with name", "Good afternoon, %1!", who);
    } else {
        m_greetingText = who.isEmpty() ? i18nc("greeting", "Good evening") : i18nc("greeting with name", "Good evening, %1!", who);
    }

    QString wd = locale.dayName(today.dayOfWeek(), QLocale::LongFormat);
    if (!wd.isEmpty()) {
        wd[0] = wd[0].toUpper();
    }
    m_weekdayText = wd;
    QString dateFmt = locale.dateFormat(QLocale::LongFormat);
    dateFmt.remove(QStringLiteral("dddd, "));
    dateFmt.remove(QStringLiteral(", dddd"));
    dateFmt.remove(QStringLiteral("dddd "));
    dateFmt.remove(QStringLiteral("dddd"));
    dateFmt = dateFmt.trimmed();
    QString dateText = dateFmt.isEmpty() ? locale.toString(today, QLocale::ShortFormat) : locale.toString(today, dateFmt);
    if (!wd.isEmpty() && dateText.startsWith(wd, Qt::CaseInsensitive)) {
        dateText = dateText.mid(wd.size()).trimmed();
        if (dateText.startsWith(QLatin1Char(',')) || dateText.startsWith(QLatin1Char('-'))) {
            dateText = dateText.mid(1).trimmed();
        }
    }
    m_fullDateText = dateText;

    // Task KPIs & prioritized upcoming list
    int todayCount = 0;
    int doneTodayCount = 0;
    int overdueCount = 0;
    int openCount = 0;
    int q1Count = 0;
    int q2Count = 0;
    int triageCount = 0;
    const int urgencyHours = qMax(12, m_settings->eisenhowerUrgencyHours());

    struct RankedTask {
        const Task *t = nullptr;
        int bucket = 0;
        bool overdue = false;
        bool dueToday = false;
        int rank = 99;
    };
    QList<RankedTask> candidates;

    for (const Task &t : m_store->tasks()) {
        if (t.isDone()) {
            if (t.completedAt.isValid() && t.completedAt.toLocalTime().date() == today) {
                ++doneTodayCount;
            }
            continue;
        }
        ++openCount;
        const bool isToday = TaskModel::matchesView(t, QStringLiteral("today"), now);
        const bool isOverdue = TaskModel::matchesView(t, QStringLiteral("overdue"), now);
        if (isToday) {
            ++todayCount;
        }
        if (isOverdue) {
            ++overdueCount;
        }
        const int b = EisenhowerManager::classifyBucket(t, now, urgencyHours);
        if (b == 0) {
            ++triageCount;
        } else if (b == 1) {
            ++q1Count;
        } else if (b == 2) {
            ++q2Count;
        }

        int rank = 50;
        if (isOverdue) {
            rank = 0;
        } else if (b == 1 && isToday) {
            rank = 1;
        } else if (isToday) {
            rank = 2;
        } else if (b == 1) {
            rank = 3;
        } else if (b == 2) {
            rank = 4;
        } else if (t.due.isValid() && (t.allDay ? t.due.date() : t.due.toLocalTime().date()) <= today.addDays(7)) {
            rank = 5;
        }
        if (rank <= 5) {
            candidates.append({&t, b, isOverdue, isToday && !isOverdue, rank});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const RankedTask &a, const RankedTask &b) {
        if (a.rank != b.rank) {
            return a.rank < b.rank;
        }
        if (a.t->due.isValid() != b.t->due.isValid()) {
            return a.t->due.isValid();
        }
        if (a.t->due != b.t->due) {
            return a.t->due < b.t->due;
        }
        if (a.t->sortOrder != b.t->sortOrder) {
            return a.t->sortOrder < b.t->sortOrder;
        }
        return a.t->id < b.t->id;
    });

    m_priorityTasks.clear();
    const int limitTasks = qMin(8, int(candidates.size()));
    m_priorityTasks.reserve(limitTasks);
    qlonglong topTaskId = 0;
    QString topTaskTitle;
    for (int i = 0; i < limitTasks; ++i) {
        const RankedTask &rt = candidates.at(i);
        const Task *t = rt.t;
        if (i == 0) {
            topTaskId = t->id;
            topTaskTitle = t->title;
        }
        const TaskList *l = m_store->list(t->listId);
        m_priorityTasks.append(QVariantMap{
            {QStringLiteral("id"), t->id},
            {QStringLiteral("title"), t->title},
            {QStringLiteral("quadrant"), rt.bucket},
            {QStringLiteral("dueText"), TaskModel::formatDue(*t, now)},
            {QStringLiteral("overdue"), rt.overdue},
            {QStringLiteral("dueToday"), rt.dueToday},
            {QStringLiteral("listName"), l ? l->name : QString()},
            {QStringLiteral("listColor"), l ? l->color : QString()},
            {QStringLiteral("trackedText"), t->trackedSeconds > 0 ? FocusManager::formatDuration(t->trackedSeconds) : QString()},
            {QStringLiteral("readOnly"), l && l->readOnly},
        });
    }

    // Today's Routine (separate from external calendar!)
    m_todayRoutines.clear();
    const QList<RoutineBlock> blocks = m_routine->blocksForDate(today);
    int routineDone = 0;
    int routinePlanned = 0;
    for (const RoutineBlock &b : blocks) {
        const RoutineLogStatus st = m_routine->statusForBlock(b.id, today);
        if (st != RoutineLogStatus::Skipped) {
            ++routinePlanned;
            if (st == RoutineLogStatus::Done) {
                ++routineDone;
            }
        }
        const int endMin = qMin(1440, b.startMinute + b.durationMinutes);
        const QString range = QStringLiteral("%1:%2–%3:%4")
                                  .arg(b.startMinute / 60, 2, 10, QLatin1Char('0'))
                                  .arg(b.startMinute % 60, 2, 10, QLatin1Char('0'))
                                  .arg(endMin == 1440 ? 23 : endMin / 60, 2, 10, QLatin1Char('0'))
                                  .arg(endMin == 1440 ? 59 : endMin % 60, 2, 10, QLatin1Char('0'));
        m_todayRoutines.append(QVariantMap{
            {QStringLiteral("id"), b.id},
            {QStringLiteral("title"), b.title},
            {QStringLiteral("timeRange"), range},
            {QStringLiteral("color"), b.color},
            {QStringLiteral("status"), int(st)},
            {QStringLiteral("startMinute"), b.startMinute},
            {QStringLiteral("endMinute"), endMin},
            {QStringLiteral("hasConflict"), false},
        });
    }

    // Today's focus sessions
    const QDateTime startOfDayUtc = QDateTime(today, QTime(0, 0), QTimeZone::systemTimeZone()).toUTC();
    const QList<FocusSession> todaySessions = m_store->database()->focusSessions(startOfDayUtc, 200);
    int focusSecsToday = 0;
    int pomodorosToday = 0;
    for (const FocusSession &s : todaySessions) {
        if (s.mode != FocusMode::Break) {
            focusSecsToday += s.durationSeconds;
            if (s.completedPomodoro) {
                ++pomodorosToday;
            }
        }
    }

    const int analyticsStreak = m_analytics->summary().value(QStringLiteral("streakDays")).toInt();
    const int totalUnits = (todayCount + doneTodayCount) + routinePlanned;
    const int doneUnits = doneTodayCount + routineDone;
    const int dailyProgressPct = totalUnits > 0 ? qBound(0, qRound(100.0 * double(doneUnits) / double(totalUnits)), 100) : 0;

    m_stats = QVariantMap{
        {QStringLiteral("todayCount"), todayCount},
        {QStringLiteral("doneTodayCount"), doneTodayCount},
        {QStringLiteral("overdueCount"), overdueCount},
        {QStringLiteral("openCount"), openCount},
        {QStringLiteral("q1Count"), q1Count},
        {QStringLiteral("q2Count"), q2Count},
        {QStringLiteral("triageCount"), triageCount},
        {QStringLiteral("topTaskId"), topTaskId},
        {QStringLiteral("topTaskTitle"), topTaskTitle},
        {QStringLiteral("routineDoneToday"), routineDone},
        {QStringLiteral("routineTotalToday"), blocks.size()},
        {QStringLiteral("focusMinutesToday"), qRound(focusSecsToday / 60.0)},
        {QStringLiteral("focusTextToday"), FocusManager::formatDuration(focusSecsToday)},
        {QStringLiteral("pomodorosToday"), pomodorosToday},
        {QStringLiteral("streakDays"), analyticsStreak},
        {QStringLiteral("dailyProgressPct"), dailyProgressPct},
    };

    // Journal snapshot
    m_recentJournals = m_journal ? m_journal->recentSummary(3) : QVariantList{};

    // Top productivity insights (read-only snapshot)
    m_topInsights = m_analytics->insights().mid(0, 3);

    // External calendar events (.ics + running Akonadi/KOrganizer if available)
    loadLocalExternalEvents(today, now);
    startAkonadiFetch(forceExternalRefresh);
    mergeExternalAndComputeSchedule(false);

    m_dirty = false;
    if (emitChanged) {
        Q_EMIT changed();
    }
}

QStringList SummaryManager::discoverExternalIcsPaths() const
{
    QStringList rawPaths;
    if (m_settings->integrateKOrganizer()) {
        const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        const QDir dir(cfgDir);
        const QStringList icalConfigs = dir.entryList({QStringLiteral("akonadi_ical_resource_*rc")}, QDir::Files);
        for (const QString &file : icalConfigs) {
            KSharedConfigPtr cfg = KSharedConfig::openConfig(dir.filePath(file), KConfig::SimpleConfig);
            const KConfigGroup grp(cfg, QStringLiteral("General"));
            QString p = grp.readPathEntry("Path", QString());
            if (!p.isEmpty()) {
                p.replace(QStringLiteral("$HOME"), QDir::homePath());
                rawPaths << p;
            }
        }
        const QString stdIcs = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/apps/korganizer/std.ics");
        if (QFileInfo::exists(stdIcs)) {
            rawPaths << stdIcs;
        }
    }
    rawPaths << m_settings->extraIcsPaths();

    QStringList out;
    QSet<QString> seen;
    for (const QString &p : std::as_const(rawPaths)) {
        const QFileInfo fi(p.trimmed());
        if (!fi.exists() || !fi.isFile()) {
            continue;
        }
        const QString canon = fi.canonicalFilePath();
        if (!seen.contains(canon)) {
            seen.insert(canon);
            out << canon;
        }
    }
    return out;
}

void SummaryManager::loadLocalExternalEvents(const QDate &today, const QDateTime &nowLocal)
{
    m_icsEvents.clear();
    m_icsSources.clear();
    m_icsLoadedFiles = 0;

    if (!m_settings->integrateKOrganizer() && m_settings->extraIcsPaths().isEmpty()) {
        return;
    }

    const QStringList paths = discoverExternalIcsPaths();
    for (const QString &path : paths) {
        const KCalendarCore::MemoryCalendar::Ptr cal(new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
        KCalendarCore::FileStorage storage(cal, path);
        if (!storage.load()) {
            continue;
        }
        ++m_icsLoadedFiles;
        const QString sourceName = QFileInfo(path).fileName() == QLatin1String("std.ics")
            ? i18nc("calendar source", "KOrganizer Local")
            : QFileInfo(path).completeBaseName();
        if (!m_icsSources.contains(sourceName)) {
            m_icsSources.append(sourceName);
        }
        appendCalendarOccurrences(cal, sourceName, today, nowLocal, m_icsEvents);
    }
}

void SummaryManager::startAkonadiFetch(bool force)
{
#ifdef KTASKS_HAVE_AKONADI
    if (QStandardPaths::isTestModeEnabled() || !m_settings->integrateKOrganizer()) {
        m_akonadiEvents.clear();
        m_akonadiSources.clear();
        return;
    }
    if (!Akonadi::ServerManager::isRunning()) {
        // Never spin up Akonadi daemon if user isn't running KOrganizer/Plasma PIM
        return;
    }
    if (m_akonadiFetching) {
        return;
    }
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
    if (!force && m_akonadiLastFetch.isValid() && m_akonadiLastFetch.secsTo(nowUtc) < 5 * 60) {
        return;
    }

    m_akonadiFetching = true;
    auto *cjob = new Akonadi::CollectionFetchJob(Akonadi::Collection::root(), Akonadi::CollectionFetchJob::Recursive, this);
    cjob->fetchScope().setContentMimeTypes({
        QStringLiteral("text/calendar"),
        QStringLiteral("application/x-vnd.akonadi.calendar.event"),
    });
    cjob->fetchScope().setListFilter(Akonadi::CollectionFetchScope::Enabled);

    connect(cjob, &Akonadi::CollectionFetchJob::result, this, [this, cjob] {
        if (cjob->error()) {
            m_akonadiFetching = false;
            return;
        }

        // Collect Routine Nextcloud calendar hrefs so we NEVER duplicate KTasks' own Routine blocks
        QStringList routineHrefs;
        for (const Account &a : m_store->accounts()) {
            const QString pref = m_store->database()->meta(QStringLiteral("routine_cal_pref_%1").arg(a.id));
            const QString sel = pref.isEmpty() ? m_store->database()->meta(QStringLiteral("routine_cal_%1").arg(a.id)) : pref;
            if (!sel.isEmpty()) {
                routineHrefs << sel;
            }
        }

        struct TargetCol {
            Akonadi::Collection col;
            QString sourceLabel;
        };
        QList<TargetCol> targets;

        const Akonadi::Collection::List cols = cjob->collections();
        for (const Akonadi::Collection &col : cols) {
            if (!col.enabled() || col.isVirtual()) {
                continue;
            }
            const QStringList mimes = col.contentMimeTypes();
            if (!mimes.contains(QStringLiteral("text/calendar"))
                && !mimes.contains(QStringLiteral("application/x-vnd.akonadi.calendar.event"))) {
                continue;
            }
            const QString rem = col.remoteId().trimmed();
            bool isOwnRoutineCal = false;
            for (const QString &rh : std::as_const(routineHrefs)) {
                if (!rh.isEmpty() && !rem.isEmpty() && (rem == rh || rem.endsWith(rh) || rh.endsWith(rem))) {
                    isOwnRoutineCal = true;
                    break;
                }
            }
            if (isOwnRoutineCal) {
                continue;
            }

            QString label;
            if (const auto *attr = col.attribute<Akonadi::EntityDisplayAttribute>()) {
                label = attr->displayName().trimmed();
            }
            if (label.isEmpty()) {
                label = col.displayName().trimmed();
            }
            if (label.isEmpty()) {
                label = col.name().trimmed();
            }
            const QString res = col.resource();
            if (res.contains(QLatin1String("birthdays")) || label.isEmpty()) {
                if (res.contains(QLatin1String("birthdays"))) {
                    label = i18nc("calendar source", "Birthdays");
                } else {
                    label = i18nc("calendar source", "KOrganizer");
                }
            } else if (res.contains(QLatin1String("google")) && !label.contains(QLatin1String("Google"), Qt::CaseInsensitive)) {
                label = i18nc("calendar source google", "Google (%1)", label);
            }
            targets.append({col, label});
        }

        if (targets.isEmpty()) {
            m_akonadiEvents.clear();
            m_akonadiSources.clear();
            m_akonadiLastFetch = QDateTime::currentDateTimeUtc();
            m_akonadiFetching = false;
            mergeExternalAndComputeSchedule(true);
            return;
        }

        auto remaining = QSharedPointer<int>::create(targets.size());
        auto collected = QSharedPointer<QList<ExternalEventRecord>>::create();
        auto srcNames = QSharedPointer<QSet<QString>>::create();

        for (const TargetCol &tc : std::as_const(targets)) {
            auto *ijob = new Akonadi::ItemFetchJob(tc.col, this);
            ijob->fetchScope().fetchFullPayload(true);
            ijob->fetchScope().setFetchModificationTime(false);
            const QString sourceLabel = tc.sourceLabel;

            connect(ijob, &Akonadi::ItemFetchJob::result, this, [this, ijob, sourceLabel, remaining, collected, srcNames] {
                if (!ijob->error()) {
                    const QDateTime nowLocal = QDateTime::currentDateTime();
                    const QDate today = nowLocal.date();
                    const KCalendarCore::MemoryCalendar::Ptr tempCal(new KCalendarCore::MemoryCalendar(QTimeZone::systemTimeZone()));
                    KCalendarCore::ICalFormat fmt;
                    int parsedCount = 0;

                    for (const Akonadi::Item &item : ijob->items()) {
                        const QByteArray payload = item.payloadData();
                        if (payload.isEmpty()) {
                            continue;
                        }
                        if (!fmt.fromRawString(tempCal, payload)) {
                            if (const KCalendarCore::Incidence::Ptr inc = fmt.readIncidence(payload)) {
                                tempCal->addIncidence(inc);
                                ++parsedCount;
                            }
                        } else {
                            ++parsedCount;
                        }
                    }
                    srcNames->insert(sourceLabel);
                    if (parsedCount > 0) {
                        appendCalendarOccurrences(tempCal, sourceLabel, today, nowLocal, *collected);
                    }
                }
                if (--(*remaining) == 0) {
                    m_akonadiEvents = *collected;
                    m_akonadiSources = srcNames->values();
                    m_akonadiSources.sort(Qt::CaseInsensitive);
                    m_akonadiLastFetch = QDateTime::currentDateTimeUtc();
                    m_akonadiFetching = false;
                    mergeExternalAndComputeSchedule(true);
                }
            });
        }
    });
#else
    Q_UNUSED(force)
#endif
}

void SummaryManager::mergeExternalAndComputeSchedule(bool emitChanged)
{
    const QDateTime nowLocal = QDateTime::currentDateTime();
    const QDate today = nowLocal.date();
    const int nowMin = nowLocal.time().hour() * 60 + nowLocal.time().minute();

    QList<ExternalEventRecord> merged;
    merged.reserve(m_icsEvents.size() + m_akonadiEvents.size());
    QSet<QString> dedupKeys;

    auto addDedup = [&](const ExternalEventRecord &rec) {
        const QString key = QStringLiteral("%1|%2|%3|%4")
                                .arg(rec.title.toLower(), rec.dateIso)
                                .arg(rec.startMinute)
                                .arg(rec.allDay ? 1 : 0);
        if (dedupKeys.contains(key)) {
            return;
        }
        dedupKeys.insert(key);
        merged.append(rec);
    };

    // Prefer Akonadi records first for richer collection display names, then .ics files
    for (const ExternalEventRecord &r : std::as_const(m_akonadiEvents)) {
        addDedup(r);
    }
    for (const ExternalEventRecord &r : std::as_const(m_icsEvents)) {
        addDedup(r);
    }

    std::sort(merged.begin(), merged.end(), [](const ExternalEventRecord &a, const ExternalEventRecord &b) {
        if (a.startSort != b.startSort) {
            return a.startSort < b.startSort;
        }
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });

    // Clear previous conflict flags on Today's Routine before recomputing
    for (int i = 0; i < m_todayRoutines.size(); ++i) {
        QVariantMap rm = m_todayRoutines.at(i).toMap();
        rm[QStringLiteral("hasConflict")] = false;
        rm[QStringLiteral("conflictWith")] = QString();
        m_todayRoutines[i] = rm;
    }

    m_externalEvents.clear();
    const int limit = qMin(12, int(merged.size()));
    m_externalEvents.reserve(limit);
    for (int i = 0; i < limit; ++i) {
        const ExternalEventRecord &r = merged.at(i);
        QString conflictRoutine;
        if (r.isToday && !r.allDay && r.startMinute >= 0) {
            for (int ri = 0; ri < m_todayRoutines.size(); ++ri) {
                QVariantMap rm = m_todayRoutines.at(ri).toMap();
                const int rStart = rm.value(QStringLiteral("startMinute")).toInt();
                const int rEnd = rm.value(QStringLiteral("endMinute")).toInt();
                if (r.startMinute < rEnd && r.endMinute > rStart) {
                    conflictRoutine = rm.value(QStringLiteral("title")).toString();
                    rm[QStringLiteral("hasConflict")] = true;
                    rm[QStringLiteral("conflictWith")] = r.title;
                    m_todayRoutines[ri] = rm;
                    break;
                }
            }
        }
        m_externalEvents.append(QVariantMap{
            {QStringLiteral("title"), r.title},
            {QStringLiteral("location"), r.location},
            {QStringLiteral("dateIso"), r.dateIso},
            {QStringLiteral("dateLabel"), r.dateLabel},
            {QStringLiteral("timeRange"), r.timeRange},
            {QStringLiteral("startTimeText"), r.startTimeText},
            {QStringLiteral("isToday"), r.isToday},
            {QStringLiteral("isPast"), r.isPast},
            {QStringLiteral("sourceName"), r.sourceName},
            {QStringLiteral("remainingText"), r.isToday ? formatRemainingText(r.startMinute, r.endMinute, nowMin) : QString()},
            {QStringLiteral("hasConflict"), !conflictRoutine.isEmpty()},
            {QStringLiteral("conflictTitle"), conflictRoutine},
        });
    }

    // Build combined "Next Up & Free Time" spotlight for Today
    struct Slot {
        int start = 0;
        int end = 0;
        QString title;
        QString timeRange;
        bool isExternal = false;
        bool hasConflict = false;
    };
    QList<Slot> slotsToday;
    for (const QVariant &rv : std::as_const(m_todayRoutines)) {
        const QVariantMap rm = rv.toMap();
        if (rm.value(QStringLiteral("status")).toInt() == 2) {
            continue; // skipped routine doesn't occupy day
        }
        const int s = rm.value(QStringLiteral("startMinute")).toInt();
        const int e = rm.value(QStringLiteral("endMinute")).toInt();
        if (e > nowMin) {
            slotsToday.append({s, e, rm.value(QStringLiteral("title")).toString(), rm.value(QStringLiteral("timeRange")).toString(), false, rm.value(QStringLiteral("hasConflict")).toBool()});
        }
    }
    for (const ExternalEventRecord &r : std::as_const(merged)) {
        if (r.isToday && !r.allDay && r.endMinute > nowMin && r.startMinute >= 0) {
            slotsToday.append({r.startMinute, r.endMinute, r.title, r.timeRange, true, false});
        }
    }
    std::sort(slotsToday.begin(), slotsToday.end(), [](const Slot &a, const Slot &b) {
        return a.start < b.start;
    });

    if (slotsToday.isEmpty()) {
        m_nextSchedule = QVariantMap{{QStringLiteral("available"), false}};
    } else {
        const Slot &first = slotsToday.first();
        const bool isNow = (first.start <= nowMin && nowMin < first.end);
        QString badge;
        if (isNow) {
            const int rem = first.end - nowMin;
            badge = i18nc("now remaining", "Now · %1 min left", rem);
        } else {
            badge = formatRemainingText(first.start, first.end, nowMin);
        }
        QString freeWindow;
        if (!isNow && (first.start - nowMin) >= 25) {
            const int gap = first.start - nowMin;
            freeWindow = gap >= 60
                ? i18nc("free gap h+m", "Free window now: %1h %2m", gap / 60, gap % 60)
                : i18nc("free gap min", "Free window now: %1 min", gap);
        }
        bool anyConflict = false;
        for (const QVariant &rv : std::as_const(m_todayRoutines)) {
            if (rv.toMap().value(QStringLiteral("hasConflict")).toBool()) {
                anyConflict = true;
                break;
            }
        }
        m_nextSchedule = QVariantMap{
            {QStringLiteral("available"), true},
            {QStringLiteral("isNow"), isNow},
            {QStringLiteral("isExternal"), first.isExternal},
            {QStringLiteral("isConflict"), anyConflict || first.hasConflict},
            {QStringLiteral("title"), first.title},
            {QStringLiteral("timeRange"), first.timeRange},
            {QStringLiteral("badgeText"), badge},
            {QStringLiteral("freeWindowText"), freeWindow},
        };
    }

    // Human-readable External Calendar status line
    QSet<QString> allSources;
    for (const QString &s : std::as_const(m_icsSources)) {
        allSources.insert(s);
    }
    for (const QString &s : std::as_const(m_akonadiSources)) {
        allSources.insert(s);
    }
    QStringList sortedSources = allSources.values();
    sortedSources.sort(Qt::CaseInsensitive);

    if (!m_settings->integrateKOrganizer() && m_settings->extraIcsPaths().isEmpty()) {
        m_externalCalStatus = i18nc("external calendar status", "External agenda disabled in Settings.");
    } else if (!sortedSources.isEmpty()) {
        m_externalCalStatus = i18ncp("external cal sources",
                                     "Read-only from %2 (%1 calendar) — kept separate from Routine.",
                                     "Read-only from %2 (%1 calendars) — kept separate from Routine.",
                                     sortedSources.size(),
                                     sortedSources.join(QStringLiteral(", ")));
    } else if (m_akonadiFetching) {
        m_externalCalStatus = i18nc("external calendar status", "Loading KOrganizer / desktop calendars…");
    } else {
        m_externalCalStatus = i18nc("external calendar status", "No external calendar with events found. Connect KOrganizer or add an .ics file in Settings.");
    }

    Q_UNUSED(today)
    if (emitChanged) {
        Q_EMIT changed();
    }
}

qlonglong SummaryManager::createPrepTaskFromEvent(const QString &title, const QString &dateIso, const QString &startTimeText, const QString &sourceName)
{
    const QString clean = title.trimmed();
    if (clean.isEmpty()) {
        return 0;
    }
    Task t;
    t.title = i18nc("prep task title", "Prepare for: %1", clean);
    const QDate d = QDate::fromString(dateIso, Qt::ISODate);
    if (d.isValid()) {
        const QTime tm = startTimeText.isEmpty() ? QTime() : QTime::fromString(startTimeText, QStringLiteral("HH:mm"));
        if (tm.isValid()) {
            t.allDay = false;
            t.due = QDateTime(d, tm, QTimeZone::systemTimeZone()).toUTC();
        } else {
            t.allDay = true;
            t.due = QDateTime(d, QTime(0, 0));
        }
    } else {
        t.allDay = true;
        t.due = QDateTime(QDate::currentDate(), QTime(0, 0));
    }
    t.quadrant = (d == QDate::currentDate()) ? 1 : 2;
    t.priority = Task::priorityForQuadrant(t.quadrant);
    t.notes = i18nc("prep task note", "Created from external agenda (%1): %2 %3",
                    sourceName.isEmpty() ? i18n("External Agenda") : sourceName,
                    dateIso,
                    startTimeText);
    t.listId = m_store->fallbackListId(m_settings->defaultListId());
    const qint64 createdId = m_store->addTask(t);
    if (createdId > 0) {
        Q_EMIT message(i18nc("@info", "Task created: “%1”", t.title), true);
    }
    return createdId;
}

qlonglong SummaryManager::openTodayJournal()
{
    return m_journal ? m_journal->openOrCreateTodayEntry() : 0;
}

void SummaryManager::ensureNetwork()
{
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }
}

bool SummaryManager::loadCachedWeather()
{
    const QString cachedJson = m_store->database()->meta(QStringLiteral("weather_cache_json"));
    const QString cachedTs = m_store->database()->meta(QStringLiteral("weather_cache_ts"));
    const QString cachedCity = m_store->database()->meta(QStringLiteral("weather_cache_city"));
    if (cachedJson.isEmpty()) {
        return false;
    }
    const QDateTime ts = QDateTime::fromString(cachedTs, Qt::ISODate);
    const bool fresh = ts.isValid() && ts.secsTo(QDateTime::currentDateTimeUtc()) < 60 * 60;
    parseWeatherJson(cachedJson.toUtf8(), cachedCity, true, ts);
    return fresh;
}

void SummaryManager::refreshWeather(bool force)
{
    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }
    if (!force && loadCachedWeather()) {
        return;
    }
    resolveWeatherLocationAndFetch(force);
}

void SummaryManager::getPreferringIpv4(const QUrl &url,
                                       std::function<void(QNetworkReply *)> onFinished,
                                       std::function<void(QNetworkReply *)> onStarted)
{
    ensureNetwork();
    const QString host = url.host();
    QHostInfo::lookupHost(host, this, [this, url, host, onFinished = std::move(onFinished), onStarted = std::move(onStarted)](const QHostInfo &info) mutable {
        QUrl targetUrl(url);
        bool usedIpv4Literal = false;
        for (const QHostAddress &addr : info.addresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                targetUrl.setHost(addr.toString());
                usedIpv4Literal = true;
                break;
            }
        }
        QNetworkRequest req(usedIpv4Literal ? targetUrl : url);
        req.setTransferTimeout(8000);
        if (usedIpv4Literal) {
            req.setRawHeader("Host", host.toUtf8());
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
            req.setPeerVerifyName(host);
#else
            req = QNetworkRequest(url);
            req.setTransferTimeout(8000);
#endif
        }
        QNetworkReply *reply = m_nam->get(req);
        if (onStarted) {
            onStarted(reply);
        }
        if (onFinished) {
            connect(reply, &QNetworkReply::finished, this, [reply, fn = std::move(onFinished)]() mutable {
                fn(reply);
            });
        }
    });
}

void SummaryManager::resolveWeatherLocationAndFetch(bool /*force*/)
{
    double lat = m_settings->weatherLatitude();
    double lon = m_settings->weatherLongitude();
    QString city = m_settings->weatherCity().trimmed();

    if (qAbs(lat) <= 90.0 && qAbs(lon) <= 180.0 && !city.isEmpty()) {
        fetchWeatherForCoords(city, lat, lon);
        return;
    }

    const QString kwPath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kweather/kweatherrc");
    if (QFileInfo::exists(kwPath)) {
        KSharedConfigPtr kwCfg = KSharedConfig::openConfig(kwPath, KConfig::SimpleConfig);
        const KConfigGroup wlRoot(kwCfg, QStringLiteral("WeatherLocations"));
        int bestIdx = 999999;
        for (const QString &sub : wlRoot.groupList()) {
            const KConfigGroup loc(&wlRoot, sub);
            const int idx = loc.readEntry("index", 999);
            const QString name = loc.readEntry("locationName", QString());
            const double kLat = loc.readEntry("latitude", 999.0);
            const double kLon = loc.readEntry("longitude", 999.0);
            if (!name.isEmpty() && qAbs(kLat) <= 90.0 && qAbs(kLon) <= 180.0 && idx < bestIdx) {
                bestIdx = idx;
                city = name;
                lat = kLat;
                lon = kLon;
            }
        }
        if (!city.isEmpty() && qAbs(lat) <= 90.0 && qAbs(lon) <= 180.0) {
            fetchWeatherForCoords(city, lat, lon);
            return;
        }
    }

    ensureNetwork();
    m_weatherLoading = true;
    Q_EMIT weatherChanged();
    getPreferringIpv4(QUrl(QStringLiteral("https://get.geojs.io/v1/ip/geo.json")), [this](QNetworkReply *reply) {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_weatherLoading = false;
            if (m_weather.isEmpty()) {
                m_weather = QVariantMap{
                    {QStringLiteral("available"), false},
                    {QStringLiteral("error"), i18n("Offline or location unavailable — choose your city to load forecast.")},
                };
            }
            Q_EMIT weatherChanged();
            return;
        }
        const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        const double latVal = obj.value(QStringLiteral("latitude")).toString().toDouble();
        const double lonVal = obj.value(QStringLiteral("longitude")).toString().toDouble();
        const QString cityVal = obj.value(QStringLiteral("city")).toString().trimmed();
        if (qAbs(latVal) > 0.0001 || qAbs(lonVal) > 0.0001) {
            const QString resolved = cityVal.isEmpty() ? i18n("Current location") : cityVal;
            m_settings->setWeatherCity(resolved);
            m_settings->setWeatherLatitude(latVal);
            m_settings->setWeatherLongitude(lonVal);
            fetchWeatherForCoords(resolved, latVal, lonVal);
        } else {
            m_weatherLoading = false;
            Q_EMIT weatherChanged();
        }
    });
}

void SummaryManager::fetchWeatherForCoords(const QString &cityName, double lat, double lon)
{
    ensureNetwork();
    m_weatherLoading = true;
    Q_EMIT weatherChanged();

    QUrl url(QStringLiteral("https://api.open-meteo.com/v1/forecast"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("latitude"), QString::number(lat, 'f', 4));
    q.addQueryItem(QStringLiteral("longitude"), QString::number(lon, 'f', 4));
    q.addQueryItem(QStringLiteral("current"), QStringLiteral("temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"));
    q.addQueryItem(QStringLiteral("daily"), QStringLiteral("weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"));
    q.addQueryItem(QStringLiteral("timezone"), QStringLiteral("auto"));
    q.addQueryItem(QStringLiteral("forecast_days"), QStringLiteral("10"));
    url.setQuery(q);

    getPreferringIpv4(url, [this, cityName](QNetworkReply *reply) {
        reply->deleteLater();
        m_weatherLoading = false;
        if (reply->error() != QNetworkReply::NoError) {
            if (!loadCachedWeather() && m_weather.isEmpty()) {
                m_weather = QVariantMap{
                    {QStringLiteral("available"), false},
                    {QStringLiteral("city"), cityName},
                    {QStringLiteral("error"), i18n("Could not update weather (%1).", reply->errorString())},
                };
            }
            Q_EMIT weatherChanged();
            return;
        }
        const QByteArray raw = reply->readAll();
        const QDateTime nowUtc = QDateTime::currentDateTimeUtc();
        m_store->database()->setMeta(QStringLiteral("weather_cache_json"), QString::fromUtf8(raw));
        m_store->database()->setMeta(QStringLiteral("weather_cache_ts"), nowUtc.toString(Qt::ISODate));
        m_store->database()->setMeta(QStringLiteral("weather_cache_city"), cityName);
        parseWeatherJson(raw, cityName, false, nowUtc);
    });
}

void SummaryManager::parseWeatherJson(const QByteArray &json, const QString &cityName, bool fromCache, const QDateTime &readingUtc)
{
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    const QJsonObject cur = root.value(QStringLiteral("current")).toObject();
    if (cur.isEmpty()) {
        return;
    }
    const int code = cur.value(QStringLiteral("weather_code")).toInt(-1);
    const bool isDay = cur.value(QStringLiteral("is_day")).toInt(1) != 0;

    const QJsonObject daily = root.value(QStringLiteral("daily")).toObject();
    const QJsonArray times = daily.value(QStringLiteral("time")).toArray();
    const QJsonArray codes = daily.value(QStringLiteral("weather_code")).toArray();
    const QJsonArray tmax = daily.value(QStringLiteral("temperature_2m_max")).toArray();
    const QJsonArray tmin = daily.value(QStringLiteral("temperature_2m_min")).toArray();
    const QJsonArray precip = daily.value(QStringLiteral("precipitation_probability_max")).toArray();

    const QLocale locale;
    QVariantList forecastList;
    const int daysCount = qMin(10, int(times.size()));
    int todayMin = 0;
    int todayMax = 0;
    for (int i = 0; i < daysCount; ++i) {
        const QDate d = QDate::fromString(times.at(i).toString(), Qt::ISODate);
        const int dCode = codes.at(i).toInt(-1);
        const int dMin = qRound(tmin.at(i).toDouble());
        const int dMax = qRound(tmax.at(i).toDouble());
        if (i == 0) {
            todayMin = dMin;
            todayMax = dMax;
        }
        forecastList.append(QVariantMap{
            {QStringLiteral("dayLabel"), i == 0 ? i18nc("forecast day", "Today") : locale.dayName(d.dayOfWeek(), QLocale::ShortFormat)},
            {QStringLiteral("dateShort"), d.isValid() ? d.toString(QStringLiteral("dd/MM")) : QString()},
            {QStringLiteral("isToday"), i == 0},
            {QStringLiteral("minTemp"), dMin},
            {QStringLiteral("maxTemp"), dMax},
            {QStringLiteral("rainProb"), precip.at(i).toInt(0)},
            {QStringLiteral("iconName"), wmoIcon(dCode, true)},
            {QStringLiteral("condition"), wmoDescription(dCode)},
        });
    }

    const QTime displayTime = readingUtc.isValid() ? readingUtc.toLocalTime().time() : QTime::currentTime();

    m_weather = QVariantMap{
        {QStringLiteral("available"), true},
        {QStringLiteral("city"), cityName},
        {QStringLiteral("tempC"), qRound(cur.value(QStringLiteral("temperature_2m")).toDouble())},
        {QStringLiteral("feelsLikeC"), qRound(cur.value(QStringLiteral("apparent_temperature")).toDouble())},
        {QStringLiteral("humidity"), qRound(cur.value(QStringLiteral("relative_humidity_2m")).toDouble())},
        {QStringLiteral("windKmh"), qRound(cur.value(QStringLiteral("wind_speed_10m")).toDouble())},
        {QStringLiteral("minTempC"), todayMin},
        {QStringLiteral("maxTempC"), todayMax},
        {QStringLiteral("conditionText"), wmoDescription(code)},
        {QStringLiteral("iconName"), wmoIcon(code, isDay)},
        {QStringLiteral("dailyForecast"), forecastList},
        {QStringLiteral("fromCache"), fromCache},
        {QStringLiteral("updatedText"), locale.toString(displayTime, QLocale::ShortFormat)},
        {QStringLiteral("error"), QString()},
    };
    Q_EMIT weatherChanged();
}

void SummaryManager::searchWeatherCities(const QString &query)
{
    const QString trimmed = query.trimmed();
    ++m_citySearchSeq;
    const int seq = m_citySearchSeq;
    if (m_cityReply) {
        m_cityReply->abort();
        m_cityReply = nullptr;
    }
    if (trimmed.size() < 2) {
        m_citySearching = false;
        m_citySearchResults.clear();
        Q_EMIT citySearchResultsChanged();
        return;
    }
    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }
    m_citySearching = true;
    Q_EMIT citySearchResultsChanged();

    QUrl url(QStringLiteral("https://geocoding-api.open-meteo.com/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("name"), trimmed);
    q.addQueryItem(QStringLiteral("count"), QStringLiteral("6"));
    q.addQueryItem(QStringLiteral("language"), QLocale::system().name().section(QLatin1Char('_'), 0, 0));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    url.setQuery(q);

    getPreferringIpv4(
        url,
        [this, seq](QNetworkReply *reply) {
            reply->deleteLater();
            if (m_cityReply == reply) {
                m_cityReply = nullptr;
            }
            if (seq != m_citySearchSeq) {
                return;
            }
            m_citySearching = false;
            m_citySearchResults.clear();
            if (reply->error() == QNetworkReply::NoError) {
                const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("results")).toArray();
                for (const QJsonValue &v : arr) {
                    const QJsonObject o = v.toObject();
                    const QString name = o.value(QStringLiteral("name")).toString();
                    const QString admin = o.value(QStringLiteral("admin1")).toString();
                    const QString country = o.value(QStringLiteral("country_code")).toString();
                    const QString label = admin.isEmpty() ? QStringLiteral("%1 (%2)").arg(name, country)
                                                          : QStringLiteral("%1, %2 (%3)").arg(name, admin, country);
                    m_citySearchResults.append(QVariantMap{
                        {QStringLiteral("name"), name},
                        {QStringLiteral("label"), label},
                        {QStringLiteral("latitude"), o.value(QStringLiteral("latitude")).toDouble()},
                        {QStringLiteral("longitude"), o.value(QStringLiteral("longitude")).toDouble()},
                    });
                }
            }
            Q_EMIT citySearchResultsChanged();
        },
        [this, seq](QNetworkReply *reply) {
            if (seq != m_citySearchSeq) {
                reply->abort();
                return;
            }
            m_cityReply = reply;
        });
}

void SummaryManager::selectWeatherCity(const QString &name, double latitude, double longitude)
{
    m_settings->setWeatherCity(name);
    m_settings->setWeatherLatitude(latitude);
    m_settings->setWeatherLongitude(longitude);
    m_citySearchResults.clear();
    Q_EMIT citySearchResultsChanged();
    fetchWeatherForCoords(name, latitude, longitude);
}

void SummaryManager::addExtraIcsFile(const QUrl &fileUrl)
{
    const QString local = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (local.isEmpty() || !QFileInfo::exists(local)) {
        return;
    }
    QStringList cur = m_settings->extraIcsPaths();
    if (!cur.contains(local)) {
        cur.append(local);
        m_settings->setExtraIcsPaths(cur);
    }
}

void SummaryManager::removeExtraIcsPath(const QString &path)
{
    QStringList cur = m_settings->extraIcsPaths();
    if (cur.removeAll(path) > 0) {
        m_settings->setExtraIcsPaths(cur);
    }
}

QString SummaryManager::wmoDescription(int code)
{
    if (code == 0) return i18nc("weather", "Clear sky");
    if (code == 1) return i18nc("weather", "Mainly clear");
    if (code == 2) return i18nc("weather", "Partly cloudy");
    if (code == 3) return i18nc("weather", "Overcast");
    if (code == 45 || code == 48) return i18nc("weather", "Fog");
    if (code >= 51 && code <= 57) return i18nc("weather", "Drizzle");
    if (code >= 61 && code <= 67) return i18nc("weather", "Rain");
    if (code >= 71 && code <= 77) return i18nc("weather", "Snow");
    if (code >= 80 && code <= 82) return i18nc("weather", "Rain showers");
    if (code >= 85 && code <= 86) return i18nc("weather", "Snow showers");
    if (code >= 95 && code <= 99) return i18nc("weather", "Thunderstorm");
    return i18nc("weather", "Variable conditions");
}

QString SummaryManager::wmoIcon(int code, bool isDay)
{
    if (code == 0 || code == 1) return isDay ? QStringLiteral("weather-clear") : QStringLiteral("weather-clear-night");
    if (code == 2) return isDay ? QStringLiteral("weather-few-clouds") : QStringLiteral("weather-few-clouds-night");
    if (code == 3) return QStringLiteral("weather-overcast");
    if (code == 45 || code == 48) return QStringLiteral("weather-fog");
    if (code >= 51 && code <= 57) return QStringLiteral("weather-showers-scattered");
    if (code >= 61 && code <= 67) return QStringLiteral("weather-showers");
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return QStringLiteral("weather-snow");
    if (code >= 80 && code <= 82) return isDay ? QStringLiteral("weather-showers-day") : QStringLiteral("weather-showers-night");
    if (code >= 95 && code <= 99) return isDay ? QStringLiteral("weather-storm-day") : QStringLiteral("weather-storm-night");
    return QStringLiteral("weather-few-clouds");
}
