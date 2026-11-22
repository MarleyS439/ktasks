// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <KSharedConfig>
#include <QObject>
#include <QVariant>

/** Application settings, stored with KConfig in ~/.config/ktasksrc. */
class Settings : public QObject
{
    Q_OBJECT
#define KTASKS_SETTING(Type, name, Setter, group, def)                                                                                                         \
    Q_PROPERTY(Type name READ name WRITE Setter NOTIFY name##Changed)                                                                                          \
public:                                                                                                                                                        \
    Type name() const                                                                                                                                          \
    {                                                                                                                                                          \
        return read(QStringLiteral(group), QStringLiteral(#name), QVariant::fromValue<Type>(def)).value<Type>();                                               \
    }                                                                                                                                                          \
    void Setter(const Type &v)                                                                                                                                 \
    {                                                                                                                                                          \
        if (name() != v) {                                                                                                                                     \
            write(QStringLiteral(group), QStringLiteral(#name), QVariant::fromValue<Type>(v));                                                                 \
            Q_EMIT name##Changed();                                                                                                                            \
        }                                                                                                                                                      \
    }                                                                                                                                                          \
    Q_SIGNAL void name##Changed();

    // General
    KTASKS_SETTING(bool, runInBackground, setRunInBackground, "General", true)
    KTASKS_SETTING(bool, showTrayIcon, setShowTrayIcon, "General", true)
    KTASKS_SETTING(bool, showBadge, setShowBadge, "General", true)
    KTASKS_SETTING(bool, showCompleted, setShowCompleted, "General", false)
    KTASKS_SETTING(QString, lastView, setLastView, "General", QStringLiteral("today"))
    KTASKS_SETTING(qlonglong, defaultListId, setDefaultListId, "General", 0)
    KTASKS_SETTING(int, defaultReminder, setDefaultReminder, "General", -1) // minutes before due; -1 none
    KTASKS_SETTING(int, sidebarWidth, setSidebarWidth, "General", 240)
    // Profile
    KTASKS_SETTING(QString, profileName, setProfileName, "Profile", QString())
    KTASKS_SETTING(QString, profileEmail, setProfileEmail, "Profile", QString())
    KTASKS_SETTING(QString, profileAvatarPath, setProfileAvatarPath, "Profile", QString())
    KTASKS_SETTING(bool, useSystemAvatar, setUseSystemAvatar, "Profile", true)
    // Sync
    KTASKS_SETTING(int, syncInterval, setSyncInterval, "Sync", 15) // minutes; 0 = manual only
    KTASKS_SETTING(bool, syncOnStart, setSyncOnStart, "Sync", true)
    KTASKS_SETTING(bool, syncNotes, setSyncNotes, "Sync", true)
    KTASKS_SETTING(bool, syncRoutines, setSyncRoutines, "Sync", true)
    // Backup
    KTASKS_SETTING(QString, backupDirectory, setBackupDirectory, "Backup", QString())
    KTASKS_SETTING(bool, autoBackupEnabled, setAutoBackupEnabled, "Backup", false)
    KTASKS_SETTING(int, autoBackupIntervalHours, setAutoBackupIntervalHours, "Backup", 24)
    KTASKS_SETTING(int, backupRetentionCount, setBackupRetentionCount, "Backup", 10)
    KTASKS_SETTING(QString, lastBackupAt, setLastBackupAt, "Backup", QString())
    // Notifications
    KTASKS_SETTING(bool, notifyReminders, setNotifyReminders, "Notifications", true)
    KTASKS_SETTING(bool, notifyAtDue, setNotifyAtDue, "Notifications", true)
    KTASKS_SETTING(QString, allDayReminderTime, setAllDayReminderTime, "Notifications", QStringLiteral("09:00"))
    KTASKS_SETTING(bool, dailySummary, setDailySummary, "Notifications", true)
    KTASKS_SETTING(QString, dailySummaryTime, setDailySummaryTime, "Notifications", QStringLiteral("08:00"))
    KTASKS_SETTING(QString, lastSummaryDate, setLastSummaryDate, "Notifications", QString())
    KTASKS_SETTING(int, snoozeMinutes, setSnoozeMinutes, "Notifications", 15)
    KTASKS_SETTING(bool, notifySyncProblems, setNotifySyncProblems, "Notifications", true)
    KTASKS_SETTING(bool, notifyRoutines, setNotifyRoutines, "Notifications", true)
    KTASKS_SETTING(bool, notifyFocus, setNotifyFocus, "Notifications", true)
    // Focus / Pomodoro
    KTASKS_SETTING(int, pomodoroFocusMinutes, setPomodoroFocusMinutes, "Focus", 25)
    KTASKS_SETTING(int, pomodoroShortBreakMinutes, setPomodoroShortBreakMinutes, "Focus", 5)
    KTASKS_SETTING(int, pomodoroLongBreakMinutes, setPomodoroLongBreakMinutes, "Focus", 15)
    KTASKS_SETTING(bool, focusDoNotDisturb, setFocusDoNotDisturb, "Focus", true)
    // Add-ons / Discord Rich Presence
    KTASKS_SETTING(bool, discordPresenceEnabled, setDiscordPresenceEnabled, "Addons", false)
    KTASKS_SETTING(bool, discordShowTaskTitle, setDiscordShowTaskTitle, "Addons", false)
    KTASKS_SETTING(QString, discordClientId, setDiscordClientId, "Addons", QString())
    KTASKS_SETTING(QString, discordLargeImage, setDiscordLargeImage, "Addons", QStringLiteral("ktasks"))
    // Routine
    KTASKS_SETTING(int, routineStartHour, setRoutineStartHour, "Routine", 6)
    KTASKS_SETTING(int, routineEndHour, setRoutineEndHour, "Routine", 23)
    // Eisenhower Matrix
    KTASKS_SETTING(int, eisenhowerUrgencyHours, setEisenhowerUrgencyHours, "General", 48)
    // Summary, External Calendar & Weather
    KTASKS_SETTING(bool, integrateKOrganizer, setIntegrateKOrganizer, "Summary", true)
    KTASKS_SETTING(QStringList, extraIcsPaths, setExtraIcsPaths, "Summary", QStringList())
    KTASKS_SETTING(QString, weatherCity, setWeatherCity, "Summary", QString())
    KTASKS_SETTING(double, weatherLatitude, setWeatherLatitude, "Summary", 999.0)
    KTASKS_SETTING(double, weatherLongitude, setWeatherLongitude, "Summary", 999.0)
#undef KTASKS_SETTING

public:
    explicit Settings(QObject *parent = nullptr);

    /** Per-view sort mode; -1 when the view uses its default. */
    Q_INVOKABLE int sortMode(const QString &view) const;
    Q_INVOKABLE void setSortMode(const QString &view, int mode);

private:
    QVariant read(const QString &group, const QString &key, const QVariant &def) const;
    void write(const QString &group, const QString &key, const QVariant &value);
    KSharedConfigPtr m_config;
};
