// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"
#include "crypto.h"

#include <QDir>
#include <QTemporaryDir>
#include <QTest>

using namespace KTasks;

class DatabaseTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void init()
    {
        Database::setExtraMigrationsForTesting({});
    }

    void createsSchema()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QString err;
        QVERIFY2(db.open(&err), qPrintable(err));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(db.lists().isEmpty());
    }

    void taskRoundTrip()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        TaskList l;
        l.name = "Casa";
        QVERIFY(db.insertList(l));
        QVERIFY(l.id > 0);

        Task t;
        t.listId = l.id;
        t.title = "Pagar boleto";
        t.notes = "linha 1\nlinha 2";
        t.priority = 1;
        t.allDay = false;
        t.due = QDateTime(QDate(2026, 12, 24), QTime(14, 30), QTimeZone::UTC);
        t.tags = {"finanças", "casa"};
        t.rrule = "FREQ=MONTHLY";
        t.alarms = {Alarm{Alarm::RelativeToDue, {}, -900}, Alarm{Alarm::Absolute, QDateTime(QDate(2026, 12, 20), QTime(8, 0), QTimeZone::UTC), 0}};
        QVERIFY(db.insertTask(t));
        QVERIFY(!t.uid.isEmpty());

        const auto got = db.task(t.id);
        QVERIFY(got.has_value());
        QCOMPARE(got->title, t.title);
        QCOMPARE(got->notes, t.notes);
        QCOMPARE(got->due, t.due);
        QCOMPARE(got->allDay, false);
        QCOMPARE(got->tags, t.tags);
        QCOMPARE(got->alarms, t.alarms);
        QCOMPARE(got->rrule, t.rrule);
        QCOMPARE(got->syncState, SyncState::New);
    }

    void allDayDatesKeepTheirDay()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        Task t;
        t.listId = 1;
        t.allDay = true;
        t.due = QDateTime(QDate(2026, 3, 1), QTime(0, 0));
        QVERIFY(db.insertTask(t));
        QCOMPARE(db.task(t.id)->due.date(), QDate(2026, 3, 1));
    }

    void userUpdateDerivesSyncState()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        Task t;
        t.listId = 1;
        t.title = "a";
        QVERIFY(db.insertTask(t));
        t.title = "b";
        QVERIFY(db.updateTaskUser(t));
        QCOMPARE(db.task(t.id)->syncState, SyncState::New); // never uploaded
        QCOMPARE(db.task(t.id)->rev, 1);

        QVERIFY(db.markUploaded(t.id, 1, "/dav/x.ics", "\"e1\"", "ICS"));
        QCOMPARE(db.task(t.id)->syncState, SyncState::Synced);
        t.title = "c";
        QVERIFY(db.updateTaskUser(t));
        const auto got = db.task(t.id);
        QCOMPARE(got->syncState, SyncState::Modified);
        QCOMPARE(got->href, QString("/dav/x.ics")); // sync columns untouched by user edits
        QCOMPARE(got->etag, QString("\"e1\""));
    }

    void uploadDoesNotHideConcurrentEdit()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        Task t;
        t.listId = 1;
        QVERIFY(db.insertTask(t));
        const qint64 revAtUpload = db.task(t.id)->rev;
        t.title = "edited while uploading";
        QVERIFY(db.updateTaskUser(t));
        QVERIFY(db.markUploaded(t.id, revAtUpload, "/h", "e", "ICS"));
        QCOMPARE(db.task(t.id)->syncState, SyncState::Modified);
    }

    void deleteLeavesTombstoneOnlyForUploadedTasks()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        Task local;
        local.listId = 1;
        QVERIFY(db.insertTask(local));
        Task remote;
        remote.listId = 1;
        remote.href = "/h.ics";
        remote.syncState = SyncState::Synced;
        QVERIFY(db.insertTask(remote));

        QVERIFY(db.removeTaskUser(local.id));
        QVERIFY(db.removeTaskUser(remote.id));
        QVERIFY(!db.task(local.id).has_value());
        QCOMPARE(db.task(remote.id)->syncState, SyncState::Deleted);
        QVERIFY(db.tasks(false).isEmpty());
        QCOMPARE(db.tasks(true).size(), 1);
    }

    void moveLeavesTombstoneInOldList()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        Task t;
        t.listId = 1;
        t.href = "/a/x.ics";
        t.etag = "e";
        t.syncState = SyncState::Synced;
        QVERIFY(db.insertTask(t));
        QVERIFY(db.moveTask(t.id, 2));
        const auto moved = db.task(t.id);
        QCOMPARE(moved->listId, 2);
        QCOMPARE(moved->syncState, SyncState::New);
        QVERIFY(moved->href.isEmpty());
        const auto old = db.tasksInList(1);
        QCOMPARE(old.size(), 1);
        QCOMPARE(old.first().syncState, SyncState::Deleted);
        QCOMPARE(old.first().href, QString("/a/x.ics"));
    }

    void transactionRollsBack()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        {
            Database::Transaction tx(&db);
            Task t;
            t.listId = 1;
            QVERIFY(db.insertTask(t));
        }
        QVERIFY(db.tasks().isEmpty());
        {
            Database::Transaction tx(&db);
            Task t;
            t.listId = 1;
            QVERIFY(db.insertTask(t));
            QVERIFY(tx.commit());
        }
        QCOMPARE(db.tasks().size(), 1);
    }

    void migrationBacksUpAndUpgrades()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.db");
        {
            Database db(path);
            QVERIFY(db.open());
            Task t;
            t.listId = 1;
            t.title = "survivor";
            QVERIFY(db.insertTask(t));
        }
        const int baseVersion = Database::latestSchemaVersion();
        Database::setExtraMigrationsForTesting({"ALTER TABLE tasks ADD COLUMN extra TEXT NOT NULL DEFAULT 'x'"});
        {
            Database db(path);
            QString err;
            QVERIFY2(db.open(&err), qPrintable(err));
            QCOMPARE(db.schemaVersion(), baseVersion + 1);
            QCOMPARE(db.tasks().size(), 1);
            QCOMPARE(db.tasks().first().title, QString("survivor"));
        }
        const QStringList backups = QDir(dir.filePath("backups")).entryList({"*.db"});
        QCOMPARE(backups.size(), 1);
        QVERIFY(backups.first().contains(QString("-v%1-").arg(baseVersion)));
    }

    void failedMigrationLeavesDataIntact()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.db");
        {
            Database db(path);
            QVERIFY(db.open());
        }
        Database::setExtraMigrationsForTesting({"ALTER TABLE tasks ADD COLUMN ok1 TEXT; THIS IS NOT SQL"});
        {
            Database db(path);
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression("statement failed.*"));
            QVERIFY(!db.open());
        }
        Database::setExtraMigrationsForTesting({});
        Database db(path);
        QVERIFY(db.open());
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
    }

    void refusesNewerSchema()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.db");
        Database::setExtraMigrationsForTesting({"CREATE TABLE future (x)"});
        {
            Database db(path);
            QVERIFY(db.open());
        }
        Database::setExtraMigrationsForTesting({});
        Database db(path);
        QVERIFY(!db.open());
    }

    void twoConnectionsSeeEachOther()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("a.db");
        Database a(path), b(path);
        QVERIFY(a.open());
        QVERIFY(b.open());
        Task t;
        t.listId = 1;
        QVERIFY(a.insertTask(t));
        QCOMPARE(b.tasks().size(), 1);
    }

    void remindersAndMeta()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("a.db"));
        QVERIFY(db.open());
        QVERIFY(!db.reminderFired("u", "k"));
        db.setReminderFired("u", "k");
        QVERIFY(db.reminderFired("u", "k"));
        const QDateTime until = QDateTime(QDate(2030, 1, 1), QTime(10, 0), QTimeZone::UTC);
        db.setSnooze("u", until);
        QCOMPARE(db.snoozedUntil("u"), until);
        db.clearSnooze("u");
        QVERIFY(!db.snoozedUntil("u").isValid());
        db.setMeta("a", "1");
        QCOMPARE(db.meta("a"), QString("1"));
        QCOMPARE(db.meta("missing", "d"), QString("d"));
    }

    void cryptoSealAndDecrypt()
    {
        Crypto::SealedData sealed;
        QVERIFY(Crypto::seal("Segredo do diário 🔐", "senha123", sealed));
        QVERIFY(Crypto::verifyPassword("senha123", sealed.salt, sealed.passwordHash));
        QVERIFY(!Crypto::verifyPassword("errada", sealed.salt, sealed.passwordHash));
        QString plain;
        QVERIFY(Crypto::decryptText(sealed.ciphertext, "senha123", sealed.salt, plain));
        QCOMPARE(plain, QString("Segredo do diário 🔐"));
        QVERIFY(!Crypto::decryptText(sealed.ciphertext, "errada", sealed.salt, plain));
    }

    void journalRoutineAndBackupRoundTrip()
    {
        QTemporaryDir dir;
        const QString dbPath = dir.filePath("a.db");
        Database db(dbPath);
        QVERIFY(db.open());

        JournalNotebook personalNb;
        personalNb.name = "Pessoal";
        QVERIFY(db.insertJournalNotebook(personalNb));
        JournalNotebook workNb;
        workNb.name = "Trabalho";
        workNb.color = "#e74c3c";
        QVERIFY(db.insertJournalNotebook(workNb));
        QVERIFY(workNb.id > 0);
        workNb.name = "Projetos";
        QVERIFY(db.updateJournalNotebook(workNb));
        QCOMPARE(db.journalNotebooks().size(), 2);

        JournalEntry je;
        je.notebookId = workNb.id;
        je.title = "Dia produtivo";
        je.content = "**Resumo** do dia";
        je.entryDate = QDate(2026, 11, 21);
        je.tags = {"pessoal"};
        QVERIFY(db.insertJournalEntry(je));
        QCOMPARE(db.journalEntries().size(), 1);
        QCOMPARE(db.journalEntries().first().notebookId, workNb.id);

        RoutineBlock rb;
        rb.title = "Leitura";
        rb.startMinute = 21 * 60;
        rb.durationMinutes = 30;
        rb.daysMask = 127;
        QVERIFY(db.insertRoutineBlock(rb));
        QCOMPARE(db.routineBlocks().size(), 1);
        QVERIFY(db.setRoutineLog(rb.id, QDate(2026, 11, 21), RoutineLogStatus::Done));
        QCOMPARE(db.routineLogsForDate(QDate(2026, 11, 21)).size(), 1);

        const QString backupFile = dir.filePath("snap.db");
        QString err;
        QVERIFY2(db.backupTo(backupFile, &err), qPrintable(err));
        QVERIFY(Database::validateBackup(backupFile, &err));

        // Mutate live DB (removing notebook removes its entries too), then restore from snapshot
        QVERIFY(db.removeJournalNotebook(workNb.id));
        QCOMPARE(db.journalNotebooks().size(), 1);
        QVERIFY(db.journalEntries().isEmpty());
        QVERIFY2(db.restoreFrom(backupFile, &err), qPrintable(err));
        QCOMPARE(db.journalNotebooks().size(), 2);
        QCOMPARE(db.journalEntries().size(), 1);
        QCOMPARE(db.journalEntries().first().title, QString("Dia produtivo"));
        QCOMPARE(db.journalEntries().first().notebookId, workNb.id);
        QCOMPARE(db.routineBlocks().size(), 1);
    }

    void routineSyncColumnsAndMoveList()
    {
        QTemporaryDir dir;
        Database db(dir.filePath("move.db"));
        QVERIFY(db.open());

        Account nc;
        nc.name = "u@cloud";
        nc.serverUrl = "https://cloud.example.com";
        nc.username = "u";
        QVERIFY(db.insertAccount(nc));

        TaskList localList;
        localList.name = "Inbox";
        QVERIFY(db.insertList(localList));
        Task t;
        t.listId = localList.id;
        t.title = "Tarefa migrada";
        QVERIFY(db.insertTask(t));

        // Local -> Nextcloud
        QVERIFY(db.moveListToAccount(localList.id, nc.id));
        QCOMPARE(db.list(localList.id)->accountId, nc.id);
        QCOMPARE(db.tasksInList(localList.id, true).first().syncState, SyncState::New);

        // Routine block v5 sync metadata & remote-delete queue
        RoutineBlock rb;
        rb.title = "Meditação";
        rb.startMinute = 420;
        rb.durationMinutes = 20;
        rb.daysMask = 127;
        rb.syncAccountId = nc.id;
        rb.syncState = SyncState::New;
        QVERIFY(db.insertRoutineBlock(rb));
        QVERIFY(db.hasPendingRoutineChanges(nc.id));
        QVERIFY(db.markRoutineUploaded(rb.id, rb.rev, "/dav/rotina/", "/dav/rotina/r1.ics", "\"e1\"", "ICS"));
        QVERIFY(!db.hasPendingRoutineChanges(nc.id));
        QVERIFY(db.routineBlockByHref(nc.id, "/dav/rotina/r1.ics").has_value());

        QVERIFY(db.queueRoutineRemoteDelete(nc.id, "/dav/rotina/r1.ics", "\"e1\""));
        QCOMPARE(db.routineRemoteDeletes(nc.id).size(), 1);
        QVERIFY(db.removeAccount(nc.id));
        QVERIFY(db.routineRemoteDeletes(nc.id).isEmpty());
        QCOMPARE(db.routineBlocks().first().syncAccountId, qint64(0));

        // Schema v6: quadrant persists across insert/update
        TaskList l2;
        l2.name = "Work";
        QVERIFY(db.insertList(l2));
        Task qtask;
        qtask.listId = l2.id;
        qtask.title = "Quadrante Q1";
        qtask.quadrant = 1;
        qtask.priority = Task::priorityForQuadrant(1);
        QVERIFY(db.insertTask(qtask));
        QCOMPARE(db.task(qtask.id)->quadrant, 1);
        qtask.quadrant = 4;
        qtask.priority = 0;
        qtask.estimatedMinutes = 30;
        qtask.trackedSeconds = 900;
        qtask.pomodorosCompleted = 1;
        QVERIFY(db.updateTaskUser(qtask));
        QCOMPARE(db.task(qtask.id)->quadrant, 4);
        QCOMPARE(db.task(qtask.id)->estimatedMinutes, 30);
        QCOMPARE(db.task(qtask.id)->trackedSeconds, 900);
        QCOMPARE(db.task(qtask.id)->pomodorosCompleted, 1);

        // Schema v7: focus sessions round-trip
        FocusSession fs;
        fs.taskId = qtask.id;
        fs.taskTitle = qtask.title;
        fs.listId = l2.id;
        fs.quadrant = 1;
        fs.mode = FocusMode::Pomodoro;
        fs.durationSeconds = 1500;
        fs.completedPomodoro = true;
        QVERIFY(db.insertFocusSession(fs));
        QVERIFY(fs.id > 0);
        QCOMPARE(db.focusSessions().size(), 1);
        QCOMPARE(db.focusSessions().first().durationSeconds, 1500);
        QVERIFY(db.removeFocusSession(fs.id));
        QVERIFY(db.focusSessions().isEmpty());

        // Schema v8: RoutineBlock.startDate persists across insert and update
        RoutineBlock datedRb;
        datedRb.title = "Inglês diário";
        datedRb.startMinute = 480;
        datedRb.durationMinutes = 45;
        datedRb.daysMask = 31;
        datedRb.startDate = QDate(2026, 11, 22);
        QVERIFY(db.insertRoutineBlock(datedRb));
        const auto loadedRb = db.routineBlock(datedRb.id);
        QVERIFY(loadedRb.has_value());
        QCOMPARE(loadedRb->startDate, QDate(2026, 11, 22));

        RoutineBlock movedStart = *loadedRb;
        movedStart.startDate = QDate(2026, 12, 1);
        QVERIFY(db.updateRoutineBlockUser(movedStart));
        QCOMPARE(db.routineBlock(datedRb.id)->startDate, QDate(2026, 12, 1));
    }
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "databasetest.moc"
