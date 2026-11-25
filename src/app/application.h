// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "analyticsmanager.h"
#include "backupmanager.h"
#include "eisenhowermanager.h"
#include "focusmanager.h"
#include "journalmanager.h"
#include "notifier.h"
#include "pdfexporter.h"
#include "routinemanager.h"
#include "settings.h"
#include "sidebarmodel.h"
#include "summarymanager.h"
#include "syncmanager.h"
#include "taskmodel.h"
#include "taskstore.h"

#include <AbstractKirigamiApplication>
#include <QColor>
#include <QHash>
#include <QUrl>
#include <QVariantMap>

class KStatusNotifierItem;
class QWindow;

/**
 * The object QML talks to. Exposes the models and every user action; also
 * owns the desktop integration (tray icon, global shortcut, launcher badge,
 * configurable shortcuts via the Kirigami action collection).
 */
class Application : public AbstractKirigamiApplication
{
    Q_OBJECT
    Q_PROPERTY(TaskModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(SidebarModel *sidebar READ sidebar CONSTANT)
    Q_PROPERTY(Settings *settings READ settings CONSTANT)
    Q_PROPERTY(SyncManager *sync READ sync CONSTANT)
    Q_PROPERTY(Notifier *notifier READ notifier CONSTANT)
    Q_PROPERTY(JournalManager *journal READ journal CONSTANT)
    Q_PROPERTY(RoutineManager *routine READ routine CONSTANT)
    Q_PROPERTY(EisenhowerManager *eisenhower READ eisenhower CONSTANT)
    Q_PROPERTY(FocusManager *focus READ focus CONSTANT)
    Q_PROPERTY(AnalyticsManager *analytics READ analytics CONSTANT)
    Q_PROPERTY(SummaryManager *summary READ summary CONSTANT)
    Q_PROPERTY(BackupManager *backup READ backup CONSTANT)
    Q_PROPERTY(PdfExporter *pdf READ pdf CONSTANT)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoChanged)
    Q_PROPERTY(QStringList shortcutActions READ shortcutActions NOTIFY shortcutsChanged)
    Q_PROPERTY(int shortcutRevision READ shortcutRevision NOTIFY shortcutsChanged)
    Q_PROPERTY(QStringList colorSchemes READ colorSchemes CONSTANT)
    Q_PROPERTY(int colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged)
    Q_PROPERTY(bool autostart READ autostart WRITE setAutostart NOTIFY autostartChanged)
    Q_PROPERTY(QString globalShortcut READ globalShortcut NOTIFY shortcutsChanged)
    Q_PROPERTY(bool supportsGlobalShortcut READ supportsGlobalShortcut CONSTANT)
    Q_PROPERTY(bool supportsTaskbarBadge READ supportsTaskbarBadge CONSTANT)
    Q_PROPERTY(bool supportsDoNotDisturb READ supportsDoNotDisturb CONSTANT)
    Q_PROPERTY(bool supportsLinuxDesktop READ supportsLinuxDesktop CONSTANT)
    Q_PROPERTY(QString systemAvatarPath READ systemAvatarPath CONSTANT)
    Q_PROPERTY(QString systemFullName READ systemFullName CONSTANT)
    Q_PROPERTY(QString effectiveAvatarPath READ effectiveAvatarPath NOTIFY profileChanged)
    Q_PROPERTY(QString effectiveProfileName READ effectiveProfileName NOTIFY profileChanged)
    Q_PROPERTY(QString effectiveProfileEmail READ effectiveProfileEmail NOTIFY profileChanged)

public:
    Application(KTasks::Database *db, QObject *parent = nullptr);
    ~Application() override;

    TaskModel *tasks() const
    {
        return m_tasks;
    }
    SidebarModel *sidebar() const
    {
        return m_sidebar;
    }
    Settings *settings() const
    {
        return m_settings;
    }
    SyncManager *sync() const
    {
        return m_sync;
    }
    Notifier *notifier() const
    {
        return m_notifier;
    }
    JournalManager *journal() const
    {
        return m_journal;
    }
    RoutineManager *routine() const
    {
        return m_routine;
    }
    EisenhowerManager *eisenhower() const
    {
        return m_eisenhower;
    }
    FocusManager *focus() const
    {
        return m_focus;
    }
    AnalyticsManager *analytics() const
    {
        return m_analytics;
    }
    SummaryManager *summary() const
    {
        return m_summary;
    }
    BackupManager *backup() const
    {
        return m_backup;
    }
    PdfExporter *pdf() const
    {
        return m_pdf;
    }
    int revision() const
    {
        return m_revision;
    }
    bool canUndo() const
    {
        return m_store->canUndo();
    }
    bool canRedo() const
    {
        return m_store->canRedo();
    }
    QString undoText() const
    {
        return m_store->undoText();
    }
    QString redoText() const
    {
        return m_store->redoText();
    }
    QStringList shortcutActions() const;
    int shortcutRevision() const
    {
        return m_shortcutRevision;
    }
    QStringList colorSchemes() const;
    int colorScheme() const;
    void setColorScheme(int index);
    bool autostart() const;
    void setAutostart(bool on);
    QString globalShortcut() const;
    bool supportsGlobalShortcut() const;
    bool supportsTaskbarBadge() const;
    bool supportsDoNotDisturb() const;
    bool supportsLinuxDesktop() const;
    QString systemAvatarPath() const;
    QString systemFullName() const;
    QString effectiveAvatarPath() const;
    QString effectiveProfileName() const;
    QString effectiveProfileEmail() const;

    void setWindow(QWindow *window);
    /** Handles command line arguments, both at start and from a second launch. */
    void handleArguments(const QStringList &arguments, bool initial);
    bool startHidden() const
    {
        return m_startHidden;
    }

    // --- tasks ---
    /** Creates a task from quick-entry text ("Pay rent tomorrow 9am !high #home"). Returns its id. */
    Q_INVOKABLE qlonglong quickAdd(const QString &text, qlonglong listId = 0, qlonglong parentId = 0);
    /** What quickAdd() would understand, for the live preview. */
    Q_INVOKABLE QVariantMap parsePreview(const QString &text) const;
    Q_INVOKABLE QVariantMap task(qlonglong id) const;
    Q_INVOKABLE void updateTask(qlonglong id, const QVariantMap &changes);
    Q_INVOKABLE void setDone(qlonglong id, bool done);
    Q_INVOKABLE void setDoneMany(const QVariantList &ids, bool done);
    Q_INVOKABLE void deleteTask(qlonglong id);
    Q_INVOKABLE void deleteTasks(const QVariantList &ids);
    Q_INVOKABLE void moveTask(qlonglong id, qlonglong listId);
    Q_INVOKABLE void moveTasks(const QVariantList &ids, qlonglong listId);
    Q_INVOKABLE void postpone(qlonglong id, int days);
    Q_INVOKABLE void postponeMany(const QVariantList &ids, int days);
    Q_INVOKABLE void updateTasks(const QVariantList &ids, const QVariantMap &changes);
    Q_INVOKABLE void setDueDate(qlonglong id, const QDateTime &date);
    Q_INVOKABLE void clearDue(qlonglong id);
    Q_INVOKABLE void setDueTime(qlonglong id, const QDateTime &time);
    Q_INVOKABLE void clearDueTime(qlonglong id);
    Q_INVOKABLE void setStartDate(qlonglong id, const QDateTime &date);
    Q_INVOKABLE void clearStart(qlonglong id);
    Q_INVOKABLE void addTag(qlonglong id, const QString &tag);
    Q_INVOKABLE void removeTag(qlonglong id, const QString &tag);
    Q_INVOKABLE void addReminder(qlonglong id, int minutesBefore);
    Q_INVOKABLE void removeReminder(qlonglong id, int index);
    Q_INVOKABLE void setRecurrence(qlonglong id, const QString &freq, int interval, bool weekdays);
    Q_INVOKABLE void setRecurrenceRule(qlonglong id, const QVariantMap &parts);
    Q_INVOKABLE QStringList allTags() const;
    Q_INVOKABLE QVariantList openTasks() const;
    Q_INVOKABLE int deleteCompleted(qlonglong listId);

    // --- lists ---
    Q_INVOKABLE QVariantList lists() const;
    Q_INVOKABLE QVariantMap listInfo(qlonglong id) const;
    Q_INVOKABLE qlonglong addList(const QString &name, const QString &color, qlonglong accountId);
    Q_INVOKABLE void updateList(qlonglong id, const QString &name, const QString &color);
    Q_INVOKABLE bool moveListToAccount(qlonglong id, qlonglong accountId);
    Q_INVOKABLE void removeList(qlonglong id);

    // --- misc ---
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void trimMemory(QObject *contextObj = nullptr);
    Q_INVOKABLE int exportIcs(const QUrl &file, qlonglong listId);
    Q_INVOKABLE int importIcs(const QUrl &file, qlonglong listId);
    Q_INVOKABLE QString shortcutOf(const QString &actionName) const;
    Q_INVOKABLE void trigger(const QString &actionName);
    Q_INVOKABLE void openNotificationSettings();
    Q_INVOKABLE void openGlobalShortcutSettings();
    Q_INVOKABLE void showWindow(const QString &xdgToken = QString());
    Q_INVOKABLE void activateQuickAddWindow(QWindow *win);
    Q_INVOKABLE QColor adaptiveChipColor(const QColor &tint, const QColor &bg) const;
    Q_INVOKABLE int queryKeyboardModifiers() const;
    Q_INVOKABLE QString formatTaskDetailsText(qlonglong id) const;
    Q_INVOKABLE void copyTaskDetails(qlonglong id);
    Q_INVOKABLE void copyTasksDetails(const QVariantList &ids);
    Q_INVOKABLE void emailTaskDetails(qlonglong id);
    Q_INVOKABLE void emailTasksDetails(const QVariantList &ids);
    Q_INVOKABLE void setProfileAvatarFromUrl(const QUrl &url);
    Q_INVOKABLE void saveProfile(const QString &name, const QString &email);
    Q_INVOKABLE void quit();

Q_SIGNALS:
    void revisionChanged();
    void undoChanged();
    void shortcutsChanged();
    void colorSchemeChanged();
    void autostartChanged();
    void profileChanged();

    // requests for the window
    void focusQuickAdd();
    void focusSearch();
    void showSettings();
    void showTask(qlonglong id);
    void showView(const QString &view);
    void showSection(const QString &section); // "tasks", "routine", "journal"
    void quickAddWindowRequested();
    void newListRequested();
    void completeCurrentRequested();
    void deleteCurrentRequested();
    void importRequested();
    void exportRequested();
    void printRequested();
    void message(const QString &text, bool withUndo);

protected:
    void setupActions() override;

private:
    QAction *addAppAction(const QString &name, const QString &text, const QString &icon, const QKeySequence &shortcut, std::function<void()> fn);
    void setupTray();
    void updateTray();
    void updateBadge();
    void setupGlobalShortcut();
    bool editTask(qlonglong id, const QString &what, const std::function<void(KTasks::Task &)> &fn);
    QString describeRecurrence(const QString &rrule) const;
    static QString describeAlarm(const KTasks::Alarm &a);

    TaskStore *m_store;
    Settings *m_settings;
    TaskModel *m_tasks;
    SidebarModel *m_sidebar;
    SyncManager *m_sync;
    Notifier *m_notifier;
    JournalManager *m_journal;
    RoutineManager *m_routine;
    EisenhowerManager *m_eisenhower;
    FocusManager *m_focus;
    AnalyticsManager *m_analytics;
    SummaryManager *m_summary;
    BackupManager *m_backup;
    PdfExporter *m_pdf;
    KStatusNotifierItem *m_tray = nullptr;
    QAction *m_globalQuickAdd = nullptr;
    QPointer<QWindow> m_window;
    mutable QHash<quint64, QColor> m_chipColorCache;
    int m_revision = 0;
    int m_shortcutRevision = 0;
    bool m_startHidden = false;
};
