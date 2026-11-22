// SPDX-License-Identifier: GPL-3.0-or-later
#include "analyticsmanager.h"
#include "application.h"
#include "backupmanager.h"
#include "discordpresence.h"
#include "eisenhowermanager.h"
#include "focusmanager.h"
#include "journalmanager.h"
#include "notifier.h"
#include "pdfexporter.h"
#include "routinemanager.h"
#include "settings.h"
#include "sidebarmodel.h"
#include "summarymanager.h"
#include "taskmodel.h"
#include "taskstore.h"

#include <KLocalizedQmlContext>
#include <KLocalizedString>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace KTasks;

class AppTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<Database> db;
    std::unique_ptr<TaskStore> store;
    std::unique_ptr<Settings> settings;
    qint64 listId = 0;

    qint64 add(const QString &title, const std::function<void(Task &)> &fn = {})
    {
        Task t;
        t.title = title;
        t.listId = listId;
        if (fn) {
            fn(t);
        }
        return store->addTask(t);
    }
    QStringList titles(const TaskModel &m)
    {
        QStringList out;
        for (int i = 0; i < m.rowCount(); ++i) {
            out << m.index(i).data(TaskModel::TitleRole).toString();
        }
        return out;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }
    void init()
    {
        dir = std::make_unique<QTemporaryDir>();
        db = std::make_unique<Database>(dir->filePath("a.db"));
        QVERIFY(db->open());
        store = std::make_unique<TaskStore>(db.get());
        settings = std::make_unique<Settings>();
        settings->setShowCompleted(false);
        settings->setLastView("all");
        settings->setSortMode("all", 0);
        settings->setNotifyReminders(true);
        settings->setNotifyAtDue(true);
        settings->setAllDayReminderTime("09:00");
        listId = store->addList("Casa", "", 0);
        settings->setSortMode(QString("list:%1").arg(listId), 0);
        QVERIFY(listId > 0);
    }
    void cleanup()
    {
        store.reset();
        settings.reset();
        db.reset();
        dir.reset();
    }

    void addUndoRedo()
    {
        const qint64 id = add("Uma");
        QVERIFY(id > 0);
        QVERIFY(store->canUndo());
        store->undo();
        QVERIFY(store->tasks().isEmpty());
        QVERIFY(store->canRedo());
        store->redo();
        QCOMPARE(store->tasks().size(), 1);
        QCOMPARE(store->tasks().first().id, id); // same row identity after redo
        store->undo();
        QVERIFY(store->tasks().isEmpty());
    }

    void completingAParentCompletesItsSubtasks()
    {
        const qint64 parent = add("Pai");
        const QString parentUid = store->task(parent)->uid;
        const qint64 child = add("Filha", [&](Task &t) {
            t.parentUid = parentUid;
        });
        store->setCompleted(parent, true);
        QVERIFY(store->task(parent)->isDone());
        QVERIFY(store->task(child)->isDone());
        store->undo();
        QVERIFY(!store->task(parent)->isDone());
        QVERIFY(!store->task(child)->isDone());

        // Reopening a subtask reopens the parent.
        store->setCompleted(parent, true);
        store->setCompleted(child, false);
        QVERIFY(!store->task(parent)->isDone());
    }

    void completingARecurringTaskMovesIt()
    {
        const QDate today = QDate::currentDate();
        const qint64 id = add("Academia", [&](Task &t) {
            t.due = QDateTime(today, QTime(0, 0));
            t.rrule = "FREQ=DAILY";
        });
        store->setCompleted(id, true);
        const Task *t = store->task(id);
        QVERIFY(!t->isDone());
        QCOMPARE(t->due.date(), today.addDays(1));
        store->undo();
        QCOMPARE(store->task(id)->due.date(), today);
    }

    void lastOccurrenceCompletesForGood()
    {
        const qint64 id = add("Uma vez", [&](Task &t) {
            t.due = QDateTime(QDate::currentDate(), QTime(0, 0));
            t.rrule = "FREQ=DAILY;COUNT=1";
        });
        store->setCompleted(id, true);
        QVERIFY(store->task(id)->isDone());
    }

    void deleteWithSubtasksAndUndo()
    {
        const qint64 parent = add("Pai");
        const QString uid = store->task(parent)->uid;
        add("Filha 1", [&](Task &t) { t.parentUid = uid; });
        add("Filha 2", [&](Task &t) { t.parentUid = uid; });
        store->removeTask(parent);
        QVERIFY(store->tasks().isEmpty());
        store->undo();
        QCOMPARE(store->tasks().size(), 3);
        QCOMPARE(store->childrenOf(*store->task(parent)).size(), 2);
    }

    void undoingTheDeleteOfASyncedTaskQueuesAnUpload()
    {
        const qint64 id = add("Sincronizada");
        QVERIFY(db->markUploaded(id, store->task(id)->rev, "/dav/x.ics", "e1", "ICS"));
        store->reload();
        QCOMPARE(store->task(id)->syncState, SyncState::Synced);
        store->removeTask(id);
        QVERIFY(!store->task(id));
        QCOMPARE(db->task(id)->syncState, SyncState::Deleted); // tombstone waits for the server
        store->undo();
        QVERIFY(store->task(id));
        QCOMPARE(store->task(id)->syncState, SyncState::Modified);
        QCOMPARE(store->task(id)->href, QString("/dav/x.ics"));
    }

    void undoAfterTheDeleteWasAlreadySynced()
    {
        const qint64 id = add("Sincronizada");
        db->markUploaded(id, store->task(id)->rev, "/dav/x.ics", "e1", "ICS");
        store->reload();
        store->removeTask(id);
        db->purgeTask(id); // what the sync engine does after the server confirmed
        store->undo();
        const Task *t = store->task(id);
        QVERIFY(t);
        QCOMPARE(t->syncState, SyncState::New); // goes up as a new task
        QVERIFY(t->href.isEmpty());
    }

    void moveToAnotherListAndBack()
    {
        const qint64 other = store->addList("Trabalho", "", 0);
        const qint64 parent = add("Pai");
        const QString uid = store->task(parent)->uid;
        const qint64 child = add("Filha", [&](Task &t) { t.parentUid = uid; });
        store->moveToList(parent, other);
        QCOMPARE(store->task(parent)->listId, other);
        QCOMPARE(store->task(child)->listId, other);
        store->undo();
        QCOMPARE(store->task(parent)->listId, listId);
        QCOMPARE(store->task(child)->listId, listId);
    }

    void normalization()
    {
        const qint64 id = add("Repete sem data", [](Task &t) { t.rrule = "FREQ=WEEKLY"; });
        const Task *t = store->task(id);
        QVERIFY(t->due.isValid()); // a recurring task gets an anchor date
        QCOMPARE(t->start, t->due);
        const qint64 id2 = add("Lembrete sem data", [](Task &t) { t.alarms = {Alarm{Alarm::RelativeToDue, {}, -600}}; });
        QVERIFY(store->task(id2)->alarms.isEmpty());
    }

    void modelViews()
    {
        const QDate today = QDate::currentDate();
        add("Hoje", [&](Task &t) { t.due = QDateTime(today, QTime(0, 0)); });
        add("Atrasada", [&](Task &t) { t.due = QDateTime(today.addDays(-3), QTime(0, 0)); });
        add("Semana", [&](Task &t) { t.due = QDateTime(today.addDays(4), QTime(0, 0)); });
        add("Longe", [&](Task &t) { t.due = QDateTime(today.addDays(40), QTime(0, 0)); });
        add("Sem data");
        const qint64 done = add("Feita", [&](Task &t) { t.due = QDateTime(today, QTime(0, 0)); });
        store->setCompleted(done, true);

        TaskModel m(store.get(), settings.get());
        m.setView("today");
        QCOMPARE(titles(m), QStringList({"Atrasada", "Hoje"}));
        QCOMPARE(m.hiddenCompleted(), 1);
        m.setView("overdue");
        QCOMPARE(titles(m), QStringList({"Atrasada"}));
        m.setView("week");
        QCOMPARE(titles(m), QStringList({"Hoje", "Semana"}));
        m.setView("nodate");
        QCOMPARE(titles(m), QStringList({"Sem data"}));
        m.setView("completed");
        QCOMPARE(titles(m), QStringList({"Feita"}));
        m.setView("all");
        QCOMPARE(m.rowCount(), 5);
        m.setShowCompleted(true);
        QCOMPARE(m.rowCount(), 6);
        QCOMPARE(titles(m).last(), QString("Feita")); // completed tasks sink to the bottom
    }

    void modelTreeAndCollapse()
    {
        const qint64 a = add("A");
        const QString uid = store->task(a)->uid;
        add("A1", [&](Task &t) { t.parentUid = uid; });
        const qint64 a2 = add("A2", [&](Task &t) { t.parentUid = uid; });
        add("B");
        add("Órfã", [](Task &t) { t.parentUid = "missing-parent"; });

        TaskModel m(store.get(), settings.get());
        m.setView(QString("list:%1").arg(listId));
        QCOMPARE(titles(m), QStringList({"A", "A1", "A2", "B", "Órfã"}));
        QCOMPARE(m.index(1).data(TaskModel::DepthRole).toInt(), 1);
        QCOMPARE(m.index(0).data(TaskModel::ChildCountRole).toInt(), 2);
        QVERIFY(m.canReorder());

        m.toggleCollapsed(a);
        QCOMPARE(titles(m), QStringList({"A", "B", "Órfã"}));
        m.toggleCollapsed(a);
        store->setCompleted(a2, true);
        QCOMPARE(titles(m), QStringList({"A", "A1", "B", "Órfã"}));
        QCOMPARE(m.index(0).data(TaskModel::ChildDoneRole).toInt(), 1);
    }

    void modelSearchIgnoresAccentsAndCase()
    {
        add("Reunião com João");
        add("Outra", [](Task &t) { t.notes = "ligar para a JOANA"; });
        add("Com etiqueta", [](Task &t) { t.tags = {"finanças"}; });
        TaskModel m(store.get(), settings.get());
        m.setSearchText("reuniao");
        QCOMPARE(titles(m), QStringList({"Reunião com João"}));
        m.setSearchText("joa");
        QCOMPARE(m.rowCount(), 2);
        m.setSearchText("FINANCAS");
        QCOMPARE(titles(m), QStringList({"Com etiqueta"}));
        m.setSearchText("");
        QCOMPARE(m.rowCount(), 3);
    }

    void modelSortAndReorder()
    {
        add("Baixa", [](Task &t) { t.priority = 9; });
        add("Alta", [](Task &t) { t.priority = 1; });
        add("Nenhuma");
        TaskModel m(store.get(), settings.get());
        const QString view = QString("list:%1").arg(listId);
        m.setView(view);
        QCOMPARE(titles(m), QStringList({"Baixa", "Alta", "Nenhuma"})); // manual: creation order
        m.setSortMode(TaskModel::SortPriority);
        QCOMPARE(titles(m), QStringList({"Alta", "Baixa", "Nenhuma"}));
        QVERIFY(!m.canReorder());
        m.setSortMode(TaskModel::SortDefault);

        // Drag the last row to the top.
        m.move(2, 1);
        m.move(1, 0);
        m.commitMove();
        QCOMPARE(titles(m), QStringList({"Nenhuma", "Baixa", "Alta"}));
        store->undo();
        QCOMPARE(titles(m), QStringList({"Baixa", "Alta", "Nenhuma"}));
    }

    void dragIntoSubtasksMakesASubtask()
    {
        const qint64 a = add("A");
        const QString uid = store->task(a)->uid;
        add("A1", [&](Task &t) { t.parentUid = uid; });
        add("A2", [&](Task &t) { t.parentUid = uid; });
        const qint64 b = add("B");
        TaskModel m(store.get(), settings.get());
        m.setView(QString("list:%1").arg(listId));
        m.move(3, 2); // B between A1 and A2
        m.commitMove();
        QCOMPARE(store->task(b)->parentUid, uid);
        QCOMPARE(titles(m), QStringList({"A", "A1", "B", "A2"}));
    }

    void remindersFireOnceAndCanBeSnoozed()
    {
        Notifier notifier(store.get(), settings.get());
        const QDateTime now = QDateTime::currentDateTime();
        const qint64 soon = add("Daqui a pouco", [&](Task &t) {
            t.allDay = false;
            t.due = now.addSecs(3600).toUTC();
            t.alarms = {Alarm{Alarm::RelativeToDue, {}, -900}};
        });
        add("Feita", [&](Task &t) {
            t.allDay = false;
            t.due = now.addSecs(-60).toUTC();
            t.status = TaskStatus::Completed;
        });
        const QString uid = store->task(soon)->uid;

        QDateTime next;
        QVERIFY(notifier.pending(now, &next).isEmpty());
        QVERIFY(qAbs(next.secsTo(now.addSecs(2700))) < 2); // 15 minutes before the due time

        auto due = notifier.pending(now.addSecs(2701));
        QCOMPARE(due.size(), 1);
        QCOMPARE(due.first().taskId, soon);
        db->setReminderFired(uid, due.first().key);
        QVERIFY(notifier.pending(now.addSecs(2800)).isEmpty()); // not twice

        db->setSnooze(uid, now.addSecs(4000).toUTC());
        QVERIFY(notifier.pending(now.addSecs(3900)).isEmpty());
        due = notifier.pending(now.addSecs(4001));
        QCOMPARE(due.size(), 1);
        QVERIFY(due.first().snoozed);
    }

    void allDayTasksNotifyAtTheConfiguredHour()
    {
        Notifier notifier(store.get(), settings.get());
        const QDate day = QDate::currentDate().addDays(2);
        add("Dia inteiro", [&](Task &t) { t.due = QDateTime(day, QTime(0, 0)); });
        QVERIFY(notifier.pending(QDateTime(day, QTime(8, 59))).isEmpty());
        QCOMPARE(notifier.pending(QDateTime(day, QTime(9, 1))).size(), 1);
        settings->setNotifyAtDue(false);
        QVERIFY(notifier.pending(QDateTime(day, QTime(9, 1))).isEmpty());
        settings->setNotifyAtDue(true);
        settings->setNotifyReminders(false);
        QVERIFY(notifier.pending(QDateTime(day, QTime(9, 1))).isEmpty());
    }

    void oldRemindersAreNotReplayed()
    {
        Notifier notifier(store.get(), settings.get());
        add("Muito antiga", [&](Task &t) { t.due = QDateTime(QDate::currentDate().addDays(-30), QTime(0, 0)); });
        QVERIFY(notifier.pending(QDateTime::currentDateTime()).isEmpty());
    }

    void journalLockUnlockAndSearch()
    {
        JournalManager jm(db.get());
        QVERIFY(!jm.notebooks().isEmpty());
        const qlonglong workNb = jm.createNotebook("Trabalho", "#e74c3c");
        QVERIFY(workNb > 0);
        QVERIFY(jm.renameNotebook(workNb, "Estudos", "#2ecc71"));
        jm.setSelectedNotebookId(workNb);

        const qint64 id = jm.createEntry("Reflexões", "2026-11-21", workNb);
        QVERIFY(id > 0);
        QCOMPARE(jm.entry(id).value("notebookId").toLongLong(), workNb);
        QCOMPARE(jm.entryIdAt(0), id);
        QVERIFY(jm.updateEntry(id, {{"content", "Conteúdo secreto **markdown**"}}));
        QVERIFY(jm.lockWithPassword(id, "minhaSenha"));
        QVERIFY(jm.entry(id).value("locked").toBool());
        QVERIFY(!jm.entry(id).value("unlocked").toBool());
        QVERIFY(jm.entry(id).value("content").toString().isEmpty());

        QVERIFY(!jm.unlockEntry(id, "errada"));
        QVERIFY(jm.unlockEntry(id, "minhaSenha"));
        QCOMPARE(jm.entry(id).value("content").toString(), QString("Conteúdo secreto **markdown**"));
        QVERIFY(jm.changePassword(id, "minhaSenha", "novaSenha"));
        jm.relockEntry(id);
        QVERIFY(!jm.unlockEntry(id, "minhaSenha"));
        QVERIFY(jm.unlockEntry(id, "novaSenha"));
        QVERIFY(jm.removePassword(id, "novaSenha"));
        QVERIFY(!jm.entry(id).value("locked").toBool());

        // Switching to a different notebook filters out the entry, switching back shows it
        jm.setSelectedNotebookId(1);
        QCOMPARE(jm.entryIdAt(0), qlonglong(0));
        jm.setSelectedNotebookId(workNb);
        QCOMPARE(jm.entryIdAt(0), id);

        // Synced <-> Offline Only transitions (plus locking keeps notes offline)
        Account nc;
        nc.name = "user@cloud";
        nc.serverUrl = "https://cloud.example.com";
        nc.username = "user";
        QVERIFY(db->insertAccount(nc));
        jm.reload();
        QVERIFY(jm.setEntrySyncAccount(id, nc.id));
        QVERIFY(jm.entry(id).value("synced").toBool());
        QCOMPARE(jm.entry(id).value("syncAccountName").toString(), QString("user@cloud"));

        // Locking a synced note moves it safely back to offline-only
        QVERIFY(jm.lockWithPassword(id, "pwd"));
        QVERIFY(!jm.entry(id).value("synced").toBool());
        QVERIFY(!jm.setEntrySyncAccount(id, nc.id)); // cannot sync while locked
        QVERIFY(jm.unlockEntry(id, "pwd"));
        QVERIFY(jm.removePassword(id, "pwd"));
        QVERIFY(jm.setNotebookSyncAccount(workNb, nc.id, true));
        QVERIFY(jm.entry(id).value("synced").toBool());
        QVERIFY(jm.setEntrySyncAccount(id, 0));
        QVERIFY(!jm.entry(id).value("synced").toBool());

        QVERIFY(jm.deleteNotebook(workNb));
        QVERIFY(!jm.entry(id).value("id").isValid());
    }

    void routineNotificationsAndPdfHtml()
    {
        RoutineManager rm(db.get());
        const qint64 bid = rm.addBlock({
            {"title", "Foco matinal"},
            {"startMinute", 9 * 60},
            {"durationMinutes", 60},
            {"isTemplate", true},
            {"daysMask", 127},
            {"notifyEnabled", true},
            {"notifyMinutesBefore", 10},
        });
        QVERIFY(bid > 0);
        QCOMPARE(rm.totalBlocks(), 1);

        Notifier notifier(store.get(), settings.get());
        notifier.setRoutineManager(&rm);
        settings->setNotifyRoutines(true);
        const QDate today = QDate::currentDate();
        QVERIFY(notifier.pendingRoutines(QDateTime(today, QTime(8, 49))).isEmpty());
        QCOMPARE(notifier.pendingRoutines(QDateTime(today, QTime(8, 51))).size(), 1);

        rm.setBlockStatus(bid, int(RoutineLogStatus::Done));
        QCOMPARE(rm.completedBlocks(), 1);
        QVERIFY(notifier.pendingRoutines(QDateTime(today, QTime(8, 51))).isEmpty());

        // Skip today and then revert back to pending (status 0)
        rm.setBlockStatus(bid, int(RoutineLogStatus::Skipped));
        QVERIFY(notifier.pendingRoutines(QDateTime(today, QTime(8, 51))).isEmpty());
        rm.setBlockStatus(bid, int(RoutineLogStatus::Pending));
        QCOMPARE(rm.completedBlocks(), 0);
        QCOMPARE(notifier.pendingRoutines(QDateTime(today, QTime(8, 51))).size(), 1);

        add("Tarefa para PDF", [&](Task &t) {
            t.due = QDateTime(today, QTime(14, 0));
            t.tags = {"trabalho"};
        });
        TaskModel tm(store.get(), settings.get());
        JournalManager jm(db.get());
        PdfExporter pdf(store.get(), &tm, &rm, &jm);
        const QString html = pdf.buildHtml({{"scope", "today"}});
        QVERIFY(html.contains("Tarefa para PDF"));
        QVERIFY(html.contains("#trabalho"));
        const QString rhtml = pdf.buildHtml({{"scope", "routine"}});
        QVERIFY(rhtml.contains("Foco matinal"));

        QCOMPARE(rm.weekDays().size(), 7);
        QCOMPARE(rm.monthCells().size(), 42);
    }

    void backupManagerCreatesAndRestores()
    {
        BackupManager bm(db.get(), settings.get());
        QTemporaryDir bdir;
        settings->setBackupDirectory(bdir.path());
        settings->setBackupRetentionCount(2);
        add("Antes do backup");
        QVERIFY(bm.createBackupNow());
        QCOMPARE(bm.availableBackups().size(), 1);
        const QUrl snapUrl = bm.availableBackups().first().toMap().value("url").toUrl();
        store->removeTask(store->tasks().first().id);
        QVERIFY(store->tasks().isEmpty());
        bm.restoreFromFile(snapUrl);
        store->reload();
        QCOMPARE(store->tasks().size(), 1);
    }

    void moveListAndToggleRoutineSync()
    {
        Account nc;
        nc.name = "u@cloud";
        nc.serverUrl = "https://cloud.example.com";
        nc.username = "u";
        QVERIFY(db->insertAccount(nc));
        store->reload();

        TaskList localList;
        localList.name = "Projeto Local";
        QVERIFY(db->insertList(localList));
        store->reload();
        add("Mover para nuvem", [&](Task &t) {
            t.listId = localList.id;
        });

        QVERIFY(store->moveListToAccount(localList.id, nc.id));
        QCOMPARE(store->list(localList.id)->accountId, nc.id);
        QVERIFY(store->moveListToAccount(localList.id, 0));
        QCOMPARE(store->list(localList.id)->accountId, qint64(0));

        RoutineManager rm(db.get());
        const qlonglong bid = rm.addBlock({
            {"title", "Bloco híbrido"},
            {"startMinute", 10 * 60},
            {"durationMinutes", 30},
            {"isTemplate", true},
            {"daysMask", 127},
            {"syncAccountId", 0},
        });
        QVERIFY(bid > 0);
        QVERIFY(!rm.blockInfo(bid).value("synced").toBool());
        QVERIFY(rm.setBlockSyncAccount(bid, nc.id));
        QVERIFY(rm.blockInfo(bid).value("synced").toBool());
        QVERIFY(rm.setBlockSyncAccount(bid, 0));
        QVERIFY(!rm.blockInfo(bid).value("synced").toBool());
    }

    void eisenhowerMatrixWorkflow()
    {
        EisenhowerManager em(store.get(), settings.get());
        const QDate today = QDate::currentDate();

        const qint64 tRaw = add("Organizar arquivos");
        const qint64 tDue = add("Responder cliente urgente", [&](Task &t) {
            t.due = QDateTime(today, QTime(0, 0));
            t.allDay = true;
            t.tags = {"trabalho"};
        });
        const qint64 tPlan = add("Estudar arquitetura", [&](Task &t) {
            t.due = QDateTime(today.addDays(1), QTime(0, 0));
            t.allDay = true;
        });
        const qint64 tFar = add("Trocar filtro", [&](Task &t) {
            t.due = QDateTime(today.addDays(10), QTime(0, 0));
            t.allDay = true;
        });

        // Raw & unprioritized tasks enter Triage; urgent ones are surfaced first
        QCOMPARE(em.triageCount(), 4);
        QCOMPARE(em.triageTasks().first().toMap().value("id").toLongLong(), tDue);
        QVERIFY(em.triageTasks().first().toMap().value("urgentDue").toBool());

        // Classify across all 4 quadrants and verify automatic VTODO priorities
        em.classifyTask(tDue, 1);
        em.classifyTask(tPlan, 2);
        em.classifyTask(tRaw, 3);
        em.classifyTask(tFar, 4);

        QCOMPARE(em.triageCount(), 0);
        QCOMPARE(em.q1Tasks().size(), 1);
        QCOMPARE(em.q2Tasks().size(), 1);
        QCOMPARE(em.q3Tasks().size(), 1);
        QCOMPARE(em.q4Tasks().size(), 1);

        QCOMPARE(store->task(tDue)->priorityLevel(), 3);
        QCOMPARE(store->task(tPlan)->priorityLevel(), 2);
        QCOMPARE(store->task(tRaw)->priorityLevel(), 1);
        QCOMPARE(store->task(tFar)->priorityLevel(), 0);
        QCOMPARE(store->task(tFar)->quadrant, 4);

        // Q2 due tomorrow within 48h window gets promotion hint to Q1
        QVERIFY(em.q2Tasks().first().toMap().value("suggestPromote").toBool());

        // Reorder multiple items in Q1
        const qint64 tQ1b = add("Segundo item Q1", [&](Task &t) {
            t.quadrant = 1;
            t.priority = Task::priorityForQuadrant(1);
        });
        QCOMPARE(em.q1Tasks().size(), 2);
        em.moveToQuadrantAt(tQ1b, 1, tDue);
        QCOMPARE(em.q1Tasks().first().toMap().value("id").toLongLong(), tQ1b);
        em.moveTaskStep(tQ1b, 1);
        QCOMPARE(em.q1Tasks().last().toMap().value("id").toLongLong(), tQ1b);

        // Filters narrow view without losing tasks
        em.setFilterTag("trabalho");
        QCOMPARE(em.q1Tasks().size(), 1);
        QCOMPARE(em.q2Tasks().size(), 0);
        em.setFilterTag(QString());
        em.setSearchText("arquitetura");
        QCOMPARE(em.q2Tasks().size(), 1);
        QCOMPARE(em.q1Tasks().size(), 0);
        em.setSearchText(QString());

        // PDF matrix export
        TaskModel tm(store.get(), settings.get());
        RoutineManager rm(db.get());
        JournalManager jm(db.get());
        PdfExporter pdf(store.get(), &tm, &rm, &jm);
        const QString html = pdf.buildHtml({{"scope", "matrix"}});
        QVERIFY(html.contains("Q1"));
        QVERIFY(html.contains("Estudar arquitetura"));

        // Clear Q4 is undoable
        QCOMPARE(em.clearQ4(), 1);
        QCOMPARE(em.q4Tasks().size(), 0);
        store->undo();
        QCOMPARE(em.q4Tasks().size(), 1);

        // Equal sortOrders reorder deterministically without collisions
        const qint64 eqA = add("Igual A", [&](Task &t) { t.quadrant = 1; t.priority = 1; t.sortOrder = 500; });
        const qint64 eqB = add("Igual B", [&](Task &t) { t.quadrant = 1; t.priority = 1; t.sortOrder = 500; });
        em.moveToQuadrantAt(eqB, 1, eqA);
        const int idxA = store->task(eqA)->sortOrder;
        const int idxB = store->task(eqB)->sortOrder;
        QVERIFY(idxB < idxA);
    }

    void focusPomodoroAndAnalyticsWorkflow()
    {
        settings->setPomodoroFocusMinutes(25);
        settings->setPomodoroShortBreakMinutes(5);
        settings->setNotifyFocus(false);

        const qint64 t1 = add("Implementar módulo", [&](Task &t) {
            t.quadrant = 2;
            t.priority = Task::priorityForQuadrant(2);
            t.estimatedMinutes = 30;
        });

        FocusManager fm(store.get(), settings.get());
        RoutineManager rm(db.get());
        AnalyticsManager am(store.get(), &rm, &fm);

        // 25 min Pomodoro run completes cleanly and updates task + analytics
        fm.startPomodoro(t1);
        QVERIFY(fm.hasSession());
        QVERIFY(fm.running());
        fm.advanceSecondsForTesting(25 * 60);
        QVERIFY(!fm.hasSession());
        QCOMPARE(store->task(t1)->pomodorosCompleted, 1);
        QCOMPARE(store->task(t1)->trackedSeconds, 25 * 60);

        // Stopwatch execution adds 10 more minutes on stopAndSave()
        fm.startStopwatch(t1);
        fm.advanceSecondsForTesting(10 * 60);
        fm.stopAndSave();
        QCOMPARE(store->task(t1)->trackedSeconds, 35 * 60);

        // Completing task feeds analytics KPIs, quadrant balance, daily chart & insights
        store->setCompleted(t1, true);
        const QVariantMap sum = am.summary();
        QCOMPARE(sum.value("completedTasks").toInt(), 1);
        QCOMPARE(sum.value("pomodorosCount").toInt(), 1);
        QCOMPARE(sum.value("trackedSeconds").toInt(), 35 * 60);
        QCOMPARE(sum.value("q2PlanningPct").toInt(), 100);
        QVERIFY(sum.value("estimationAccuracyPct").toInt() >= 80);
        QVERIFY(am.dailySeries().size() >= 7);
        QVERIFY(!am.insights().isEmpty());
        QCOMPARE(am.recentSessions().size(), 2);

        // Manual log + removal updates analytics
        fm.addManualMinutes(t1, 15);
        QCOMPARE(store->task(t1)->trackedSeconds, 50 * 60);
        QCOMPARE(am.recentSessions().size(), 3);
        const qlonglong sid = am.recentSessions().first().toMap().value("id").toLongLong();
        QVERIFY(fm.deleteSession(sid));
        QCOMPARE(am.recentSessions().size(), 2);
    }

    void performanceAndOptimizationInvariants()
    {
        // 1. TaskStore omits heavy base_ics from UI memory while DB keeps full sync payload
        Task synced;
        synced.uid = QStringLiteral("sync-uid-opt-1");
        synced.listId = listId;
        synced.title = QStringLiteral("Synced CalDAV task");
        synced.baseIcs = QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nBEGIN:VTODO\r\nSUMMARY:Synced CalDAV task\r\nEND:VTODO\r\nEND:VCALENDAR");
        synced.syncState = SyncState::Synced;
        QVERIFY(db->insertTask(synced));
        store->reload();

        const Task *uiTask = store->task(synced.id);
        QVERIFY(uiTask != nullptr);
        QVERIFY(uiTask->baseIcs.isEmpty());
        const QList<Task> dbTasks = db->tasks(false, true);
        auto it = std::find_if(dbTasks.cbegin(), dbTasks.cend(), [&](const Task &x) { return x.id == synced.id; });
        QVERIFY(it != dbTasks.cend());
        QVERIFY(it->baseIcs.contains(QStringLiteral("BEGIN:VCALENDAR")));

        // 2. FocusManager separates tickChanged from stateChanged (zero bar/card churn on 1s ticks)
        settings->setPomodoroFocusMinutes(25);
        FocusManager fm(store.get(), settings.get());
        QSignalSpy stateSpy(&fm, &FocusManager::stateChanged);
        QSignalSpy tickSpy(&fm, &FocusManager::tickChanged);
        fm.startPomodoro(synced.id);
        QVERIFY(stateSpy.count() >= 1);
        QVERIFY(tickSpy.count() >= 1);
        stateSpy.clear();
        tickSpy.clear();

        fm.advanceSecondsForTesting(10);
        QCOMPARE(stateSpy.count(), 0);
        QCOMPARE(tickSpy.count(), 10);
        QCOMPARE(fm.timeText(), QStringLiteral("24:50"));

        fm.togglePause();
        QCOMPARE(stateSpy.count(), 1);
        QCOMPARE(tickSpy.count(), 10);
        fm.discard();

        // 3. AnalyticsManager defers eager SQL rebuilds when inactive and releases memory when hidden
        RoutineManager rm(db.get());
        AnalyticsManager am(store.get(), &rm, &fm);
        QVERIFY(!am.active());
        QVERIFY(am.isDirty());
        add(QStringLiteral("Lazy check task"));
        QVERIFY(am.isDirty());

        QSignalSpy amSpy(&am, &AnalyticsManager::changed);
        am.setActive(true);
        QVERIFY(am.active());
        QVERIFY(!am.isDirty());
        QCOMPARE(amSpy.count(), 1);
        am.setActive(false);
        QVERIFY(!am.active());
        QVERIFY(am.isDirty());

        // 4. EisenhowerManager uses incremental QuadrantTaskModel diffing without resetting ListViews
        EisenhowerManager em(store.get(), settings.get());
        QVERIFY(!em.active());
        em.setActive(true);
        QVERIFY(!em.isDirty());

        auto *q1 = qobject_cast<QuadrantTaskModel *>(em.q1Model());
        QVERIFY(q1 != nullptr);
        QSignalSpy resetSpy(q1, &QAbstractItemModel::modelReset);
        QSignalSpy insSpy(q1, &QAbstractItemModel::rowsInserted);
        QSignalSpy moveSpy(q1, &QAbstractItemModel::rowsMoved);
        QSignalSpy dataSpy(q1, &QAbstractItemModel::dataChanged);

        const qint64 qa = add(QStringLiteral("Q1 Alpha"), [](Task &t) { t.quadrant = 1; t.priority = 1; t.sortOrder = 100; });
        const qint64 qb = add(QStringLiteral("Q1 Beta"), [](Task &t) { t.quadrant = 1; t.priority = 1; t.sortOrder = 200; });
        QCOMPARE(q1->rowCount(), 2);
        QCOMPARE(q1->idAt(0), qa);
        QCOMPARE(q1->idAt(1), qb);
        QVERIFY(insSpy.count() >= 2);
        QCOMPARE(resetSpy.count(), 0);

        em.moveToQuadrantAt(qb, 1, qa);
        QCOMPARE(q1->idAt(0), qb);
        QCOMPARE(q1->idAt(1), qa);
        QCOMPARE(resetSpy.count(), 0);
        QVERIFY(moveSpy.count() >= 1);

        store->recordFocus(qb, 90, 0);
        QVERIFY(dataSpy.count() >= 1);
        QCOMPARE(resetSpy.count(), 0);
    }

    void summaryBatchAndExternalCalendarWorkflow()
    {
        // 1. Batch complete & batch move execute in a single atomic undo step each
        const qint64 otherList = store->addList("Trabalho", "#2980b9", 0);
        const QDate today = QDate::currentDate();
        const qint64 m1 = add("Item Multi 1", [&](Task &t) { t.due = QDateTime(today, QTime(0, 0)); t.quadrant = 1; t.priority = 1; });
        const qint64 m2 = add("Item Multi 2", [&](Task &t) { t.due = QDateTime(today.addDays(-1), QTime(0, 0)); t.quadrant = 2; t.priority = 5; });
        store->setCompletedMany({m1, m2}, true);
        QVERIFY(store->task(m1)->isDone());
        QVERIFY(store->task(m2)->isDone());
        store->undo();
        QVERIFY(!store->task(m1)->isDone());
        QVERIFY(!store->task(m2)->isDone());

        store->moveToListMany({m1, m2}, otherList);
        QCOMPARE(store->task(m1)->listId, otherList);
        QCOMPARE(store->task(m2)->listId, otherList);
        store->undo();
        QCOMPARE(store->task(m1)->listId, listId);
        QCOMPARE(store->task(m2)->listId, listId);

        // 2. SidebarModel groups smart views under the "Tasks" section header
        SidebarModel sb(store.get());
        QVERIFY(sb.rowCount() >= 6);
        QVERIFY(!sb.index(0).data(SidebarModel::SectionRole).toString().isEmpty());

        // 3. SummaryManager aggregates KPIs, routine, and read-only KOrganizer/.ics events without touching RoutineManager
        settings->setProfileName(QStringLiteral("Alex Silva"));
        settings->setIntegrateKOrganizer(false);
        const QString icsPath = dir->filePath(QStringLiteral("external_cal.ics"));
        const int nowMin = QTime::currentTime().hour() * 60 + QTime::currentTime().minute();
        const int baseMin = qMax(0, qMin(nowMin, 23 * 60));
        const int extStartMin = qMax(0, baseMin - 15);
        const int extEndMin = qMin(23 * 60 + 59, baseMin + 45);
        {
            QFile f(icsPath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            const QString dtStart = QDateTime(today, QTime(extStartMin / 60, extStartMin % 60)).toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
            const QString dtEnd = QDateTime(today, QTime(extEndMin / 60, extEndMin % 60)).toString(QStringLiteral("yyyyMMdd'T'HHmmss"));
            const QByteArray body = QStringLiteral(
                "BEGIN:VCALENDAR\r\n"
                "VERSION:2.0\r\n"
                "PRODID:-//Test//External//EN\r\n"
                "BEGIN:VEVENT\r\n"
                "UID:ext-ev-1\r\n"
                "SUMMARY:Reunião Externa KOrganizer\r\n"
                "LOCATION:Sala 2\r\n"
                "DTSTART:%1\r\n"
                "DTEND:%2\r\n"
                "END:VEVENT\r\n"
                "END:VCALENDAR\r\n").arg(dtStart, dtEnd).toUtf8();
            f.write(body);
        }
        settings->setExtraIcsPaths({icsPath});

        RoutineManager rm(db.get());
        FocusManager fm(store.get(), settings.get());
        AnalyticsManager am(store.get(), &rm, &fm);
        SummaryManager sm(store.get(), &rm, &am, &fm, settings.get());
        JournalManager jmSummary(db.get());
        sm.setJournalManager(&jmSummary);
        QVERIFY(!sm.active());
        sm.setActive(true);
        QVERIFY(!sm.isDirty());
        QVERIFY(sm.greetingText().contains(QStringLiteral("Alex")));
        QVERIFY(!sm.weekdayText().isEmpty());
        QVERIFY(!sm.fullDateText().isEmpty());
        QVERIFY(!sm.fullDateText().contains(sm.weekdayText(), Qt::CaseInsensitive));
        QCOMPARE(sm.stats().value("todayCount").toInt(), 2);
        QCOMPARE(sm.stats().value("overdueCount").toInt(), 1);
        QCOMPARE(sm.stats().value("q1Count").toInt(), 1);
        QCOMPARE(sm.externalEvents().size(), 1);
        QCOMPARE(sm.externalEvents().first().toMap().value("title").toString(), QStringLiteral("Reunião Externa KOrganizer"));
        QCOMPARE(rm.allBlocks().size(), 0); // external events never pollute RoutineManager!

        // 4. Conflict detection between Routine & External event + Prep Task creation + Journal quick entry + Labeled Pomodoro
        RoutineBlock rb;
        rb.title = QStringLiteral("Foco Vespertino");
        rb.daysMask = (1 << (today.dayOfWeek() - 1));
        rb.startMinute = baseMin;
        rb.durationMinutes = 60;
        rb.active = true;
        QVERIFY(rm.addBlock({
                    {QStringLiteral("title"), rb.title},
                    {QStringLiteral("daysMask"), rb.daysMask},
                    {QStringLiteral("startMinute"), rb.startMinute},
                    {QStringLiteral("durationMinutes"), rb.durationMinutes},
                    {QStringLiteral("active"), true},
                })
                > 0);
        QVERIFY(sm.externalEvents().first().toMap().value("hasConflict").toBool());
        QVERIFY(sm.todayRoutines().first().toMap().value("hasConflict").toBool());
        QVERIFY(sm.nextSchedule().value("isConflict").toBool());

        const qint64 prepId = sm.createPrepTaskFromEvent(QStringLiteral("Reunião Externa KOrganizer"), today.toString(Qt::ISODate), QStringLiteral("14:00"), QStringLiteral("external_cal"));
        QVERIFY(prepId > 0);
        QVERIFY(store->task(prepId)->title.contains(QStringLiteral("Reunião Externa KOrganizer")));

        const qint64 jId = sm.openTodayJournal();
        QVERIFY(jId > 0);
        QCOMPARE(sm.recentJournals().size(), 1);
        QCOMPARE(sm.openTodayJournal(), jId); // reuses today's entry on second call

        fm.startPomodoroLabeled(QStringLiteral("Reunião Externa KOrganizer"));
        QCOMPARE(fm.activeTaskTitle(), QStringLiteral("Reunião Externa KOrganizer"));
        fm.discard();

        // 5. PDF export separates stat pills and writes valid PDF output
        TaskModel pdfTaskModel(store.get(), settings.get());
        PdfExporter pdf(store.get(), &pdfTaskModel, &rm, &jmSummary);
        const QString routineHtml = pdf.buildHtml({{QStringLiteral("scope"), QStringLiteral("routine")}});
        QVERIFY(routineHtml.contains(QStringLiteral("<td class=\"stat-gap\"></td>")));
        QVERIFY(!routineHtml.contains(QStringLiteral("</span><span class=\"stat-pill\">")));
        const QString pdfPath = dir->filePath(QStringLiteral("routine_test.pdf"));
        QVERIFY(pdf.exportPdf(QUrl::fromLocalFile(pdfPath), {{QStringLiteral("scope"), QStringLiteral("routine")}}));
        QFile pdfFile(pdfPath);
        QVERIFY(pdfFile.open(QIODevice::ReadOnly));
        QVERIFY(pdfFile.read(5).startsWith("%PDF-"));
    }

    void routineNavigationAndForwardOnlyRecurrence()
    {
        RoutineManager rm(db.get());
        rm.setSelectedDateIso(QStringLiteral("2026-11-22"));
        const QString novTitle = rm.monthTitle();
        const QString novWeek = rm.weekTitle();

        QSignalSpy dateSpy(&rm, &RoutineManager::selectedDateChanged);
        rm.shiftMonths(-1);
        QCOMPARE(dateSpy.count(), 1);
        QCOMPARE(rm.selectedDateIso(), QStringLiteral("2026-10-22"));
        QVERIFY(rm.monthTitle() != novTitle);

        rm.shiftWeeks(-1);
        QCOMPARE(dateSpy.count(), 2);
        QVERIFY(rm.weekTitle() != novWeek);

        // Forward-only recurring template anchored at 2026-11-22 must NOT backfill past months/days
        rm.setSelectedDateIso(QStringLiteral("2026-11-22"));
        const qint64 bid = rm.addBlock({
            {QStringLiteral("title"), QStringLiteral("Leitura Matinal")},
            {QStringLiteral("startMinute"), 420},
            {QStringLiteral("durationMinutes"), 30},
            {QStringLiteral("isTemplate"), true},
            {QStringLiteral("daysMask"), 127},
            {QStringLiteral("startDate"), QStringLiteral("2026-11-22")},
        });
        QVERIFY(bid > 0);
        QCOMPARE(rm.blockInfo(bid).value(QStringLiteral("startDate")).toString(), QStringLiteral("2026-11-22"));

        QVERIFY(rm.blocksForDate(QDate(2025, 11, 22)).isEmpty());
        QVERIFY(rm.blocksForDate(QDate(2026, 10, 22)).isEmpty());
        QVERIFY(rm.blocksForDate(QDate(2026, 11, 21)).isEmpty());
        QCOMPARE(rm.blocksForDate(QDate(2026, 11, 22)).size(), 1);
        QCOMPARE(rm.blocksForDate(QDate(2026, 12, 15)).size(), 1);

        const QVariantList cells = rm.monthCells();
        for (const QVariant &v : cells) {
            const QVariantMap cell = v.toMap();
            const QDate cd = QDate::fromString(cell.value(QStringLiteral("iso")).toString(), Qt::ISODate);
            const QVariantList entries = cell.value(QStringLiteral("entries")).toList();
            if (cd < QDate(2026, 11, 22)) {
                QVERIFY(entries.isEmpty());
            } else {
                QCOMPARE(entries.size(), 1);
            }
        }
    }

    void guardedDialogOutsideClickAndEsc()
    {
        KLocalizedString::setApplicationDomain("ktasks");
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedQmlContext(&engine));
        const QString guardedPath = QStringLiteral(KTASKS_SOURCE_DIR "/src/app/qml/GuardedDialog.qml");
        QVERIFY(QFile::exists(guardedPath));

        const QByteArray qmlCode = QStringLiteral(
            "import QtQuick\n"
            "import QtQuick.Controls as QQC2\n"
            "import org.kde.kirigami as Kirigami\n"
            "import \"%1\"\n"
            "Kirigami.ApplicationWindow {\n"
            "    id: win\n"
            "    width: 800\n"
            "    height: 600\n"
            "    visible: true\n"
            "    property alias dialog: dlg\n"
            "    GuardedDialog {\n"
            "        id: dlg\n"
            "        dirty: false\n"
            "        preferredWidth: 300\n"
            "        preferredHeight: 200\n"
            "        standardButtons: Kirigami.Dialog.Cancel\n"
            "        QQC2.TextField { focus: true }\n"
            "    }\n"
            "}\n")
            .arg(QUrl::fromLocalFile(QStringLiteral(KTASKS_SOURCE_DIR "/src/app/qml")).toString())
            .toUtf8();
        QQmlComponent comp(&engine);
        comp.setData(qmlCode, QUrl::fromLocalFile(QStringLiteral(KTASKS_SOURCE_DIR "/src/app/qml/test_host.qml")));
        QVERIFY2(comp.isReady(), qPrintable(comp.errorString()));
        std::unique_ptr<QObject> rootObj(comp.create());
        auto *win = qobject_cast<QQuickWindow *>(rootObj.get());
        QVERIFY(win != nullptr);
        auto *dlg = win->property("dialog").value<QObject *>();
        QVERIFY(dlg != nullptr);

        // 1. Clean dialog: Esc closes immediately with no prompt
        QMetaObject::invokeMethod(dlg, "open");
        QTRY_VERIFY(dlg->property("opened").toBool());
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(!dlg->property("opened").toBool());

        // 2. Clean dialog: outside click closes immediately with no prompt
        QMetaObject::invokeMethod(dlg, "open");
        QTRY_VERIFY(dlg->property("opened").toBool());
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTRY_VERIFY(!dlg->property("opened").toBool());

        // 3. Dirty dialog: outside click keeps dialog open and opens confirmation prompt
        dlg->setProperty("dirty", true);
        QMetaObject::invokeMethod(dlg, "open");
        QTRY_VERIFY(dlg->property("opened").toBool());
        QTest::mouseClick(win, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTRY_VERIFY(dlg->property("opened").toBool());
        auto *prompt = dlg->findChild<QObject *>(QStringLiteral("unsavedPrompt"));
        QVERIFY(prompt != nullptr);
        QTRY_VERIFY(prompt->property("opened").toBool());

        // 4. Dirty dialog: Escape closes prompt first, next Escape keeps dirty dialog open and reopens prompt
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(!prompt->property("visible").toBool());
        QVERIFY(dlg->property("opened").toBool());
        QTest::keyClick(win, Qt::Key_Escape);
        QTRY_VERIFY(prompt->property("opened").toBool());
        QVERIFY(dlg->property("opened").toBool());

        // 5. Force close cleanly dismisses dirty dialog
        QMetaObject::invokeMethod(prompt, "close");
        QMetaObject::invokeMethod(dlg, "forceClose");
        QTRY_VERIFY(!dlg->property("opened").toBool());
    }

    void routineAvailableMinutesAndOverlaps()
    {
        RoutineManager rm(db.get());
        const QDate today = QDate::currentDate();
        const QDate tomorrow = today.addDays(1);
        const QDate yesterday = today.addDays(-1);

        // Past block on today, ongoing overlapping blocks, and a skipped evening block
        rm.setSelectedDateIso(today.toString(Qt::ISODate));
        rm.addBlock({
            {QStringLiteral("title"), QStringLiteral("Manhã passada")},
            {QStringLiteral("startMinute"), 8 * 60},
            {QStringLiteral("durationMinutes"), 120},
            {QStringLiteral("isTemplate"), false},
            {QStringLiteral("specificDate"), today.toString(Qt::ISODate)},
        });
        rm.addBlock({
            {QStringLiteral("title"), QStringLiteral("Bloco em curso")},
            {QStringLiteral("startMinute"), 11 * 60 + 30},
            {QStringLiteral("durationMinutes"), 90}, // 11:30 - 13:00
            {QStringLiteral("isTemplate"), false},
            {QStringLiteral("specificDate"), today.toString(Qt::ISODate)},
        });
        rm.addBlock({
            {QStringLiteral("title"), QStringLiteral("Sobreposto")},
            {QStringLiteral("startMinute"), 12 * 60 + 30},
            {QStringLiteral("durationMinutes"), 90}, // 12:30 - 14:00 (merged 12:00..14:00 = 120m left)
            {QStringLiteral("isTemplate"), false},
            {QStringLiteral("specificDate"), today.toString(Qt::ISODate)},
        });
        const qint64 skippedId = rm.addBlock({
            {QStringLiteral("title"), QStringLiteral("Pulado")},
            {QStringLiteral("startMinute"), 20 * 60},
            {QStringLiteral("durationMinutes"), 60},
            {QStringLiteral("isTemplate"), false},
            {QStringLiteral("specificDate"), today.toString(Qt::ISODate)},
        });
        rm.setBlockStatusForDate(skippedId, today, int(RoutineLogStatus::Skipped));

        // At 12:00 (min 720): 720m left in day minus 120m merged [12:00, 14:00] = 600m
        QCOMPARE(rm.availableMinutesForDateAndMinute(today, 12 * 60), 600);

        // Future date counts whole 1440m day minus planned non-skipped intervals
        rm.setSelectedDateIso(tomorrow.toString(Qt::ISODate));
        QCOMPARE(rm.availableMinutesForDateAndMinute(today, 12 * 60), 1440);
        rm.addBlock({
            {QStringLiteral("title"), QStringLiteral("Reunião amanhã")},
            {QStringLiteral("startMinute"), 9 * 60},
            {QStringLiteral("durationMinutes"), 120},
            {QStringLiteral("isTemplate"), false},
            {QStringLiteral("specificDate"), tomorrow.toString(Qt::ISODate)},
        });
        QCOMPARE(rm.availableMinutesForDateAndMinute(today, 12 * 60), 1320);

        // Past date has 0 remaining available minutes
        rm.setSelectedDateIso(yesterday.toString(Qt::ISODate));
        QVERIFY(rm.isPast());
        QCOMPARE(rm.availableMinutesForDateAndMinute(today, 12 * 60), 0);
    }

    void focusDndAndDiscordPrivacy()
    {
        settings->setNotifyFocus(false);
        settings->setPomodoroFocusMinutes(25);
        settings->setPomodoroShortBreakMinutes(5);
        settings->setFocusDoNotDisturb(true);
        settings->setDiscordPresenceEnabled(true);
        settings->setDiscordShowTaskTitle(false);
        settings->setDiscordLargeImage(QStringLiteral("ktasks"));

        const qint64 privTaskId = add(QStringLiteral("Segredo Médico Confidencial"));
        FocusManager fm(store.get(), settings.get());

        // 1. Do Not Disturb toggles across Focus / Pause / Break / Complete / Setting off
        QVERIFY(!fm.dndActive());
        fm.startPomodoro(privTaskId);
        QVERIFY(fm.dndActive());
        fm.togglePause();
        QVERIFY(!fm.dndActive());
        fm.togglePause();
        QVERIFY(fm.dndActive());
        fm.startBreak(false);
        QVERIFY(!fm.dndActive());
        fm.startStopwatch(privTaskId);
        QVERIFY(fm.dndActive());
        settings->setFocusDoNotDisturb(false);
        QVERIFY(!fm.dndActive());
        settings->setFocusDoNotDisturb(true);
        QVERIFY(fm.dndActive());
        fm.discard();
        QVERIFY(!fm.dndActive());

        // 2. Completing a Pomodoro clears DND cleanly before completion notification
        fm.startPomodoro(privTaskId);
        QVERIFY(fm.dndActive());
        fm.advanceSecondsForTesting(25 * 60);
        QVERIFY(!fm.hasSession());
        QVERIFY(!fm.dndActive());

        // 3. Discord Rich Presence omits private task title when showTaskTitle == false
        DiscordPresence::ActivityState act;
        act.hasSession = true;
        act.running = true;
        act.mode = FocusMode::Pomodoro;
        act.cycleNumber = 2;
        act.elapsedSeconds = 300;
        act.remainingSeconds = 1200;
        act.taskTitle = QStringLiteral("Segredo Médico Confidencial");

        const QJsonObject privPayload = DiscordPresence::buildSetActivityPayload(42, act, false, QStringLiteral("ktasks"), 1700000000, QStringLiteral("n1"));
        const QByteArray privJson = QJsonDocument(privPayload).toJson(QJsonDocument::Compact);
        QVERIFY(!privJson.contains("Segredo"));
        QVERIFY(!privJson.contains("Confidencial"));
        const QJsonObject activityObj = privPayload.value(QStringLiteral("args")).toObject().value(QStringLiteral("activity")).toObject();
        QCOMPARE(activityObj.value(QStringLiteral("assets")).toObject().value(QStringLiteral("large_image")).toString(), QStringLiteral("ktasks"));
        QCOMPARE(activityObj.value(QStringLiteral("assets")).toObject().value(QStringLiteral("large_text")).toString(), QStringLiteral("KTasks"));
        QCOMPARE(activityObj.value(QStringLiteral("timestamps")).toObject().value(QStringLiteral("end")).toInteger(), qint64(1700001200));

        // Explicit opt-in includes title when user enables it
        const QJsonObject pubPayload = DiscordPresence::buildSetActivityPayload(42, act, true, QStringLiteral("ktasks"), 1700000000, QStringLiteral("n2"));
        const QByteArray pubJson = QJsonDocument(pubPayload).toJson(QJsonDocument::Compact);
        QVERIFY(pubJson.contains("Segredo Médico Confidencial"));

        // Wire frame encode/decode & Client ID validation
        const QByteArray frame = DiscordPresence::encodeFrame(1, QByteArrayLiteral("{\"ok\":true}"));
        quint32 op = 99, len = 0;
        QVERIFY(DiscordPresence::decodeFrameHeader(frame, op, len));
        QCOMPARE(op, quint32(1));
        QCOMPARE(len, quint32(11));
        QVERIFY(DiscordPresence::isValidClientId(QStringLiteral("123456789012345678")));
        QVERIFY(!DiscordPresence::isValidClientId(QStringLiteral("abc123")));
        QVERIFY(!DiscordPresence::candidateSocketPaths().isEmpty());
    }

    void settingsAndRoutineQmlLoadCleanly()
    {
        KLocalizedString::setApplicationDomain("ktasks");
        Application app(db.get());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextObject(new KLocalizedQmlContext(&engine));
        qmlRegisterSingletonInstance("app.ktasks.backend", 1, 0, "App", &app);
        qmlRegisterUncreatableType<TaskModel>("app.ktasks.backend", 1, 0, "TaskModel", QStringLiteral("enums only"));

        QQmlComponent settingsComp(&engine, QUrl::fromLocalFile(QStringLiteral(KTASKS_SOURCE_DIR "/src/app/qml/SettingsPage.qml")));
        QVERIFY2(settingsComp.isReady(), qPrintable(settingsComp.errorString()));
        std::unique_ptr<QObject> settingsObj(settingsComp.create());
        QVERIFY(settingsObj != nullptr);

        QQmlComponent routineComp(&engine, QUrl::fromLocalFile(QStringLiteral(KTASKS_SOURCE_DIR "/src/app/qml/RoutinePage.qml")));
        QVERIFY2(routineComp.isReady(), qPrintable(routineComp.errorString()));
        std::unique_ptr<QObject> routineObj(routineComp.create());
        QVERIFY(routineObj != nullptr);
    }
};

QTEST_MAIN(AppTest)
#include "apptest.moc"
