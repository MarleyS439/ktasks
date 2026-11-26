// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "taskstore.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>

class KNotification;
class RoutineManager;
class Settings;

/**
 * Desktop notifications: task reminders (with "Complete" and "Snooze"
 * buttons), tasks reaching their due time, routine block reminders, the
 * daily summary and sync problems. Which reminders already fired is
 * remembered in the database, so nothing is shown twice and reminders
 * missed while the application was closed are shown at the next start.
 */
class Notifier : public QObject
{
    Q_OBJECT
public:
    Notifier(TaskStore *store, Settings *settings, QObject *parent = nullptr);
    void setRoutineManager(RoutineManager *routine);

    void notifyConflicts(const QString &accountName, const QStringList &titles);
    void notifySyncProblem(const QString &accountName, const QString &message, bool needsLogin);
    /** Shows a sample reminder so the user can see what to expect. */
    Q_INVOKABLE void sendTestNotification();

    /**
     * Whether KNotifications can show anything on this platform. The Windows
     * build of KNotifications has no visual backend, so there every
     * notification is handed to fallbackMessage() instead (a tray balloon).
     */
    static bool hasNativeBackend();

    struct Due {
        qint64 taskId;
        QString uid;
        QString key; // identifies this trigger in the reminder log
        QDateTime when;
        bool snoozed = false;
    };
    /** Everything that should have fired by @p now and has not yet. Exposed for tests. */
    QList<Due> pending(const QDateTime &now, QDateTime *next = nullptr) const;

    struct RoutineDue {
        qint64 blockId;
        QString uid;
        QString key;
        QDateTime when;
        QDate date;
        bool snoozed = false;
    };
    QList<RoutineDue> pendingRoutines(const QDateTime &now, QDateTime *next = nullptr) const;

Q_SIGNALS:
    /** The user clicked a notification: show the window, select the task (0: none). */
    void activateRequested(qlonglong taskId, const QString &view, const QString &xdgToken);
    /** A notification for platforms without a KNotifications backend; see hasNativeBackend(). */
    void fallbackMessage(const QString &title, const QString &text, qlonglong taskId, const QString &view, bool warning);

private:
    void check();
    void schedule(const QDateTime &next);
    void showReminder(const KTasks::Task &task, bool snoozed);
    void showRoutineReminder(const KTasks::RoutineBlock &block, const QDate &date);
    void showMissed(const QList<const KTasks::Task *> &tasks);
    void maybeShowSummary(const QDateTime &now);
    void closeObsolete();
    void snooze(const QString &uid, int minutes);
    void deliver(KNotification *n, qlonglong taskId, const QString &view, bool warning = false);

    TaskStore *m_store;
    Settings *m_settings;
    RoutineManager *m_routine = nullptr;
    QTimer m_timer;
    QHash<QString, QPointer<KNotification>> m_open; // by task uid
    QHash<QString, QPointer<KNotification>> m_openRoutines; // by routine uid
    bool m_firstCheck = true;
};
