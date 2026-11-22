// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "discordpresence.h"
#include "task.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QVariantList>

class Settings;
class TaskStore;

/**
 * Pomodoro timer and task execution stopwatch. Tracks elapsed execution time
 * on individual tasks, stores focus session history in SQLite, manages PC
 * Do Not Disturb & Discord Rich Presence while focusing, and fires desktop
 * notifications when a cycle ends.
 */
class FocusManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasSession READ hasSession NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(int mode READ mode NOTIFY stateChanged) // 0 Pomodoro, 1 Break, 2 Stopwatch
    Q_PROPERTY(qlonglong activeTaskId READ activeTaskId NOTIFY stateChanged)
    Q_PROPERTY(QString activeTaskTitle READ activeTaskTitle NOTIFY stateChanged)
    Q_PROPERTY(int cycleNumber READ cycleNumber NOTIFY stateChanged)
    Q_PROPERTY(int completedPomodorosSession READ completedPomodorosSession NOTIFY stateChanged)
    Q_PROPERTY(int elapsedSeconds READ elapsedSeconds NOTIFY tickChanged)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY tickChanged)
    Q_PROPERTY(int targetSeconds READ targetSeconds NOTIFY stateChanged)
    Q_PROPERTY(QString timeText READ timeText NOTIFY tickChanged)
    Q_PROPERTY(QString statusLabel READ statusLabel NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY tickChanged)
    Q_PROPERTY(bool dndActive READ dndActive NOTIFY integrationsChanged)
    Q_PROPERTY(DiscordPresence *discord READ discord CONSTANT)

public:
    FocusManager(TaskStore *store, Settings *settings, QObject *parent = nullptr);
    ~FocusManager() override;

    bool hasSession() const { return m_hasSession; }
    bool running() const { return m_running; }
    bool paused() const { return m_hasSession && !m_running; }
    int mode() const { return int(m_mode); }
    qlonglong activeTaskId() const { return m_activeTaskId; }
    QString activeTaskTitle() const;
    int cycleNumber() const { return m_cycleNumber; }
    int completedPomodorosSession() const { return m_completedInStretch; }
    int elapsedSeconds() const { return m_elapsedSeconds; }
    int remainingSeconds() const { return m_remainingSeconds; }
    int targetSeconds() const { return m_targetSeconds; }
    QString timeText() const;
    QString statusLabel() const;
    double progress() const;
    bool dndActive() const { return m_dndActive; }
    DiscordPresence *discord() const { return m_discord; }

    Q_INVOKABLE void startPomodoro(qlonglong taskId = 0);
    Q_INVOKABLE void startPomodoroLabeled(const QString &label);
    Q_INVOKABLE void startStopwatch(qlonglong taskId = 0);
    Q_INVOKABLE void startBreak(bool longBreak = false);
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void stopAndSave();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void completeActiveTask();
    Q_INVOKABLE void addManualMinutes(qlonglong taskId, int minutes);
    Q_INVOKABLE bool deleteSession(qlonglong sessionId);
    Q_INVOKABLE QVariantList recentSessions(int limit = 25) const;

    static QString formatDuration(int totalSeconds);

    /** Deterministic helper for tests. */
    void advanceSecondsForTesting(int secs);

Q_SIGNALS:
    void stateChanged();
    void tickChanged();
    void sessionsChanged();
    void integrationsChanged();
    void message(const QString &text, bool withUndo);
    void cycleCompleted(int completedMode, qlonglong taskId);

private:
    void onTick();
    void commitSegment(bool completedPomodoro);
    void notifyCycleFinished(KTasks::FocusMode finishedMode, const QString &taskTitle, qint64 taskId);
    void refreshCachedTitle(bool emitIfChanged);
    void updateTimeText();
    void syncDesktopIntegrations();
    void syncDoNotDisturb();
    void syncDiscordPresence();

    TaskStore *m_store;
    Settings *m_settings;
    DiscordPresence *m_discord = nullptr;
    QTimer m_timer;

    bool m_hasSession = false;
    bool m_running = false;
    bool m_dndActive = false;
    quint32 m_dndCookie = 0;
    KTasks::FocusMode m_mode = KTasks::FocusMode::Pomodoro;
    qint64 m_activeTaskId = 0;
    QString m_fallbackTitle;
    QString m_cachedTaskTitle;
    QString m_timeText;
    int m_cycleNumber = 1;
    int m_completedInStretch = 0;
    int m_elapsedSeconds = 0;
    int m_remainingSeconds = 0;
    int m_targetSeconds = 0;
    QDateTime m_startedAt;
};
