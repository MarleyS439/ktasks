// SPDX-License-Identifier: GPL-3.0-or-later
#include "application.h"
#include "icscodec.h"
#include "logging.h"
#include "naturalparser.h"
#include "recurrence.h"

#include <KColorSchemeManager>
#include <KColorSchemeModel>
#ifdef KTASKS_HAVE_GLOBALACCEL
#include <KGlobalAccel>
#endif
#include <KLocalizedString>
#include <KStatusNotifierItem>
#include <KUser>
#ifndef Q_OS_WIN
#include <KWindowSystem>
#endif
#include <QClipboard>
#include <QCommandLineParser>
#include <QCoreApplication>
#ifdef KTASKS_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusMessage>
#endif
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QMenu>
#include <QProcess>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QTimeZone>
#include <QUrlQuery>
#include <QWindow>
#include <cmath>

using namespace KTasks;

static const QString kDesktopId = QStringLiteral("io.github.marleys439.ktasks");
static const QString kAppIconId = QStringLiteral("io.github.marleys439.ktasks");
static const QString kSymbolicIconId = QStringLiteral("view-task-symbolic");

Application::Application(Database *db, QObject *parent)
    : AbstractKirigamiApplication(parent)
{
    m_settings = new Settings(this);
    m_store = new TaskStore(db, this);
    m_tasks = new TaskModel(m_store, m_settings, this);
    m_sidebar = new SidebarModel(m_store, this);
    m_sync = new SyncManager(m_store, m_settings, this);
    m_journal = new JournalManager(db, this);
    m_routine = new RoutineManager(db, this);
    m_eisenhower = new EisenhowerManager(m_store, m_settings, this);
    m_focus = new FocusManager(m_store, m_settings, this);
    m_analytics = new AnalyticsManager(m_store, m_routine, m_focus, this);
    m_summary = new SummaryManager(m_store, m_routine, m_analytics, m_focus, m_settings, this);
    m_summary->setJournalManager(m_journal);
    m_backup = new BackupManager(db, m_settings, this);
    m_pdf = new PdfExporter(m_store, m_tasks, m_routine, m_journal, this);
    m_notifier = new Notifier(m_store, m_settings, this);
    m_notifier->setRoutineManager(m_routine);

    connect(m_eisenhower, &EisenhowerManager::message, this, &Application::message);
    connect(m_focus, &FocusManager::message, this, &Application::message);
    connect(m_summary, &SummaryManager::message, this, &Application::message);

    connect(m_backup, &BackupManager::message, this, [this](const QString &text, bool) {
        Q_EMIT message(text, false);
    });
    connect(m_pdf, &PdfExporter::message, this, [this](const QString &text, bool) {
        Q_EMIT message(text, false);
    });
    connect(m_backup, &BackupManager::restored, this, [this] {
        m_store->reload();
        m_journal->reload();
        m_routine->reload();
    });

    // A view that no longer exists (deleted list) falls back to "Today".
    connect(m_store, &TaskStore::changed, this, [this] {
        ++m_revision;
        Q_EMIT revisionChanged();
        if (m_tasks->isListView() && !m_store->list(m_tasks->listId())) {
            m_tasks->setView(QStringLiteral("today"));
        }
    });
    connect(m_store, &TaskStore::undoChanged, this, &Application::undoChanged);
    connect(m_sidebar, &SidebarModel::countsChanged, this, [this] {
        updateTray();
        updateBadge();
    });
    connect(m_settings, &Settings::showTrayIconChanged, this, &Application::setupTray);
    connect(m_settings, &Settings::showBadgeChanged, this, &Application::updateBadge);
    connect(m_settings, &Settings::profileNameChanged, this, &Application::profileChanged);
    connect(m_settings, &Settings::profileEmailChanged, this, &Application::profileChanged);
    connect(m_settings, &Settings::profileAvatarPathChanged, this, &Application::profileChanged);
    connect(m_settings, &Settings::useSystemAvatarChanged, this, &Application::profileChanged);

    connect(m_sync, &SyncManager::conflicts, m_notifier, &Notifier::notifyConflicts);
    connect(m_sync, &SyncManager::syncProblem, m_notifier, &Notifier::notifySyncProblem);
    connect(m_sync, &SyncManager::notesSynced, m_journal, &JournalManager::reload);
    connect(m_sync, &SyncManager::accountsChanged, m_journal, &JournalManager::reload);
    connect(m_journal, &JournalManager::syncNeeded, m_sync, &SyncManager::scheduleNotesSync);
    connect(m_sync, &SyncManager::routinesSynced, m_routine, &RoutineManager::reload);
    connect(m_sync, &SyncManager::accountsChanged, m_routine, &RoutineManager::reload);
    connect(m_routine, &RoutineManager::syncNeeded, m_sync, &SyncManager::scheduleRoutinesSync);
    connect(m_journal, &JournalManager::message, this, [this](const QString &text) {
        Q_EMIT message(text, false);
    });
    connect(m_notifier, &Notifier::activateRequested, this, [this](qlonglong taskId, const QString &view, const QString &token) {
        showWindow(token);
        if (view == QLatin1String("settings")) {
            Q_EMIT showSettings();
        } else if (view == QLatin1String("routine")) {
            Q_EMIT showSection(QStringLiteral("routine"));
        } else if (view == QLatin1String("journal")) {
            Q_EMIT showSection(QStringLiteral("journal"));
        } else if (view.startsWith(QLatin1String("search:"))) {
            Q_EMIT showSection(QStringLiteral("tasks"));
            m_tasks->setView(QStringLiteral("all"));
            m_tasks->setSearchText(i18nc("appended to the title of the local copy kept after a sync conflict", "conflicting copy"));
        } else if (!view.isEmpty()) {
            Q_EMIT showSection(QStringLiteral("tasks"));
            m_tasks->setSearchText(QString());
            m_tasks->setView(view);
        }
        if (taskId > 0) {
            if (const Task *t = m_store->task(taskId)) {
                if (m_tasks->indexOfTask(taskId) < 0) {
                    m_tasks->setSearchText(QString());
                    m_tasks->setView(QStringLiteral("list:%1").arg(t->listId));
                }
                Q_EMIT showTask(taskId);
            }
        }
    });

    setupActions();
    setupGlobalShortcut();
    setupTray();
    updateBadge();
}

Application::~Application() = default;

// ---- actions and shortcuts ----

QAction *Application::addAppAction(const QString &name, const QString &text, const QString &icon, const QKeySequence &shortcut, std::function<void()> fn)
{
    auto *action = mainCollection()->addAction(name, this, [fn] {
        fn();
    });
    action->setText(text);
    action->setIcon(QIcon::fromTheme(icon));
    if (!shortcut.isEmpty()) {
        KirigamiActionCollection::setDefaultShortcut(action, shortcut);
    }
    return action;
}

void Application::setupActions()
{
    AbstractKirigamiApplication::setupActions();

    addAppAction(QStringLiteral("add_task"), i18nc("@action", "New Task"), QStringLiteral("list-add"), QKeySequence(Qt::CTRL | Qt::Key_N), [this] {
        Q_EMIT focusQuickAdd();
    });
    addAppAction(QStringLiteral("add_list"), i18nc("@action", "New List…"), QStringLiteral("folder-new"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), [this] {
        Q_EMIT newListRequested();
    });
    addAppAction(QStringLiteral("find"), i18nc("@action", "Search"), QStringLiteral("search"), QKeySequence(Qt::CTRL | Qt::Key_F), [this] {
        Q_EMIT focusSearch();
    });
    addAppAction(QStringLiteral("sync_now"), i18nc("@action", "Synchronize Now"), QStringLiteral("view-refresh"), QKeySequence(Qt::Key_F5), [this] {
        m_sync->syncNow();
    });
    addAppAction(QStringLiteral("edit_undo"), i18nc("@action", "Undo"), QStringLiteral("edit-undo"), QKeySequence(Qt::CTRL | Qt::Key_Z), [this] {
        undo();
    });
    addAppAction(QStringLiteral("edit_redo"), i18nc("@action", "Redo"), QStringLiteral("edit-redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] {
        redo();
    });
    addAppAction(QStringLiteral("toggle_completed"), i18nc("@action", "Show or Hide Completed Tasks"), QStringLiteral("view-visible"), QKeySequence(Qt::CTRL | Qt::Key_H), [this] {
        m_tasks->setShowCompleted(!m_tasks->showCompleted());
    });
    addAppAction(QStringLiteral("complete_task"), i18nc("@action", "Complete Selected Task"), QStringLiteral("task-complete"), QKeySequence(Qt::CTRL | Qt::Key_Return), [this] {
        Q_EMIT completeCurrentRequested();
    });
    addAppAction(QStringLiteral("delete_task"), i18nc("@action", "Delete Selected Task"), QStringLiteral("edit-delete"), QKeySequence(Qt::Key_Delete), [this] {
        Q_EMIT deleteCurrentRequested();
    });
    addAppAction(QStringLiteral("view_summary"), i18nc("@action", "Go to Summary"), QStringLiteral("go-home"), QKeySequence(Qt::CTRL | Qt::Key_0), [this] {
        Q_EMIT showSection(QStringLiteral("summary"));
    });
    addAppAction(QStringLiteral("view_today"), i18nc("@action", "Go to Today"), QStringLiteral("view-calendar-day"), QKeySequence(Qt::CTRL | Qt::Key_1), [this] {
        Q_EMIT showView(QStringLiteral("today"));
    });
    addAppAction(QStringLiteral("view_week"), i18nc("@action", "Go to Next 7 Days"), QStringLiteral("view-calendar-week"), QKeySequence(Qt::CTRL | Qt::Key_2), [this] {
        Q_EMIT showView(QStringLiteral("week"));
    });
    addAppAction(QStringLiteral("view_all"), i18nc("@action", "Go to All Tasks"), QStringLiteral("view-task"), QKeySequence(Qt::CTRL | Qt::Key_3), [this] {
        Q_EMIT showSection(QStringLiteral("tasks"));
        Q_EMIT showView(QStringLiteral("all"));
    });
    addAppAction(QStringLiteral("view_routine"), i18nc("@action", "Go to Routine"), QStringLiteral("chronometer"), QKeySequence(Qt::CTRL | Qt::Key_4), [this] {
        Q_EMIT showSection(QStringLiteral("routine"));
    });
    addAppAction(QStringLiteral("view_journal"), i18nc("@action", "Go to Journal"), QStringLiteral("story-editor"), QKeySequence(Qt::CTRL | Qt::Key_5), [this] {
        Q_EMIT showSection(QStringLiteral("journal"));
    });
    addAppAction(QStringLiteral("print_tasks"), i18nc("@action", "Print / Export PDF…"), QStringLiteral("document-print"), QKeySequence::Print, [this] {
        Q_EMIT printRequested();
    });
    addAppAction(QStringLiteral("quick_add_window"), i18nc("@action", "Quick Entry Window"), QStringLiteral("document-new"), QKeySequence(), [this] {
        Q_EMIT quickAddWindowRequested();
    });
    addAppAction(QStringLiteral("import_ics"), i18nc("@action", "Import from iCalendar File…"), QStringLiteral("document-import"), QKeySequence(), [this] {
        Q_EMIT importRequested();
    });
    addAppAction(QStringLiteral("export_ics"), i18nc("@action", "Export to iCalendar File…"), QStringLiteral("document-export"), QKeySequence(), [this] {
        Q_EMIT exportRequested();
    });
    addAppAction(QStringLiteral("options_configure"), i18nc("@action", "Settings…"), QStringLiteral("configure"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma), [this] {
        Q_EMIT showSettings();
    });

    readSettings();

    const auto actions = mainCollection()->actions();
    for (QAction *a : actions) {
        connect(a, &QAction::changed, this, [this] {
            ++m_shortcutRevision;
            Q_EMIT shortcutsChanged();
        });
    }
}

QStringList Application::shortcutActions() const
{
    QStringList names;
    const auto actions = mainCollection()->actions();
    for (const QAction *a : actions) {
        if (!a->objectName().isEmpty()) {
            names << a->objectName();
        }
    }
    return names;
}

QString Application::shortcutOf(const QString &actionName) const
{
    const QAction *a = const_cast<Application *>(this)->action(actionName);
    return a ? a->shortcut().toString(QKeySequence::PortableText) : QString();
}

void Application::trigger(const QString &actionName)
{
    if (QAction *a = action(actionName); a && a->isEnabled()) {
        a->trigger();
    }
}

void Application::setupGlobalShortcut()
{
    m_globalQuickAdd = new QAction(i18nc("@action global shortcut", "Add a Task from Anywhere"), this);
    m_globalQuickAdd->setObjectName(QStringLiteral("quick_add_global"));
    m_globalQuickAdd->setProperty("componentName", kDesktopId + QStringLiteral(".desktop"));
    m_globalQuickAdd->setProperty("componentDisplayName", i18n("KTasks"));
    connect(m_globalQuickAdd, &QAction::triggered, this, [this] {
        Q_EMIT quickAddWindowRequested();
    });
#ifdef KTASKS_HAVE_GLOBALACCEL
    const QKeySequence defaultSeq(Qt::META | Qt::ALT | Qt::Key_N);
    // Avoid conflict if the desktop action "QuickAdd" (X-KDE-Shortcuts) already owns Meta+Alt+N.
    const auto desktopKeys = KGlobalAccel::self()->globalShortcut(kDesktopId + QStringLiteral(".desktop"), QStringLiteral("QuickAdd"));
    if (desktopKeys.isEmpty()) {
        KGlobalAccel::self()->setDefaultShortcut(m_globalQuickAdd, {defaultSeq});
        KGlobalAccel::self()->setShortcut(m_globalQuickAdd, {defaultSeq});
    } else {
        KGlobalAccel::self()->setDefaultShortcut(m_globalQuickAdd, {});
        KGlobalAccel::self()->setShortcut(m_globalQuickAdd, {});
    }
    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutChanged, this, [this](QAction *a) {
        if (a == m_globalQuickAdd) {
            Q_EMIT shortcutsChanged();
        }
    });
#endif
}

QString Application::globalShortcut() const
{
#ifdef KTASKS_HAVE_GLOBALACCEL
    const auto keys = KGlobalAccel::self()->shortcut(m_globalQuickAdd);
    if (!keys.isEmpty()) {
        return keys.first().toString(QKeySequence::NativeText);
    }
    const auto desktopKeys = KGlobalAccel::self()->globalShortcut(kDesktopId + QStringLiteral(".desktop"), QStringLiteral("QuickAdd"));
    return desktopKeys.isEmpty() ? QString() : desktopKeys.first().toString(QKeySequence::NativeText);
#else
    return QString();
#endif
}

bool Application::supportsGlobalShortcut() const
{
#ifdef KTASKS_HAVE_GLOBALACCEL
    return true;
#else
    return false;
#endif
}

bool Application::supportsTaskbarBadge() const
{
#ifdef KTASKS_HAVE_DBUS
    return true;
#else
    return false;
#endif
}

bool Application::supportsDoNotDisturb() const
{
#ifdef KTASKS_HAVE_DBUS
    return true;
#else
    return false;
#endif
}

bool Application::supportsLinuxDesktop() const
{
#ifdef Q_OS_LINUX
    return true;
#else
    return false;
#endif
}

void Application::activateQuickAddWindow(QWindow *win)
{
    if (!win) {
        return;
    }
    win->show();
    if (win->windowState() & Qt::WindowMinimized) {
        win->setWindowStates(win->windowStates() & ~Qt::WindowMinimized);
    }
    win->raise();
#ifndef Q_OS_WIN
    KWindowSystem::updateStartupId(win);
    KWindowSystem::activateWindow(win);
#else
    win->requestActivate();
#endif
}

QString Application::systemAvatarPath() const
{
    const KUser user;
    QString path = user.faceIconPath();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        return path;
    }
    const QString homeFace = QDir::homePath() + QStringLiteral("/.face");
    if (QFileInfo::exists(homeFace)) {
        return homeFace;
    }
    const QString homeFaceIcon = QDir::homePath() + QStringLiteral("/.face.icon");
    if (QFileInfo::exists(homeFaceIcon)) {
        return homeFaceIcon;
    }
    return {};
}

QString Application::systemFullName() const
{
    const KUser user;
    const QString full = user.property(KUser::FullName).toString().trimmed();
    return full.isEmpty() ? user.loginName() : full;
}

QString Application::effectiveAvatarPath() const
{
    if (m_settings->useSystemAvatar()) {
        return systemAvatarPath();
    }
    const QString custom = m_settings->profileAvatarPath();
    if (!custom.isEmpty() && QFileInfo::exists(custom)) {
        return custom;
    }
    return {};
}

QString Application::effectiveProfileName() const
{
    const QString custom = m_settings->profileName().trimmed();
    return custom.isEmpty() ? systemFullName() : custom;
}

QString Application::effectiveProfileEmail() const
{
    return m_settings->profileEmail().trimmed();
}

void Application::setProfileAvatarFromUrl(const QUrl &url)
{
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        m_settings->setUseSystemAvatar(false);
        m_settings->setProfileAvatarPath(path);
    }
}

void Application::saveProfile(const QString &name, const QString &email)
{
    m_settings->setProfileName(name.trimmed());
    m_settings->setProfileEmail(email.trimmed());
    Q_EMIT profileChanged();
    Q_EMIT message(i18n("Profile saved"), false);
}

QString Application::formatTaskDetailsText(qlonglong id) const
{
    const Task *t = m_store->task(id);
    if (!t) {
        return {};
    }
    const QLocale locale;
    QStringList lines;
    lines << t->title;
    lines << QString(t->title.size() > 40 ? 40 : qMax(12, int(t->title.size())), QChar(0x2500));
    if (const TaskList *l = m_store->list(t->listId)) {
        lines << i18n("List: %1", l->name);
    }
    lines << i18n("Status: %1", t->isDone() ? i18n("Completed") : i18n("Pending"));
    if (t->due.isValid()) {
        lines << i18n("Due: %1", TaskModel::formatDue(*t, QDateTime::currentDateTime()));
    }
    if (t->start.isValid()) {
        const QDate s = t->allDay ? t->start.date() : t->start.toLocalTime().date();
        lines << i18n("Start: %1", locale.toString(s, QLocale::ShortFormat));
    }
    if (t->priorityLevel() > 0) {
        const QString pLabel = t->priorityLevel() == 3 ? i18nc("priority", "High")
            : t->priorityLevel() == 2                  ? i18nc("priority", "Medium")
                                                       : i18nc("priority", "Low");
        lines << i18n("Priority: %1", pLabel);
    }
    if (!t->rrule.isEmpty()) {
        lines << i18n("Repeat: %1", describeRecurrence(t->rrule));
    }
    if (!t->tags.isEmpty()) {
        QStringList hashTags;
        for (const QString &tag : t->tags) {
            hashTags << QLatin1Char('#') + tag;
        }
        lines << i18n("Tags: %1", hashTags.join(QLatin1Char(' ')));
    }
    const auto children = m_store->childrenOf(*t);
    if (!children.isEmpty()) {
        lines << QString();
        lines << i18n("Subtasks:");
        for (const Task *c : children) {
            lines << QStringLiteral("  %1 %2").arg(c->isDone() ? QStringLiteral("[x]") : QStringLiteral("[ ]"), c->title);
        }
    }
    if (!t->notes.trimmed().isEmpty()) {
        lines << QString();
        lines << i18n("Notes:");
        lines << t->notes.trimmed();
    }
    return lines.join(QLatin1Char('\n'));
}

static QList<qint64> toTaskIdList(const QVariantList &ids)
{
    QList<qint64> out;
    out.reserve(ids.size());
    for (const QVariant &v : ids) {
        const qint64 id = v.toLongLong();
        if (id > 0 && !out.contains(id)) {
            out.append(id);
        }
    }
    return out;
}

void Application::copyTaskDetails(qlonglong id)
{
    copyTasksDetails({id});
}

void Application::copyTasksDetails(const QVariantList &ids)
{
    const QList<qint64> taskIds = toTaskIdList(ids);
    QStringList blocks;
    for (qint64 id : taskIds) {
        const QString text = formatTaskDetailsText(id);
        if (!text.isEmpty()) {
            blocks << text;
        }
    }
    if (blocks.isEmpty()) {
        return;
    }
    if (QClipboard *cb = QGuiApplication::clipboard()) {
        cb->setText(blocks.join(QStringLiteral("\n\n")));
        Q_EMIT message(blocks.size() > 1
                           ? i18np("Details of %1 task copied to clipboard", "Details of %1 tasks copied to clipboard", blocks.size())
                           : i18n("Task details copied to clipboard"),
                       false);
    }
}

void Application::emailTaskDetails(qlonglong id)
{
    emailTasksDetails({id});
}

void Application::emailTasksDetails(const QVariantList &ids)
{
    const QList<qint64> taskIds = toTaskIdList(ids);
    QStringList blocks;
    QString firstTitle;
    for (qint64 id : taskIds) {
        const Task *t = m_store->task(id);
        const QString text = formatTaskDetailsText(id);
        if (t && !text.isEmpty()) {
            if (firstTitle.isEmpty()) {
                firstTitle = t->title;
            }
            blocks << text;
        }
    }
    if (blocks.isEmpty()) {
        return;
    }
    QUrl url;
    url.setScheme(QStringLiteral("mailto"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("subject"),
                       blocks.size() == 1 ? i18n("Task: %1", firstTitle) : i18np("%1 task", "%1 tasks", blocks.size()));
    query.addQueryItem(QStringLiteral("body"), blocks.join(QStringLiteral("\n\n")));
    url.setQuery(query);
    QDesktopServices::openUrl(url);
}

int Application::queryKeyboardModifiers() const
{
    return int(QGuiApplication::queryKeyboardModifiers());
}

static double channelLuminance(double v)
{
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

static double relativeLuminance(const QColor &c)
{
    return 0.2126 * channelLuminance(c.redF()) + 0.7152 * channelLuminance(c.greenF()) + 0.0722 * channelLuminance(c.blueF());
}

QColor Application::adaptiveChipColor(const QColor &tint, const QColor &bg) const
{
    const quint64 key = (quint64(tint.rgb()) << 32) | quint64(bg.rgb());
    const auto it = m_chipColorCache.constFind(key);
    if (it != m_chipColorCache.constEnd()) {
        return it.value();
    }

    const bool darkBg = (0.2126 * bg.redF() + 0.7152 * bg.greenF() + 0.0722 * bg.blueF()) < 0.5;
    const double bgLum = relativeLuminance(bg);
    const double targetR = darkBg ? 1.0 : 0.08;
    const double targetG = darkBg ? 1.0 : 0.09;
    const double targetB = darkBg ? 1.0 : 0.11;

    double r = tint.redF();
    double g = tint.greenF();
    double b = tint.blueF();
    for (int step = 0; step < 12; ++step) {
        const QColor cand = QColor::fromRgbF(float(r), float(g), float(b));
        const double l1 = relativeLuminance(cand);
        const double ratio = (std::max(l1, bgLum) + 0.05) / (std::min(l1, bgLum) + 0.05);
        if (ratio >= 4.8) {
            if (m_chipColorCache.size() > 256) {
                m_chipColorCache.clear();
            }
            m_chipColorCache.insert(key, cand);
            return cand;
        }
        r += (targetR - r) * 0.22;
        g += (targetG - g) * 0.22;
        b += (targetB - b) * 0.22;
    }
    const QColor result = QColor::fromRgbF(float(r), float(g), float(b));
    if (m_chipColorCache.size() > 256) {
        m_chipColorCache.clear();
    }
    m_chipColorCache.insert(key, result);
    return result;
}

void Application::openGlobalShortcutSettings()
{
#ifdef Q_OS_LINUX
    QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_keys")});
#endif
}

// ---- tray, badge, window ----

void Application::setupTray()
{
    if (!m_settings->showTrayIcon()) {
        delete m_tray;
        m_tray = nullptr;
        return;
    }
    if (!m_tray) {
        m_tray = new KStatusNotifierItem(QStringLiteral("ktasks"), this);
        m_tray->setCategory(KStatusNotifierItem::ApplicationStatus);
        m_tray->setIconByName(kSymbolicIconId);
        m_tray->setTitle(i18n("KTasks"));
        m_tray->setStandardActionsEnabled(false);
        QMenu *menu = m_tray->contextMenu();
        menu->addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action tray menu", "New Task…"), this, [this] {
            Q_EMIT quickAddWindowRequested();
        });
        menu->addAction(QIcon::fromTheme(QStringLiteral("view-calendar-day")), i18nc("@action tray menu", "Today"), this, [this] {
            showWindow();
            Q_EMIT showView(QStringLiteral("today"));
        });
        menu->addAction(QIcon::fromTheme(QStringLiteral("view-refresh")), i18nc("@action tray menu", "Synchronize Now"), m_sync, &SyncManager::syncNow);
        menu->addSeparator();
        menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18nc("@action tray menu", "Quit"), this, &Application::quit);
        connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this](bool, const QPoint &) {
            if (m_window && m_window->isVisible() && m_window->isActive()) {
                m_window->hide();
            } else {
                showWindow(m_tray->providedToken());
            }
        });
        if (m_window) {
            m_tray->setAssociatedWindow(m_window);
        }
    }
    updateTray();
}

void Application::updateTray()
{
    if (!m_tray) {
        return;
    }
    const int due = m_sidebar->dueCount();
    const int overdue = m_sidebar->overdueCount();
    QString sub;
    if (due == 0) {
        sub = i18n("Nothing due today");
    } else {
        sub = i18np("%1 task due today", "%1 tasks due today", due - overdue);
        if (overdue > 0) {
            sub += QLatin1Char('\n') + i18np("%1 overdue", "%1 overdue", overdue);
        }
    }
    m_tray->setToolTip(kAppIconId, i18n("KTasks"), sub);
    // Stay in the visible part of the tray only when something needs attention.
    m_tray->setStatus(due > 0 ? KStatusNotifierItem::Active : KStatusNotifierItem::Passive);
}

// The Unity launcher API, which Plasma's task manager understands, shows
// the number of tasks due on the application's icon.
void Application::updateBadge()
{
#ifdef KTASKS_HAVE_DBUS
    const int due = m_settings->showBadge() ? m_sidebar->dueCount() : 0;
    QDBusMessage msg = QDBusMessage::createSignal(QStringLiteral("/io/github/marleys439/ktasks"), QStringLiteral("com.canonical.Unity.LauncherEntry"), QStringLiteral("Update"));
    QVariantMap props;
    props[QStringLiteral("count")] = qlonglong(due);
    props[QStringLiteral("count-visible")] = due > 0;
    msg.setArguments({QStringLiteral("application://") + kDesktopId + QStringLiteral(".desktop"), props});
    QDBusConnection::sessionBus().send(msg);
#endif
}

void Application::setWindow(QWindow *window)
{
    m_window = window;
    if (m_tray && window) {
        m_tray->setAssociatedWindow(window);
    }
}

void Application::showWindow(const QString &xdgToken)
{
    if (!m_window) {
        return;
    }
#ifndef Q_OS_WIN
    if (!xdgToken.isEmpty()) {
        KWindowSystem::setCurrentXdgActivationToken(xdgToken);
    }
#else
    Q_UNUSED(xdgToken)
#endif
    m_window->show();
    if (m_window->windowState() & Qt::WindowMinimized) {
        m_window->setWindowStates(m_window->windowStates() & ~Qt::WindowMinimized);
    }
    m_window->raise();
#ifndef Q_OS_WIN
    KWindowSystem::updateStartupId(m_window);
    KWindowSystem::activateWindow(m_window);
#else
    m_window->requestActivate();
#endif
}

void Application::quit()
{
    QCoreApplication::quit();
}

void Application::handleArguments(const QStringList &arguments, bool initial)
{
    QCommandLineParser parser;
    const QCommandLineOption hidden(QStringLiteral("hidden"), i18n("Start in the system tray without showing the window."));
    const QCommandLineOption quick(QStringLiteral("quick-add"), i18n("Open the quick entry window."));
    const QCommandLineOption add(QStringLiteral("add"), i18n("Add a task, e.g. --add \"Buy milk tomorrow 9am #home\"."), QStringLiteral("text"));
    parser.addOptions({hidden, quick, add});
    parser.parse(arguments.isEmpty() ? QStringList{QStringLiteral("ktasks")} : arguments);

    if (parser.isSet(add)) {
        const qlonglong id = quickAdd(parser.value(add));
        if (initial) {
            m_startHidden = true;
        }
        Q_UNUSED(id)
        return;
    }
    if (parser.isSet(quick)) {
        if (initial) {
            m_startHidden = true;
        }
        // Give the window a moment to load on a cold start.
        QTimer::singleShot(initial ? 400 : 0, this, &Application::quickAddWindowRequested);
        return;
    }
    if (parser.isSet(hidden)) {
        if (initial) {
            m_startHidden = true;
        }
        return;
    }
    if (!initial) {
        showWindow();
    }
}

// ---- color scheme, autostart, system settings ----

QStringList Application::colorSchemes() const
{
    QStringList out;
    const QAbstractItemModel *model = KColorSchemeManager::instance()->model();
    for (int i = 0; i < model->rowCount(); ++i) {
        out << model->index(i, 0).data(KColorSchemeModel::NameRole).toString();
    }
    return out;
}

int Application::colorScheme() const
{
    const QString active = KColorSchemeManager::instance()->activeSchemeId();
    const QAbstractItemModel *model = KColorSchemeManager::instance()->model();
    for (int i = 0; i < model->rowCount(); ++i) {
        if (model->index(i, 0).data(KColorSchemeModel::IdRole).toString() == active) {
            return i;
        }
    }
    return 0; // first row: follow the system
}

void Application::setColorScheme(int index)
{
    const QAbstractItemModel *model = KColorSchemeManager::instance()->model();
    if (index < 0 || index >= model->rowCount()) {
        return;
    }
    KColorSchemeManager::instance()->activateScheme(model->index(index, 0));
    Q_EMIT colorSchemeChanged();
}

#ifndef Q_OS_WIN
static QString autostartFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/autostart/") + kDesktopId + QStringLiteral(".desktop");
}
#endif

bool Application::autostart() const
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    return reg.contains(QStringLiteral("KTasks"));
#else
    return QFile::exists(autostartFile());
#endif
}

void Application::setAutostart(bool on)
{
    if (on == autostart()) {
        return;
    }
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);
    if (on) {
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        reg.setValue(QStringLiteral("KTasks"), QStringLiteral("\"%1\" --hidden").arg(exe));
    } else {
        reg.remove(QStringLiteral("KTasks"));
    }
#else
    if (on) {
        QDir().mkpath(QFileInfo(autostartFile()).absolutePath());
        QFile f(autostartFile());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const QString exe = QCoreApplication::applicationFilePath();
            f.write(QStringLiteral("[Desktop Entry]\nType=Application\nName=KTasks\nIcon=%1\nExec=\"%2\" --hidden\nX-KDE-autostart-phase=2\n")
                        .arg(kAppIconId, exe)
                        .toUtf8());
        }
    } else {
        QFile::remove(autostartFile());
    }
#endif
    Q_EMIT autostartChanged();
}

void Application::openNotificationSettings()
{
#ifdef Q_OS_LINUX
    QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_notifications"), QStringLiteral("--args"), QStringLiteral("--notifyrc ktasks")});
#endif
}

// ---- tasks ----

static QDateTime combine(const QDate &date, const QTime &time)
{
    return QDateTime(date, time, QTimeZone::systemTimeZone()).toUTC();
}

static QDate localDate(const Task &t, const QDateTime &dt)
{
    return t.allDay ? dt.date() : dt.toLocalTime().date();
}

qlonglong Application::quickAdd(const QString &text, qlonglong listId, qlonglong parentId)
{
    if (text.trimmed().isEmpty()) {
        return 0;
    }
    const bool monthFirst = QLocale().dateFormat(QLocale::ShortFormat).startsWith(QLatin1Char('M'));
    const ParsedInput p = parseNaturalInput(text, QDateTime::currentDateTime(), monthFirst);
    Task t;
    t.title = p.title;
    t.priority = Task::priorityFromLevel(p.priorityLevel);
    t.tags = p.tags;
    t.rrule = p.rrule;
    if (p.hasDate()) {
        t.allDay = !p.hasTime();
        t.due = p.hasTime() ? combine(p.date, p.time) : QDateTime(p.date, QTime(0, 0));
    }
    // Inherit the context of the view the task is typed into.
    const QString view = m_tasks->view();
    if (!p.hasDate() && parentId == 0) {
        if (view == QLatin1String("today")) {
            t.due = QDateTime(QDate::currentDate(), QTime(0, 0));
        } else if (view == QLatin1String("week")) {
            t.due = QDateTime(QDate::currentDate().addDays(1), QTime(0, 0));
        }
    }
    if (view.startsWith(QLatin1String("tag:")) && !t.tags.contains(view.mid(4), Qt::CaseInsensitive)) {
        t.tags.append(view.mid(4));
    }
    if (m_settings->defaultReminder() >= 0 && t.due.isValid()) {
        t.alarms.append(Alarm{Alarm::RelativeToDue, {}, -m_settings->defaultReminder() * 60});
    }

    qint64 target = listId;
    if (const Task *parent = m_store->task(parentId)) {
        t.parentUid = parent->uid;
        target = parent->listId;
    } else {
        if (!p.listName.isEmpty()) {
            for (const TaskList &l : m_store->lists()) {
                if (l.name.startsWith(p.listName, Qt::CaseInsensitive) && !l.readOnly) {
                    target = l.id;
                    break;
                }
            }
        }
        if (target <= 0 && m_tasks->isListView()) {
            target = m_tasks->listId();
        }
        if (target <= 0) {
            target = m_settings->defaultListId();
        }
    }
    t.listId = m_store->fallbackListId(target);
    return m_store->addTask(t);
}

QVariantMap Application::parsePreview(const QString &text) const
{
    QVariantMap out;
    if (text.trimmed().isEmpty()) {
        return out;
    }
    const bool monthFirst = QLocale().dateFormat(QLocale::ShortFormat).startsWith(QLatin1Char('M'));
    const ParsedInput p = parseNaturalInput(text, QDateTime::currentDateTime(), monthFirst);
    out[QStringLiteral("title")] = p.title;
    if (p.hasDate()) {
        Task t;
        t.allDay = !p.hasTime();
        t.due = p.hasTime() ? combine(p.date, p.time) : QDateTime(p.date, QTime(0, 0));
        out[QStringLiteral("due")] = TaskModel::formatDue(t, QDateTime::currentDateTime());
    }
    out[QStringLiteral("priority")] = p.priorityLevel;
    out[QStringLiteral("tags")] = p.tags;
    if (!p.rrule.isEmpty()) {
        out[QStringLiteral("repeat")] = describeRecurrence(p.rrule);
    }
    if (!p.listName.isEmpty()) {
        for (const TaskList &l : m_store->lists()) {
            if (l.name.startsWith(p.listName, Qt::CaseInsensitive) && !l.readOnly) {
                out[QStringLiteral("list")] = l.name;
                break;
            }
        }
    }
    return out;
}

QString Application::describeRecurrence(const QString &rrule) const
{
    if (rrule.isEmpty()) {
        return QString();
    }
    const auto rp = Recurrence::parseRule(rrule);
    if (!rp.valid) {
        return i18nc("recurrence too complex to describe", "Custom repetition");
    }

    QString base;
    if (rp.freq == QLatin1String("DAILY")) {
        base = i18ncp("recurrence", "Every day", "Every %1 days", rp.interval);
    } else if (rp.freq == QLatin1String("WEEKLY")) {
        static const QStringList kByDayCodes{QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"), QStringLiteral("TH"),
                                             QStringLiteral("FR"), QStringLiteral("SA"), QStringLiteral("SU")};
        if (rp.interval == 1 && rp.byDays == QStringList{QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"), QStringLiteral("TH"), QStringLiteral("FR")}) {
            base = i18nc("recurrence", "Every weekday");
        } else {
            base = i18ncp("recurrence", "Every week", "Every %1 weeks", rp.interval);
            if (!rp.byDays.isEmpty()) {
                QStringList dayNames;
                const QLocale locale;
                for (const QString &code : rp.byDays) {
                    const int idx = kByDayCodes.indexOf(code);
                    if (idx >= 0) {
                        dayNames.append(locale.dayName(idx + 1, QLocale::ShortFormat));
                    }
                }
                if (!dayNames.isEmpty()) {
                    base += QStringLiteral(" (%1)").arg(dayNames.join(QStringLiteral(", ")));
                }
            }
        }
    } else if (rp.freq == QLatin1String("MONTHLY")) {
        base = i18ncp("recurrence", "Every month", "Every %1 months", rp.interval);
    } else {
        base = i18ncp("recurrence", "Every year", "Every %1 years", rp.interval);
    }

    if (rp.endMode == QLatin1String("count")) {
        base += QStringLiteral(" · ") + i18ncp("recurrence limit", "%1 time", "%1 times", rp.count);
    } else if (rp.endMode == QLatin1String("until") && rp.until.isValid()) {
        base += QStringLiteral(" · ") + i18nc("recurrence limit, %1 is date", "until %1", QLocale().toString(rp.until, QLocale::ShortFormat));
    }
    return base;
}

QString Application::describeAlarm(const Alarm &a)
{
    if (a.kind == Alarm::Absolute) {
        return QLocale().toString(a.at.toLocalTime(), QLocale::ShortFormat);
    }
    const int minutes = -a.offsetSecs / 60;
    const bool start = a.kind == Alarm::RelativeToStart;
    if (minutes == 0) {
        return start ? i18nc("reminder", "When it starts") : i18nc("reminder", "When it is due");
    }
    if (minutes < 0) {
        return i18ncp("reminder", "%1 minute after", "%1 minutes after", -minutes);
    }
    if (minutes % 1440 == 0) {
        return i18ncp("reminder", "%1 day before", "%1 days before", minutes / 1440);
    }
    if (minutes % 60 == 0) {
        return i18ncp("reminder", "%1 hour before", "%1 hours before", minutes / 60);
    }
    return i18ncp("reminder", "%1 minute before", "%1 minutes before", minutes);
}

QVariantMap Application::task(qlonglong id) const
{
    QVariantMap m;
    const Task *t = m_store->task(id);
    m[QStringLiteral("exists")] = t != nullptr;
    if (!t) {
        return m;
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale;
    m[QStringLiteral("id")] = t->id;
    m[QStringLiteral("title")] = t->title;
    m[QStringLiteral("notes")] = t->notes;
    m[QStringLiteral("done")] = t->isDone();
    m[QStringLiteral("priority")] = t->priorityLevel();
    m[QStringLiteral("quadrant")] = t->quadrant;
    m[QStringLiteral("estimatedMinutes")] = t->estimatedMinutes;
    m[QStringLiteral("trackedSeconds")] = t->trackedSeconds;
    m[QStringLiteral("trackedText")] = FocusManager::formatDuration(t->trackedSeconds);
    m[QStringLiteral("pomodorosCompleted")] = t->pomodorosCompleted;
    m[QStringLiteral("percent")] = t->percent;
    m[QStringLiteral("allDay")] = t->allDay;
    m[QStringLiteral("hasDue")] = t->due.isValid();
    m[QStringLiteral("hasStart")] = t->start.isValid();
    if (t->due.isValid()) {
        const QDateTime local = t->allDay ? t->due : t->due.toLocalTime();
        m[QStringLiteral("due")] = local;
        m[QStringLiteral("dueDateText")] = locale.toString(local.date(), QStringLiteral("ddd, d MMM yyyy"));
        m[QStringLiteral("dueTimeText")] = t->allDay ? QString() : locale.toString(local.time(), QLocale::ShortFormat);
        m[QStringLiteral("overdue")] = t->allDay ? t->due.date() < now.date() : t->due < now;
    }
    if (t->start.isValid()) {
        const QDateTime local = t->allDay ? t->start : t->start.toLocalTime();
        m[QStringLiteral("start")] = local;
        m[QStringLiteral("startDateText")] = locale.toString(local.date(), QStringLiteral("ddd, d MMM yyyy"));
    }
    m[QStringLiteral("tags")] = t->tags;
    m[QStringLiteral("rrule")] = t->rrule;
    m[QStringLiteral("recurrenceText")] = describeRecurrence(t->rrule);
    const auto simple = Recurrence::parseSimple(t->rrule);
    const auto rparts = Recurrence::parseRule(t->rrule);
    m[QStringLiteral("recurrenceSimple")] = t->rrule.isEmpty() || simple.valid;
    m[QStringLiteral("recurrenceFreq")] = simple.valid ? (simple.weekdays ? QStringLiteral("WEEKDAYS") : simple.freq) : QString();
    m[QStringLiteral("recurrenceInterval")] = simple.valid ? simple.interval : 1;
    m[QStringLiteral("recurrenceParts")] = QVariantMap{
        {QStringLiteral("valid"), rparts.valid},
        {QStringLiteral("freq"), rparts.freq},
        {QStringLiteral("interval"), rparts.interval},
        {QStringLiteral("byDays"), rparts.byDays},
        {QStringLiteral("endMode"), rparts.endMode},
        {QStringLiteral("count"), rparts.count},
        {QStringLiteral("untilIso"), rparts.until.isValid() ? rparts.until.toString(Qt::ISODate) : QString()},
    };
    QVariantList alarms;
    for (const Alarm &a : t->alarms) {
        alarms.append(describeAlarm(a));
    }
    m[QStringLiteral("reminders")] = alarms;
    m[QStringLiteral("listId")] = t->listId;
    const TaskList *l = m_store->list(t->listId);
    m[QStringLiteral("listName")] = l ? l->name : QString();
    m[QStringLiteral("readOnly")] = l && l->readOnly;
    const Task *parent = m_store->taskByUid(t->listId, t->parentUid);
    m[QStringLiteral("parentId")] = parent ? parent->id : 0;
    m[QStringLiteral("parentTitle")] = parent ? parent->title : QString();
    QVariantList subs;
    QList<const Task *> children = m_store->childrenOf(*t);
    std::sort(children.begin(), children.end(), [](const Task *a, const Task *b) {
        return a->sortOrder != b->sortOrder ? a->sortOrder < b->sortOrder : a->id < b->id;
    });
    for (const Task *c : std::as_const(children)) {
        subs.append(QVariantMap{{QStringLiteral("id"), c->id}, {QStringLiteral("title"), c->title}, {QStringLiteral("done"), c->isDone()}});
    }
    m[QStringLiteral("subtasks")] = subs;
    m[QStringLiteral("createdText")] = locale.toString(t->created.toLocalTime(), QLocale::ShortFormat);
    m[QStringLiteral("modifiedText")] = locale.toString(t->modified.toLocalTime(), QLocale::ShortFormat);
    m[QStringLiteral("completedText")] = t->completedAt.isValid() ? locale.toString(t->completedAt.toLocalTime(), QLocale::ShortFormat) : QString();
    m[QStringLiteral("pendingSync")] = l && l->accountId > 0 && t->syncState != SyncState::Synced;
    return m;
}

bool Application::editTask(qlonglong id, const QString &what, const std::function<void(Task &)> &fn)
{
    const Task *cur = m_store->task(id);
    if (!cur) {
        return false;
    }
    Task t = *cur;
    fn(t);
    m_store->updateTask(t, what);
    return true;
}

void Application::updateTask(qlonglong id, const QVariantMap &changes)
{
    const Task *cur = m_store->task(id);
    if (!cur) {
        return;
    }
    Task t = *cur;
    if (changes.contains(QStringLiteral("title"))) {
        const QString title = changes.value(QStringLiteral("title")).toString().trimmed();
        if (!title.isEmpty()) {
            t.title = title;
        }
    }
    if (changes.contains(QStringLiteral("notes"))) {
        t.notes = changes.value(QStringLiteral("notes")).toString();
    }
    if (changes.contains(QStringLiteral("quadrant"))) {
        const int q = qBound(0, changes.value(QStringLiteral("quadrant")).toInt(), 4);
        t.quadrant = q;
        t.priority = Task::priorityForQuadrant(q);
    } else if (changes.contains(QStringLiteral("priority"))) {
        const int lvl = qBound(0, changes.value(QStringLiteral("priority")).toInt(), 3);
        t.priority = Task::priorityFromLevel(lvl);
        if (lvl == 3) {
            t.quadrant = 1;
        } else if (lvl == 2) {
            t.quadrant = 2;
        } else if (lvl == 1) {
            t.quadrant = EisenhowerManager::isTaskUrgent(t, QDateTime::currentDateTime(), m_settings->eisenhowerUrgencyHours()) ? 3 : 4;
        } else {
            t.quadrant = 0;
        }
    }
    if (changes.contains(QStringLiteral("estimatedMinutes"))) {
        t.estimatedMinutes = qBound(0, changes.value(QStringLiteral("estimatedMinutes")).toInt(), 24 * 60);
    }
    if (changes.contains(QStringLiteral("percent"))) {
        t.percent = qBound(0, changes.value(QStringLiteral("percent")).toInt(), 99);
        t.status = t.percent > 0 ? TaskStatus::InProcess : TaskStatus::NeedsAction;
    }
    if (t.title == cur->title && t.notes == cur->notes && t.priority == cur->priority && t.quadrant == cur->quadrant
        && t.estimatedMinutes == cur->estimatedMinutes && t.percent == cur->percent) {
        return;
    }
    m_store->updateTask(t, i18nc("undo step", "Edit task"));
}

void Application::updateTasks(const QVariantList &ids, const QVariantMap &changes)
{
    const QList<qint64> taskIds = toTaskIdList(ids);
    QList<Task> updated;
    const QDateTime now = QDateTime::currentDateTime();
    const int urgHours = m_settings->eisenhowerUrgencyHours();
    for (qint64 id : taskIds) {
        const Task *cur = m_store->task(id);
        if (!cur) {
            continue;
        }
        const TaskList *l = m_store->list(cur->listId);
        if (l && l->readOnly) {
            continue;
        }
        Task t = *cur;
        if (changes.contains(QStringLiteral("quadrant"))) {
            const int q = qBound(0, changes.value(QStringLiteral("quadrant")).toInt(), 4);
            t.quadrant = q;
            t.priority = Task::priorityForQuadrant(q);
        } else if (changes.contains(QStringLiteral("priority"))) {
            const int lvl = qBound(0, changes.value(QStringLiteral("priority")).toInt(), 3);
            t.priority = Task::priorityFromLevel(lvl);
            t.quadrant = (lvl == 3) ? 1 : (lvl == 2) ? 2 : (lvl == 1) ? (EisenhowerManager::isTaskUrgent(t, now, urgHours) ? 3 : 4) : 0;
        }
        if (t.priority != cur->priority || t.quadrant != cur->quadrant) {
            updated.append(t);
        }
    }
    if (!updated.isEmpty()) {
        m_store->updateTasks(updated, updated.size() > 1 ? i18nc("undo step", "Edit tasks") : i18nc("undo step", "Edit task"));
    }
}

void Application::setDone(qlonglong id, bool done)
{
    const Task *t = m_store->task(id);
    if (!t) {
        return;
    }
    if (done && m_focus->activeTaskId() == id && m_focus->mode() != int(FocusMode::Break)) {
        m_focus->stopAndSave();
    }
    const bool repeats = done && !t->rrule.isEmpty();
    m_store->setCompleted(id, done);
    if (done) {
        const Task *now = m_store->task(id);
        if (repeats && now && !now->isDone()) {
            Q_EMIT message(i18n("Done. Next time: %1", TaskModel::formatDue(*now, QDateTime::currentDateTime())), true);
        } else {
            Q_EMIT message(i18n("Task completed"), true);
        }
    }
}

void Application::setDoneMany(const QVariantList &ids, bool done)
{
    const QList<qint64> taskIds = toTaskIdList(ids);
    if (taskIds.size() <= 1) {
        if (!taskIds.isEmpty()) {
            setDone(taskIds.first(), done);
        }
        return;
    }
    QList<qint64> writable;
    for (qint64 id : taskIds) {
        const Task *t = m_store->task(id);
        const TaskList *l = t ? m_store->list(t->listId) : nullptr;
        if (t && !(l && l->readOnly) && t->isDone() != done) {
            if (done && m_focus->activeTaskId() == id && m_focus->mode() != int(FocusMode::Break)) {
                m_focus->stopAndSave();
            }
            writable.append(id);
        }
    }
    if (writable.isEmpty()) {
        return;
    }
    m_store->setCompletedMany(writable, done);
    Q_EMIT message(done ? i18np("%1 task completed", "%1 tasks completed", writable.size())
                        : i18np("%1 task reopened", "%1 tasks reopened", writable.size()),
                   true);
}

void Application::deleteTask(qlonglong id)
{
    const Task *t = m_store->task(id);
    if (!t) {
        return;
    }
    const int children = m_store->descendantsOf(*t).size();
    m_store->removeTask(id);
    Q_EMIT message(children > 0 ? i18np("Task and %1 subtask deleted", "Task and %1 subtasks deleted", children) : i18n("Task deleted"), true);
}

void Application::deleteTasks(const QVariantList &ids)
{
    const QList<qint64> taskIds = toTaskIdList(ids);
    if (taskIds.size() <= 1) {
        if (!taskIds.isEmpty()) {
            deleteTask(taskIds.first());
        }
        return;
    }
    QList<qint64> writable;
    for (qint64 id : taskIds) {
        const Task *t = m_store->task(id);
        const TaskList *l = t ? m_store->list(t->listId) : nullptr;
        if (t && !(l && l->readOnly)) {
            writable.append(id);
        }
    }
    if (writable.isEmpty()) {
        return;
    }
    m_store->removeTasks(writable, i18nc("undo step", "Delete tasks"));
    Q_EMIT message(i18np("%1 task deleted", "%1 tasks deleted", writable.size()), true);
}

void Application::moveTask(qlonglong id, qlonglong listId)
{
    moveTasks({id}, listId);
}

void Application::moveTasks(const QVariantList &ids, qlonglong listId)
{
    const TaskList *l = m_store->list(listId);
    if (!l) {
        return;
    }
    if (l->readOnly) {
        Q_EMIT message(i18n("\"%1\" is read-only.", l->name), false);
        return;
    }
    const QList<qint64> taskIds = toTaskIdList(ids);
    QList<qint64> movable;
    for (qint64 id : taskIds) {
        const Task *t = m_store->task(id);
        const TaskList *srcList = t ? m_store->list(t->listId) : nullptr;
        if (t && t->listId != listId && !(srcList && srcList->readOnly)) {
            movable.append(id);
        }
    }
    if (movable.isEmpty()) {
        return;
    }
    m_store->moveToListMany(movable, listId);
    Q_EMIT message(movable.size() == 1
                       ? i18n("Moved to \"%1\"", l->name)
                       : i18np("Moved %1 task to \"%2\"", "Moved %1 tasks to \"%2\"", movable.size(), l->name),
                   true);
}

void Application::setDueDate(qlonglong id, const QDateTime &date)
{
    const QDate d = date.toLocalTime().date();
    if (!d.isValid()) {
        return;
    }
    editTask(id, i18nc("undo step", "Change due date"), [&](Task &t) {
        if (t.due.isValid() && !t.allDay) {
            t.due = combine(d, t.due.toLocalTime().time());
        } else {
            if (!t.due.isValid() && !t.start.isValid()) {
                t.allDay = true;
            }
            t.due = t.allDay ? QDateTime(d, QTime(0, 0)) : combine(d, QTime(9, 0));
        }
    });
}

void Application::clearDue(qlonglong id)
{
    editTask(id, i18nc("undo step", "Remove due date"), [](Task &t) {
        t.due = QDateTime();
        t.rrule.clear();
        if (!t.start.isValid()) {
            t.allDay = true;
        }
    });
}

void Application::setDueTime(qlonglong id, const QDateTime &time)
{
    const QTime tm = time.toLocalTime().time();
    if (!tm.isValid()) {
        return;
    }
    editTask(id, i18nc("undo step", "Change due time"), [&](Task &t) {
        const QDate d = t.due.isValid() ? localDate(t, t.due) : QDate::currentDate();
        const QDate s = t.start.isValid() ? localDate(t, t.start) : QDate();
        t.allDay = false;
        t.due = combine(d, tm);
        if (s.isValid()) {
            t.start = combine(s, s == d ? tm : QTime(0, 0));
        }
    });
}

void Application::clearDueTime(qlonglong id)
{
    editTask(id, i18nc("undo step", "Remove due time"), [](Task &t) {
        if (t.allDay) {
            return;
        }
        const QDate d = t.due.isValid() ? t.due.toLocalTime().date() : QDate();
        const QDate s = t.start.isValid() ? t.start.toLocalTime().date() : QDate();
        t.allDay = true;
        t.due = d.isValid() ? QDateTime(d, QTime(0, 0)) : QDateTime();
        t.start = s.isValid() ? QDateTime(s, QTime(0, 0)) : QDateTime();
    });
}

void Application::setStartDate(qlonglong id, const QDateTime &date)
{
    const QDate d = date.toLocalTime().date();
    if (!d.isValid()) {
        return;
    }
    editTask(id, i18nc("undo step", "Change start date"), [&](Task &t) {
        t.start = t.allDay ? QDateTime(d, QTime(0, 0)) : combine(d, QTime(0, 0));
        if (t.due.isValid() && t.start > t.due) {
            t.due = t.allDay ? t.start : combine(d, t.due.toLocalTime().time());
        }
    });
}

void Application::clearStart(qlonglong id)
{
    editTask(id, i18nc("undo step", "Remove start date"), [](Task &t) {
        if (t.rrule.isEmpty()) {
            t.start = QDateTime();
        }
    });
}

void Application::postpone(qlonglong id, int days)
{
    postponeMany({id}, days);
}

void Application::postponeMany(const QVariantList &ids, int days)
{
    const QList<qint64> taskIds = toTaskIdList(ids);
    const QDate today = QDate::currentDate();
    QList<Task> updated;
    for (qint64 id : taskIds) {
        const Task *cur = m_store->task(id);
        const TaskList *l = cur ? m_store->list(cur->listId) : nullptr;
        if (!cur || (l && l->readOnly)) {
            continue;
        }
        Task t = *cur;
        if (!t.due.isValid()) {
            t.allDay = true;
            t.due = QDateTime(today.addDays(days), QTime(0, 0));
        } else {
            // Postponing an overdue task counts from today, not from the old date.
            const QDate from = qMax(localDate(t, t.due), days > 0 ? today : localDate(t, t.due));
            const QDate target = days == 0 ? today : from.addDays(days);
            const qint64 shift = localDate(t, t.due).daysTo(target);
            t.due = t.allDay ? QDateTime(target, QTime(0, 0)) : combine(target, t.due.toLocalTime().time());
            if (t.start.isValid()) {
                const QDate s = localDate(t, t.start).addDays(shift);
                t.start = t.allDay ? QDateTime(s, QTime(0, 0)) : combine(s, t.start.toLocalTime().time());
            }
        }
        updated.append(t);
    }
    if (!updated.isEmpty()) {
        m_store->updateTasks(updated, updated.size() > 1 ? i18nc("undo step", "Postpone tasks") : i18nc("undo step", "Postpone task"));
    }
}

void Application::addTag(qlonglong id, const QString &tag)
{
    QString clean = tag.trimmed();
    while (clean.startsWith(QLatin1Char('#'))) {
        clean.remove(0, 1);
    }
    if (clean.isEmpty()) {
        return;
    }
    editTask(id, i18nc("undo step", "Add tag"), [&](Task &t) {
        if (!t.tags.contains(clean, Qt::CaseInsensitive)) {
            t.tags.append(clean);
        }
    });
}

void Application::removeTag(qlonglong id, const QString &tag)
{
    editTask(id, i18nc("undo step", "Remove tag"), [&](Task &t) {
        t.tags.removeAll(tag);
    });
}

void Application::addReminder(qlonglong id, int minutesBefore)
{
    editTask(id, i18nc("undo step", "Add reminder"), [&](Task &t) {
        if (!t.due.isValid() && !t.start.isValid()) {
            // A reminder needs a moment to refer to.
            t.allDay = true;
            t.due = QDateTime(QDate::currentDate(), QTime(0, 0));
        }
        const Alarm a{Alarm::RelativeToDue, {}, -minutesBefore * 60};
        if (!t.alarms.contains(a)) {
            t.alarms.append(a);
        }
    });
}

void Application::removeReminder(qlonglong id, int index)
{
    editTask(id, i18nc("undo step", "Remove reminder"), [&](Task &t) {
        if (index >= 0 && index < t.alarms.size()) {
            t.alarms.removeAt(index);
        }
    });
}

void Application::setRecurrence(qlonglong id, const QString &freq, int interval, bool weekdays)
{
    editTask(id, i18nc("undo step", "Change repetition"), [&](Task &t) {
        t.rrule = Recurrence::buildSimple(freq, qMax(1, interval), weekdays);
    });
}

void Application::setRecurrenceRule(qlonglong id, const QVariantMap &parts)
{
    Recurrence::RuleParts rp;
    rp.freq = parts.value(QStringLiteral("freq")).toString().trimmed().toUpper();
    rp.interval = qMax(1, parts.value(QStringLiteral("interval"), 1).toInt());
    rp.byDays = parts.value(QStringLiteral("byDays")).toStringList();
    rp.endMode = parts.value(QStringLiteral("endMode"), QStringLiteral("never")).toString();
    rp.count = qMax(1, parts.value(QStringLiteral("count"), 5).toInt());
    const QString untilIso = parts.value(QStringLiteral("untilIso")).toString();
    rp.until = untilIso.isEmpty() ? QDate() : QDate::fromString(untilIso, Qt::ISODate);
    const QString built = Recurrence::buildRule(rp);
    editTask(id, i18nc("undo step", "Change repetition"), [&](Task &t) {
        t.rrule = built;
        if (!t.rrule.isEmpty() && !t.due.isValid() && !t.start.isValid()) {
            t.allDay = true;
            t.due = QDateTime(QDate::currentDate(), QTime(0, 0));
            t.start = t.due;
        }
    });
}

QStringList Application::allTags() const
{
    return m_store->allTags();
}

QVariantList Application::openTasks() const
{
    QVariantList out;
    for (const Task &t : m_store->tasks()) {
        if (!t.isDone()) {
            out.append(QVariantMap{{QStringLiteral("id"), t.id}, {QStringLiteral("title"), t.title}});
        }
    }
    return out;
}

int Application::deleteCompleted(qlonglong listId)
{
    const int n = m_store->removeCompleted(listId);
    if (n > 0) {
        Q_EMIT message(i18np("%1 completed task deleted", "%1 completed tasks deleted", n), true);
    }
    return n;
}

// ---- lists ----

QVariantList Application::lists() const
{
    QVariantList out;
    for (const TaskList &l : m_store->lists()) {
        const Account *a = m_store->account(l.accountId);
        out.append(QVariantMap{{QStringLiteral("id"), l.id},
                               {QStringLiteral("name"), l.name},
                               {QStringLiteral("color"), l.color},
                               {QStringLiteral("readOnly"), l.readOnly},
                               {QStringLiteral("account"), a ? a->name : QString()}});
    }
    return out;
}

QVariantMap Application::listInfo(qlonglong id) const
{
    const TaskList *l = m_store->list(id);
    if (!l) {
        return {};
    }
    int total = 0;
    for (const Task &t : m_store->tasks()) {
        total += t.listId == id;
    }
    const Account *a = m_store->account(l->accountId);
    return {{QStringLiteral("id"), l->id},
            {QStringLiteral("name"), l->name},
            {QStringLiteral("color"), l->color},
            {QStringLiteral("readOnly"), l->readOnly},
            {QStringLiteral("accountId"), l->accountId},
            {QStringLiteral("account"), a ? a->name : QString()},
            {QStringLiteral("taskCount"), total}};
}

qlonglong Application::addList(const QString &name, const QString &color, qlonglong accountId)
{
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    return m_store->addList(name, color, accountId);
}

void Application::updateList(qlonglong id, const QString &name, const QString &color)
{
    if (!name.trimmed().isEmpty()) {
        m_store->updateList(id, name, color);
    }
}

bool Application::moveListToAccount(qlonglong id, qlonglong accountId)
{
    return m_store->moveListToAccount(id, accountId);
}

void Application::removeList(qlonglong id)
{
    m_store->removeList(id);
}

// ---- misc ----

void Application::undo()
{
    if (m_store->canUndo()) {
        const QString text = m_store->undoText();
        m_store->undo();
        Q_EMIT message(i18nc("%1 is the action that was undone", "Undone: %1", text), false);
    }
}

void Application::redo()
{
    if (m_store->canRedo()) {
        const QString text = m_store->redoText();
        m_store->redo();
        Q_EMIT message(i18nc("%1 is the action that was redone", "Redone: %1", text), false);
    }
}

void Application::trimMemory(QObject *contextObj)
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (QQmlEngine *eng = contextObj ? qmlEngine(contextObj) : nullptr) {
        eng->collectGarbage();
        eng->trimComponentCache();
    }
}

int Application::exportIcs(const QUrl &file, qlonglong listId)
{
    QList<Task> tasks;
    for (const Task &t : m_store->tasks()) {
        if (listId <= 0 || t.listId == listId) {
            Task copy = t;
            copy.baseIcs.clear(); // export our view of the task, not the server's bytes
            tasks.append(copy);
        }
    }
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT message(i18n("Could not write %1: %2", file.toLocalFile(), f.errorString()), false);
        return -1;
    }
    f.write(IcsCodec::exportCalendar(tasks).toUtf8());
    Q_EMIT message(i18np("%1 task exported", "%1 tasks exported", tasks.size()), false);
    return tasks.size();
}

int Application::importIcs(const QUrl &file, qlonglong listId)
{
    QFile f(file.toLocalFile());
    if (!f.open(QIODevice::ReadOnly)) {
        Q_EMIT message(i18n("Could not read %1: %2", file.toLocalFile(), f.errorString()), false);
        return -1;
    }
    const QList<Task> tasks = IcsCodec::decodeAll(QString::fromUtf8(f.readAll()));
    if (tasks.isEmpty()) {
        Q_EMIT message(i18n("No tasks found in that file."), false);
        return 0;
    }
    const int n = m_store->importTasks(tasks, listId > 0 ? listId : (m_tasks->isListView() ? m_tasks->listId() : m_settings->defaultListId()));
    Q_EMIT message(i18np("%1 task imported", "%1 tasks imported", n), true);
    return n;
}
