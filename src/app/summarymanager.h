// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class AnalyticsManager;
class FocusManager;
class JournalManager;
class QNetworkAccessManager;
class RoutineManager;
class Settings;
class TaskStore;

/**
 * Lazy daily Home/Summary dashboard coordinator: greeting, weekday, KPIs,
 * daily progress bar, priority & upcoming tasks, today's routine blocks,
 * read-only KOrganizer/Akonadi & local .ics external agenda events (kept
 * separate from Nextcloud routine sync), recent journal notes, top insights,
 * and hourly auto-refreshing Open-Meteo weather with multi-day forecast.
 */
class SummaryManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString greetingText READ greetingText NOTIFY changed)
    Q_PROPERTY(QString weekdayText READ weekdayText NOTIFY changed)
    Q_PROPERTY(QString fullDateText READ fullDateText NOTIFY changed)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY changed)
    Q_PROPERTY(QVariantList priorityTasks READ priorityTasks NOTIFY changed)
    Q_PROPERTY(QVariantList todayRoutines READ todayRoutines NOTIFY changed)
    Q_PROPERTY(QVariantList externalEvents READ externalEvents NOTIFY changed)
    Q_PROPERTY(QString externalCalStatus READ externalCalStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap nextSchedule READ nextSchedule NOTIFY changed)
    Q_PROPERTY(QVariantList recentJournals READ recentJournals NOTIFY changed)
    Q_PROPERTY(QVariantList topInsights READ topInsights NOTIFY changed)
    Q_PROPERTY(QVariantMap weather READ weather NOTIFY weatherChanged)
    Q_PROPERTY(bool weatherLoading READ weatherLoading NOTIFY weatherChanged)
    Q_PROPERTY(QVariantList citySearchResults READ citySearchResults NOTIFY citySearchResultsChanged)

public:
    struct ExternalEventRecord {
        QDateTime startSort;
        QDate date;
        int startMinute = -1;
        int endMinute = -1;
        bool allDay = false;
        QString uid;
        QString title;
        QString location;
        QString sourceName;
        bool isToday = false;
        bool isPast = false;
        QString dateIso;
        QString dateLabel;
        QString timeRange;
        QString startTimeText;
    };

    SummaryManager(TaskStore *store, RoutineManager *routine, AnalyticsManager *analytics, FocusManager *focus, Settings *settings, QObject *parent = nullptr);
    ~SummaryManager() override;

    void setJournalManager(JournalManager *journal);

    bool active() const { return m_active; }
    void setActive(bool active);
    bool isDirty() const { return m_dirty; }

    QString greetingText() const;
    QString weekdayText() const;
    QString fullDateText() const;
    QVariantMap stats() const;
    QVariantList priorityTasks() const;
    QVariantList todayRoutines() const;
    QVariantList externalEvents() const;
    QString externalCalStatus() const;
    QVariantMap nextSchedule() const;
    QVariantList recentJournals() const;
    QVariantList topInsights() const;
    QVariantMap weather() const { return m_weather; }
    bool weatherLoading() const { return m_weatherLoading; }
    QVariantList citySearchResults() const { return m_citySearchResults; }

    /** Discovers non-Nextcloud local KOrganizer / iCalendar (.ics) paths. */
    QStringList discoverExternalIcsPaths() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshWeather(bool force = false);
    Q_INVOKABLE void searchWeatherCities(const QString &query);
    Q_INVOKABLE void selectWeatherCity(const QString &name, double latitude, double longitude);
    Q_INVOKABLE void addExtraIcsFile(const QUrl &fileUrl);
    Q_INVOKABLE void removeExtraIcsPath(const QString &path);
    Q_INVOKABLE qlonglong createPrepTaskFromEvent(const QString &title, const QString &dateIso, const QString &startTimeText, const QString &sourceName);
    Q_INVOKABLE qlonglong openTodayJournal();

Q_SIGNALS:
    void activeChanged();
    void changed();
    void weatherChanged();
    void citySearchResultsChanged();
    void message(const QString &text, bool withUndo);

private:
    void markDirtyOrRebuild();
    void ensureFresh() const;
    void rebuildInternal(bool emitChanged, bool forceExternalRefresh = false);
    void loadLocalExternalEvents(const QDate &today, const QDateTime &nowLocal);
    void startAkonadiFetch(bool force);
    void mergeExternalAndComputeSchedule(bool emitChanged);
    void ensureNetwork();
    void resolveWeatherLocationAndFetch(bool force);
    void fetchWeatherForCoords(const QString &cityName, double lat, double lon);
    void parseWeatherJson(const QByteArray &json, const QString &cityName, bool fromCache, const QDateTime &readingUtc = {});
    bool loadCachedWeather();
    static QString wmoDescription(int code);
    static QString wmoIcon(int code, bool isDay);
    static QString formatRemainingText(int startMinute, int endMinute, int nowMinute);

    TaskStore *m_store;
    RoutineManager *m_routine;
    AnalyticsManager *m_analytics;
    FocusManager *m_focus;
    Settings *m_settings;
    JournalManager *m_journal = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QTimer m_weatherTimer;

    bool m_active = false;
    bool m_dirty = true;
    bool m_weatherLoading = false;
    bool m_akonadiFetching = false;

    QString m_greetingText;
    QString m_weekdayText;
    QString m_fullDateText;
    QVariantMap m_stats;
    QVariantList m_priorityTasks;
    QVariantList m_todayRoutines;
    QList<ExternalEventRecord> m_icsEvents;
    QList<ExternalEventRecord> m_akonadiEvents;
    QStringList m_icsSources;
    QStringList m_akonadiSources;
    int m_icsLoadedFiles = 0;
    QDateTime m_akonadiLastFetch;
    QVariantList m_externalEvents;
    QString m_externalCalStatus;
    QVariantMap m_nextSchedule;
    QVariantList m_recentJournals;
    QVariantList m_topInsights;
    QVariantMap m_weather;
    QVariantList m_citySearchResults;
};
