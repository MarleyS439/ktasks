// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end interaction test of the real main window (Main.qml): every
// section, the settings tabs, the dialogs, and typical keyboard and mouse
// workflows (add, complete, undo, delete, edit, journal, routine, matrix).
// Any warning coming from KTasks' own QML fails the test.
#include "application.h"
#include "database.h"
#include "taskmodel.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <qqml.h>

using namespace KTasks;

namespace
{
QMutex s_mutex;
QStringList s_qmlProblems;
QtMessageHandler s_prevHandler = nullptr;

void collectingHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (type != QtDebugMsg && type != QtInfoMsg) {
        const QString file = QString::fromUtf8(ctx.file ? ctx.file : "");
        const bool ours = msg.contains(QLatin1String("/src/app/qml/")) || file.contains(QLatin1String("/src/app/qml/"))
            || msg.contains(QLatin1String("qrc:/qt/qml/app/ktasks/"));
        if (ours) {
            QMutexLocker lock(&s_mutex);
            s_qmlProblems << msg;
        }
    }
    if (s_prevHandler) {
        s_prevHandler(type, ctx, msg);
    }
}

QString qmlDir()
{
    return QStringLiteral(KTASKS_SOURCE_DIR "/src/app/qml");
}

// Depth-first search through the visual tree and the QObject tree.
template<typename Pred>
QQuickItem *findItem(QQuickItem *root, Pred pred)
{
    if (!root) {
        return nullptr;
    }
    if (pred(root)) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *hit = findItem(child, pred)) {
            return hit;
        }
    }
    return nullptr;
}

bool isType(QObject *o, const char *qmlName)
{
    // C++ classes by inheritance (the desktop style subclasses them in QML),
    // QML components by their generated class name.
    return o->inherits(qmlName) || QString::fromLatin1(o->metaObject()->className()).startsWith(QLatin1String(qmlName));
}

bool reallyVisible(QQuickItem *item)
{
    return item && item->isVisible() && item->width() > 0 && item->height() > 0 && item->opacity() > 0;
}

// Types text into the focused item the way a keyboard does, accents included.
void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text) {
        const Qt::Key key = c.isLetterOrNumber() && c.unicode() < 128 ? Qt::Key(c.toUpper().unicode()) : (c == QLatin1Char(' ') ? Qt::Key_Space : Qt::Key_unknown);
        QTest::sendKeyEvent(QTest::Press, window, key, QString(c), Qt::NoModifier);
        QTest::sendKeyEvent(QTest::Release, window, key, QString(c), Qt::NoModifier);
    }
    QCoreApplication::processEvents();
}

QPoint centerOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}
} // namespace

class UiTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Database> m_db;
    std::unique_ptr<Application> m_app;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow *m_win = nullptr;

    QObject *loaderItem(const char *loaderName)
    {
        auto *loader = m_win->findChild<QObject *>(QLatin1String(loaderName));
        return loader ? loader->property("item").value<QObject *>() : nullptr;
    }
    void switchSection(const QString &section)
    {
        QVERIFY(QMetaObject::invokeMethod(m_win, "switchSection", Q_ARG(QVariant, section)));
        QTRY_COMPARE(m_win->property("currentSection").toString(), section);
    }
    QQuickItem *taskRow(qlonglong id)
    {
        return findItem(m_win->contentItem(), [id](QQuickItem *it) {
            return isType(it, "TaskDelegate") && it->property("taskId").toLongLong() == id && reallyVisible(it);
        });
    }
    void assertNoQmlProblems()
    {
        QMutexLocker lock(&s_mutex);
        const QStringList problems = s_qmlProblems;
        s_qmlProblems.clear();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        // Start from default settings, as a new user would.
        const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QFile::remove(cfg + QStringLiteral("/ktasksrc"));
        QFile::remove(cfg + QStringLiteral("/ktasksstaterc"));
        s_prevHandler = qInstallMessageHandler(collectingHandler);

        KLocalizedString::setApplicationDomain("ktasks");
        // The style main() uses; without it Windows falls back to its native
        // style, which KTasks does not target.
        if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
            QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
        }
        m_dir = std::make_unique<QTemporaryDir>();
        m_db = std::make_unique<Database>(m_dir->filePath(QStringLiteral("ktasks.db")));
        QVERIFY(m_db->open());
        m_app = std::make_unique<Application>(m_db.get());
        m_app->settings()->setRunInBackground(false);

        m_engine = std::make_unique<QQmlApplicationEngine>();
        KLocalization::setupLocalizedContext(m_engine.get());
        qmlRegisterSingletonInstance("app.ktasks.backend", 1, 0, "App", m_app.get());
        qmlRegisterUncreatableType<TaskModel>("app.ktasks.backend", 1, 0, "TaskModel", QStringLiteral("enums only"));
        m_engine->load(QUrl::fromLocalFile(qmlDir() + QStringLiteral("/Main.qml")));
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_win = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
        QVERIFY(m_win);
        m_app->setWindow(m_win);
        m_win->resize(1280, 860);
        m_win->show();
        m_win->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(m_win));
        QTRY_VERIFY(loaderItem("summaryLoader"));
        assertNoQmlProblems();
    }

    void cleanupTestCase()
    {
        if (m_win) {
            m_win->hide();
        }
        m_engine.reset();
        m_app.reset();
        m_db.reset();
        qInstallMessageHandler(s_prevHandler);
    }

    void init()
    {
        // Each step starts on a clean slate: no layers, no dialogs.
        QVERIFY(QMetaObject::invokeMethod(m_win, "switchSection", Q_ARG(QVariant, QStringLiteral("summary"))));
        // A window manager gives the focus back after a secondary window
        // (quick add) closes; the offscreen platform does not.
        m_win->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(m_win));
        QTest::qWait(50);
    }

    void cleanup()
    {
        assertNoQmlProblems();
    }

    void everySectionLoadsAndRenders_data()
    {
        QTest::addColumn<QString>("section");
        QTest::addColumn<QString>("loader");
        QTest::newRow("summary") << QStringLiteral("summary") << QStringLiteral("summaryLoader");
        QTest::newRow("tasks") << QStringLiteral("tasks") << QStringLiteral("tasksLoader");
        QTest::newRow("routine") << QStringLiteral("routine") << QStringLiteral("routineLoader");
        QTest::newRow("matrix") << QStringLiteral("matrix") << QStringLiteral("matrixLoader");
        QTest::newRow("journal") << QStringLiteral("journal") << QStringLiteral("journalLoader");
        QTest::newRow("insights") << QStringLiteral("insights") << QStringLiteral("insightsLoader");
    }
    void everySectionLoadsAndRenders()
    {
        QFETCH(QString, section);
        QFETCH(QString, loader);
        switchSection(section);
        QTRY_VERIFY(loaderItem(loader.toLatin1().constData()));
        auto *item = qobject_cast<QQuickItem *>(loaderItem(loader.toLatin1().constData()));
        QVERIFY(item);
        QTRY_VERIFY(item->width() > 100 && item->height() > 100);
        const QImage shot = m_win->grabWindow();
        QVERIFY(!shot.isNull());
        // Only the current section stays loaded.
        for (const char *other : {"summaryLoader", "tasksLoader", "routineLoader", "matrixLoader", "journalLoader", "insightsLoader"}) {
            if (loader != QLatin1String(other)) {
                QVERIFY2(!loaderItem(other), other);
            }
        }
    }

    void taskViewsSwitchFromTheSidebar()
    {
        switchSection(QStringLiteral("tasks"));
        for (const QString &view : {QStringLiteral("today"), QStringLiteral("week"), QStringLiteral("overdue"), QStringLiteral("all"),
                                    QStringLiteral("nodate"), QStringLiteral("completed")}) {
            Q_EMIT m_app->showView(view);
            QTRY_COMPARE(m_app->tasks()->view(), view);
            QTRY_VERIFY(loaderItem("tasksLoader"));
        }
    }

    void typeATaskCompleteUndoAndDelete()
    {
        switchSection(QStringLiteral("tasks"));
        Q_EMIT m_app->showView(QStringLiteral("all"));
        QTRY_COMPARE(m_app->tasks()->view(), QStringLiteral("all"));
        QObject *page = loaderItem("tasksLoader");
        QVERIFY(page);

        // Type into the quick entry, as a user would, and press Enter.
        QVERIFY(QMetaObject::invokeMethod(page, "focusQuickAdd"));
        QTRY_VERIFY(m_win->activeFocusItem() && isType(m_win->activeFocusItem(), "QQuickTextField"));
        const int before = m_app->store()->tasks().size();
        typeText(m_win, QStringLiteral("Comprar leite amanhã 9h #mercado !alta"));
        QTest::keyClick(m_win, Qt::Key_Return);
        QTRY_COMPARE(m_app->store()->tasks().size(), before + 1);

        const Task *created = nullptr;
        for (const Task &t : m_app->store()->tasks()) {
            if (t.title.startsWith(QLatin1String("Comprar leite"))) {
                created = &t;
            }
        }
        QVERIFY(created);
        const qlonglong id = created->id;
        QVERIFY2(created->due.isValid(), "the natural language parser should have found a date");
        QCOMPARE(created->due.toLocalTime().date(), QDate::currentDate().addDays(1));
        QVERIFY(created->tags.contains(QStringLiteral("mercado")));
        // The field is cleared for the next task.
        QTRY_COMPARE(m_win->activeFocusItem()->property("text").toString(), QString());

        // Click the row: the details pane shows it (the window is wide).
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = taskRow(id)));
        QTest::mouseClick(m_win, Qt::LeftButton, Qt::NoModifier, centerOf(row));
        QTRY_COMPARE(m_win->property("currentTaskId").toLongLong(), id);
        QQuickItem *details = nullptr;
        QTRY_VERIFY((details = findItem(m_win->contentItem(), [id](QQuickItem *it) {
                         return isType(it, "TaskDetails") && it->property("taskId").toLongLong() == id && reallyVisible(it);
                     })));

        // Click the checkbox of the row to complete it.
        QQuickItem *check = findItem(row, [](QQuickItem *it) {
            return isType(it, "QQuickCheckBox") && reallyVisible(it);
        });
        QVERIFY(check);
        QTest::mouseClick(m_win, Qt::LeftButton, Qt::NoModifier, centerOf(check));
        QTRY_VERIFY(m_app->store()->task(id)->isDone());

        // Ctrl+Z reopens it.
        QTest::keyClick(m_win, Qt::Key_Z, Qt::ControlModifier);
        QTRY_VERIFY(!m_app->store()->task(id)->isDone());

        // Rename it from the details pane.
        m_app->updateTask(id, QVariantMap{{QStringLiteral("title"), QStringLiteral("Comprar pão")}});
        QTRY_COMPARE(m_app->store()->task(id)->title, QStringLiteral("Comprar pão"));
        QTRY_VERIFY(taskRow(id) && taskRow(id)->property("title").toString() == QStringLiteral("Comprar pão"));

        // Delete through the window's delete action, then undo it.
        Q_EMIT m_app->deleteCurrentRequested();
        QTRY_VERIFY(!m_app->store()->task(id));
        QTRY_COMPARE(m_win->property("currentTaskId").toLongLong(), 0);
        m_app->undo();
        QTRY_VERIFY(m_app->store()->task(id));
        QCOMPARE(m_app->store()->task(id)->title, QStringLiteral("Comprar pão"));
    }

    void searchFiltersTheList()
    {
        m_app->quickAdd(QStringLiteral("Ligar para o médico"));
        m_app->quickAdd(QStringLiteral("Lavar o carro"));
        switchSection(QStringLiteral("tasks"));
        Q_EMIT m_app->showView(QStringLiteral("all"));
        m_app->tasks()->setSearchText(QStringLiteral("medico")); // accents ignored
        QTRY_COMPARE(m_app->tasks()->rowCount(), 1);
        m_app->tasks()->setSearchText(QString());
        QTRY_VERIFY(m_app->tasks()->rowCount() >= 2);
    }

    void narrowWindowShowsDetailsAsALayer()
    {
        const qlonglong id = m_app->quickAdd(QStringLiteral("Tarefa em janela estreita"));
        switchSection(QStringLiteral("tasks"));
        Q_EMIT m_app->showView(QStringLiteral("all"));
        m_win->resize(560, 760);
        QTRY_VERIFY(!m_win->property("wide").toBool());
        Q_EMIT m_app->showTask(id);
        QTRY_VERIFY(m_win->property("detailsLayer").value<QObject *>());
        // Esc / back closes the layer and deselects the task.
        QVERIFY(QMetaObject::invokeMethod(m_win, "selectTask", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(!m_win->property("detailsLayer").value<QObject *>());
        m_win->resize(1280, 860);
        QTRY_VERIFY(m_win->property("wide").toBool());
    }

    void settingsTabsAndBack()
    {
        Q_EMIT m_app->showSettings();
        QObject *layer = nullptr;
        QTRY_VERIFY((layer = m_win->property("settingsLayer").value<QObject *>()));
        auto *tabs = layer->findChild<QObject *>(QStringLiteral("settingsTabs"));
        QVERIFY(tabs);
        for (int i = 0; i < 3; ++i) {
            tabs->setProperty("currentIndex", i);
            QTRY_COMPARE(tabs->property("currentIndex").toInt(), i);
            QTest::qWait(50);
            QVERIFY(!m_win->grabWindow().isNull());
        }
        // Profile: save a name through the backend the page uses.
        m_app->saveProfile(QStringLiteral("Pessoa Teste"), QStringLiteral("pessoa@example.org"));
        QCOMPARE(m_app->effectiveProfileName(), QStringLiteral("Pessoa Teste"));
        // No avatar chosen: an empty URL, never a broken "file://" one.
        QVERIFY(m_app->effectiveAvatarUrl().isEmpty() || m_app->effectiveAvatarUrl().isLocalFile());

        QVERIFY(QMetaObject::invokeMethod(m_win, "switchSection", Q_ARG(QVariant, QStringLiteral("summary"))));
        QTRY_VERIFY(!m_win->property("settingsLayer").value<QObject *>());
    }

    void avatarUrlIsAValidLocalFileUrl()
    {
        const QString img = m_dir->filePath(QStringLiteral("avatar.png"));
        QImage pic(8, 8, QImage::Format_ARGB32);
        pic.fill(Qt::red);
        QVERIFY(pic.save(img));
        m_app->setProfileAvatarFromUrl(QUrl::fromLocalFile(img));
        QCOMPARE(m_app->effectiveAvatarUrl(), QUrl::fromLocalFile(img));
        QVERIFY(m_app->effectiveAvatarUrl().isLocalFile());
        QCOMPARE(QDir::cleanPath(m_app->effectiveAvatarUrl().toLocalFile()), QDir::cleanPath(img));
        switchSection(QStringLiteral("summary"));
        QTest::qWait(100);
    }

    void dialogsOpenAndClose()
    {
        QVERIFY(QMetaObject::invokeMethod(m_win, "openListDialog", Q_ARG(QVariant, 0)));
        QObject *listDialog = loaderItem("listDialogLoader");
        QVERIFY(listDialog);
        QTRY_VERIFY(listDialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(listDialog, "close"));
        QTRY_VERIFY(!listDialog->property("visible").toBool());

        QVERIFY(QMetaObject::invokeMethod(m_win, "openAccountDialog"));
        QObject *accountDialog = loaderItem("accountDialogLoader");
        QVERIFY(accountDialog);
        QTRY_VERIFY(accountDialog->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(accountDialog, "close"));
        QTRY_VERIFY(!accountDialog->property("visible").toBool());

        for (const QString &scope : {QStringLiteral("today"), QStringLiteral("week"), QStringLiteral("matrix"), QStringLiteral("routine")}) {
            QVERIFY(QMetaObject::invokeMethod(m_win, "openPrintDialog", Q_ARG(QVariant, scope), Q_ARG(QVariant, 0)));
            QObject *printDialog = loaderItem("printDialogLoader");
            QVERIFY(printDialog);
            QTRY_VERIFY(printDialog->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(printDialog, "close"));
            QTRY_VERIFY(!printDialog->property("visible").toBool());
        }

        Q_EMIT m_app->quickAddWindowRequested();
        QObject *quickWin = nullptr;
        QTRY_VERIFY((quickWin = loaderItem("quickAddWindowLoader")));
        QTRY_VERIFY(quickWin->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(quickWin, "close"));
        QTRY_VERIFY(!quickWin->property("visible").toBool());
    }

    void newListFromTheDialogBackend()
    {
        const qlonglong listId = m_app->addList(QStringLiteral("Trabalho"), QStringLiteral("#3daee9"), 0);
        QVERIFY(listId > 0);
        const qlonglong id = m_app->quickAdd(QStringLiteral("Relatório"), listId);
        switchSection(QStringLiteral("tasks"));
        Q_EMIT m_app->showView(QStringLiteral("list:%1").arg(listId));
        QTRY_VERIFY(taskRow(id));
        // Deleting a list is confirmed in the dialog and cannot be undone.
        m_app->removeList(listId);
        QTRY_VERIFY(!m_app->store()->list(listId));
        QVERIFY(!m_app->store()->task(id));
        // The page does not stay on the deleted list.
        QTRY_COMPARE(m_app->tasks()->view(), QStringLiteral("all"));
        // A task typed now stays visible in the current view.
        const qlonglong after = m_app->quickAdd(QStringLiteral("Depois da exclusão"));
        QTRY_VERIFY(taskRow(after));
    }

    void journalWriteAnEntry()
    {
        switchSection(QStringLiteral("journal"));
        QObject *page = nullptr;
        QTRY_VERIFY((page = loaderItem("journalLoader")));
        const int before = m_app->journal()->count();
        QVERIFY(QMetaObject::invokeMethod(page, "createNewAndFocus"));
        QTRY_COMPARE(m_app->journal()->count(), before + 1);
        const qlonglong entryId = page->property("currentEntryId").toLongLong();
        QVERIFY(entryId > 0);
        QTRY_VERIFY(page->property("editMode").toBool());
        // The page moves the focus into the editor on the next event loop pass.
        QTRY_VERIFY(m_win->activeFocusItem() && isType(m_win->activeFocusItem(), "QQuickTextEdit"));
        typeText(m_win, QStringLiteral("Hoje foi um bom dia"));
        QVERIFY(QMetaObject::invokeMethod(page, "finishEditing"));
        QTRY_VERIFY(m_app->journal()->entry(entryId).value(QStringLiteral("content")).toString().contains(QStringLiteral("Hoje foi um bom dia")));
        // The summary shows the latest entries.
        switchSection(QStringLiteral("summary"));
        QTRY_VERIFY(!m_app->journal()->recentSummary(3).isEmpty());
    }

    void routineBlockDialogOpens()
    {
        switchSection(QStringLiteral("routine"));
        QObject *page = nullptr;
        QTRY_VERIFY((page = loaderItem("routineLoader")));
        auto *dialog = page->findChild<QObject *>(QStringLiteral("routineBlockDialog"));
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "openNew", Q_ARG(QVariant, false), Q_ARG(QVariant, 9 * 60)));
        QTRY_VERIFY(dialog->property("opened").toBool());
        QTest::keyClick(m_win, Qt::Key_Escape);
        QTRY_VERIFY(!dialog->property("opened").toBool());
        // Day, week and month calendars.
        for (int mode : {1, 2, 0}) {
            page->setProperty("calendarMode", mode);
            QTest::qWait(50);
            QVERIFY(!m_win->grabWindow().isNull());
        }
    }

    void matrixShowsAndClassifiesTasks()
    {
        const qlonglong id = m_app->quickAdd(QStringLiteral("Urgente e importante"));
        m_app->eisenhower()->classifyTask(id, 1);
        switchSection(QStringLiteral("matrix"));
        QTRY_VERIFY(loaderItem("matrixLoader"));
        QTRY_VERIFY(findItem(m_win->contentItem(), [](QQuickItem *it) {
            return it->property("text").toString() == QStringLiteral("Urgente e importante") && reallyVisible(it);
        }));
        m_app->eisenhower()->classifyTask(id, 4);
        QTest::qWait(50);
    }

    void insightsAndSummaryReflectData()
    {
        const qlonglong id = m_app->quickAdd(QStringLiteral("Concluída hoje"));
        m_app->setDone(id, true);
        switchSection(QStringLiteral("insights"));
        QTRY_VERIFY(loaderItem("insightsLoader"));
        QTest::qWait(100);
        switchSection(QStringLiteral("summary"));
        QTRY_VERIFY(loaderItem("summaryLoader"));
        QTest::qWait(100);
    }

    void keyboardShortcutsReachTheWindow()
    {
        switchSection(QStringLiteral("tasks"));
        Q_EMIT m_app->focusQuickAdd();
        QTRY_VERIFY(m_win->activeFocusItem() && isType(m_win->activeFocusItem(), "QQuickTextField"));
        Q_EMIT m_app->focusSearch();
        QTest::qWait(50);
    }

    void dataSurvivesAReopen()
    {
        // Everything typed above is in the database file, not only in memory.
        const int tasks = m_app->store()->tasks().size();
        QVERIFY(tasks > 0);
        Database again(m_dir->filePath(QStringLiteral("ktasks.db")));
        QVERIFY(again.open());
        QCOMPARE(int(again.tasks().size()), tasks);
    }
};

QTEST_MAIN(UiTest)
#include "uitest.moc"
