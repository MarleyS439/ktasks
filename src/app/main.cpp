// SPDX-License-Identifier: GPL-3.0-or-later
#include "application.h"
#include "database.h"
#include "logging.h"

#include <KAboutData>
#include <KColorSchemeManager>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>
#ifdef KTASKS_HAVE_CRASH
#include <KCrash>
#endif
#ifdef KTASKS_HAVE_DBUS
#include <KDBusService>
#else
#include <QLocalServer>
#include <QLocalSocket>
#endif
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <qqml.h>

#ifndef KTASKS_VERSION
#define KTASKS_VERSION "1.0.0"
#endif

// A Windows GUI program has no console, so warnings would be lost. Keep the
// messages of the current and the previous session next to the database, where
// a user can find them and attach them to a bug report. KTASKS_LOG_FILE forces
// a log file on any platform.
static QFile *s_logFile = nullptr;
static QtMessageHandler s_previousHandler = nullptr;

static void fileMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (s_logFile && s_logFile->isOpen()) {
        static const char *const names[] = {"debug", "warning", "critical", "fatal", "info"};
        const int idx = (type >= QtDebugMsg && type <= QtInfoMsg) ? int(type) : 0;
        const QByteArray line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs).toUtf8() + ' ' + names[idx] + ' '
            + (context.category ? QByteArray(context.category) + ": " : QByteArray()) + msg.toUtf8() + '\n';
        s_logFile->write(line);
        s_logFile->flush();
    }
    if (s_previousHandler) {
        s_previousHandler(type, context, msg);
    }
}

static void setupFileLogging(const QString &dataDir)
{
    QString path = qEnvironmentVariable("KTASKS_LOG_FILE");
#ifdef Q_OS_WIN
    if (path.isEmpty()) {
        path = dataDir + QStringLiteral("/ktasks.log");
    }
#else
    Q_UNUSED(dataDir)
#endif
    if (path.isEmpty()) {
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString old = path + QStringLiteral(".old");
    if (QFileInfo::exists(path)) {
        QFile::remove(old);
        QFile::rename(path, old);
    }
    s_logFile = new QFile(path);
    if (!s_logFile->open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        delete s_logFile;
        s_logFile = nullptr;
        return;
    }
    s_previousHandler = qInstallMessageHandler(fileMessageHandler);
}

static void migrateLegacyAfazerData(const QString &newDataDir)
{
    const QString newDb = newDataDir + QStringLiteral("/ktasks.db");
    if (!QFileInfo::exists(newDb)) {
        const QString legacyDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/afazer");
        const QString legacyDb = legacyDir + QStringLiteral("/afazer.db");
        if (QFileInfo::exists(legacyDb)) {
            QDir().mkpath(newDataDir);
            QFile::copy(legacyDb, newDb);
            for (const QString &suf : {QStringLiteral("-wal"), QStringLiteral("-shm")}) {
                if (QFileInfo::exists(legacyDb + suf)) {
                    QFile::copy(legacyDb + suf, newDb + suf);
                }
            }
            const QDir legacyBackups(legacyDir + QStringLiteral("/backups"));
            if (legacyBackups.exists()) {
                const QString newBackups = newDataDir + QStringLiteral("/backups");
                QDir().mkpath(newBackups);
                for (const QFileInfo &fi : legacyBackups.entryInfoList(QDir::Files | QDir::NoDotAndDotDot)) {
                    const QString dest = newBackups + QLatin1Char('/') + fi.fileName();
                    if (!QFileInfo::exists(dest)) {
                        QFile::copy(fi.absoluteFilePath(), dest);
                    }
                }
            }
        }
    }

    const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString legacyCfg = cfgDir + QStringLiteral("/afazerrc");
    const QString newCfg = cfgDir + QStringLiteral("/ktasksrc");
    if (!QFileInfo::exists(newCfg) && QFileInfo::exists(legacyCfg)) {
        QDir().mkpath(cfgDir);
        QFile::copy(legacyCfg, newCfg);
    }
}

int main(int argc, char *argv[])
{
#ifdef Q_OS_WIN
    KIconTheme::initTheme();
#endif
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("ktasks");

    KAboutData about(QStringLiteral("ktasks"),
                     i18n("KTasks"),
                     QStringLiteral(KTASKS_VERSION),
                     i18n("Tasks, routines, and journal for Plasma — local or synchronized with Nextcloud"),
                     KAboutLicense::GPL_V3,
                     i18n("© 2026 MarleyS439"));
    about.addAuthor(i18n("MarleyS439"), i18n("Author & Maintainer"), QString(), QStringLiteral("https://github.com/MarleyS439"));
    about.setOrganizationDomain("marleys439.github.io");
    about.setDesktopFileName(QStringLiteral("io.github.marleys439.ktasks"));
    KAboutData::setApplicationData(about);
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.marleys439.ktasks"), QIcon::fromTheme(QStringLiteral("view-task"))));
    QGuiApplication::setQuitOnLastWindowClosed(false);
#ifdef KTASKS_HAVE_CRASH
    KCrash::initialize();
#endif

    QCommandLineParser parser;
    about.setupCommandLine(&parser);
    parser.addOption(QCommandLineOption(QStringLiteral("hidden"), i18n("Start in the system tray without showing the window.")));
    parser.addOption(QCommandLineOption(QStringLiteral("quick-add"), i18n("Open the quick entry window.")));
    parser.addOption(QCommandLineOption(QStringLiteral("add"), i18n("Add a task, e.g. --add \"Buy milk tomorrow 9am #home\"."), QStringLiteral("text")));
    parser.process(app);
    about.processCommandLine(&parser);

    // One instance only: a second launch hands its arguments to the first.
    const bool isolated = qEnvironmentVariableIsSet("KTASKS_DATA_DIR");
#ifdef KTASKS_HAVE_DBUS
    std::unique_ptr<KDBusService> service;
    if (!isolated) {
        service = std::make_unique<KDBusService>(KDBusService::Unique);
    }
#else
    std::unique_ptr<QLocalServer> localServer;
    const QString singleInstanceKey = QStringLiteral("ktasks-single-instance");
    if (!isolated) {
        QLocalSocket probe;
        probe.connectToServer(singleInstanceKey, QIODevice::WriteOnly);
        if (probe.waitForConnected(250)) {
            const QByteArray payload = app.arguments().join(QChar(0x1e)).toUtf8();
            probe.write(payload);
            probe.flush();
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
            return 0;
        }
        QLocalServer::removeServer(singleInstanceKey);
        localServer = std::make_unique<QLocalServer>();
        localServer->listen(singleInstanceKey);
    }
#endif

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }
    // Follows the system color scheme and restores the one chosen in the settings.
    KColorSchemeManager::instance();

    const QString dataDir = isolated ? qEnvironmentVariable("KTASKS_DATA_DIR") : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    setupFileLogging(dataDir);
    qCInfo(lcApp) << "KTasks" << KTASKS_VERSION << "starting; data directory:" << QDir::toNativeSeparators(dataDir);
    if (!isolated) {
        migrateLegacyAfazerData(dataDir);
    }
    KTasks::Database db(dataDir + QStringLiteral("/ktasks.db"));
    QString error;
    if (!db.open(&error)) {
        qCCritical(lcApp) << "cannot open the database:" << error;
        QMessageBox::critical(nullptr, i18n("KTasks"), i18n("The task database could not be opened:\n%1\n\nYour data was not changed.", error));
        return 1;
    }

    Application application(&db);
    application.handleArguments(app.arguments(), true);
#ifdef KTASKS_HAVE_DBUS
    if (service) {
        QObject::connect(service.get(), &KDBusService::activateRequested, &application, [&application](const QStringList &arguments, const QString &) {
            application.handleArguments(arguments, false);
        });
    }
#else
    if (localServer) {
        QObject::connect(localServer.get(), &QLocalServer::newConnection, &application, [&localServer, &application] {
            while (QLocalSocket *sock = localServer->nextPendingConnection()) {
                QObject::connect(sock, &QLocalSocket::readyRead, &application, [sock, &application] {
                    const QByteArray raw = sock->readAll();
                    const QString decoded = QString::fromUtf8(raw);
                    const QStringList args = decoded.isEmpty() ? QStringList() : decoded.split(QChar(0x1e));
                    application.handleArguments(args, false);
                });
                QObject::connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
    }
#endif

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    qmlRegisterSingletonInstance("app.ktasks.backend", 1, 0, "App", &application);
    qmlRegisterUncreatableType<TaskModel>("app.ktasks.backend", 1, 0, "TaskModel", QStringLiteral("enums only"));
    engine.loadFromModule("app.ktasks", "Main");
    if (engine.rootObjects().isEmpty()) {
        qCCritical(lcApp) << "the user interface could not be loaded";
        return 1;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
    application.setWindow(window);
    if (window && !application.startHidden()) {
        window->show();
    }

    // Development aids: open a task or the settings, save a picture of the window and quit.
    if (qEnvironmentVariableIntValue("KTASKS_SELECT") > 0) {
        QTimer::singleShot(500, &application, [&application] {
            Q_EMIT application.showTask(qEnvironmentVariableIntValue("KTASKS_SELECT"));
        });
    }
    const QString showTarget = qEnvironmentVariable("KTASKS_SHOW");
    if (showTarget == QLatin1String("settings")) {
        QTimer::singleShot(500, &application, &Application::showSettings);
    } else if (!showTarget.isEmpty()) {
        QTimer::singleShot(300, &application, [&application, showTarget] {
            Q_EMIT application.showSection(showTarget);
        });
    }
    const QString shot = qEnvironmentVariable("KTASKS_SCREENSHOT");
    if (!shot.isEmpty() && window) {
        QTimer::singleShot(qEnvironmentVariableIntValue("KTASKS_SCREENSHOT_DELAY") > 0 ? qEnvironmentVariableIntValue("KTASKS_SCREENSHOT_DELAY") : 1500, window, [window, shot] {
            window->grabWindow().save(shot);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
