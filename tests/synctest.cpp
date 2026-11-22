// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"
#include "icscodec.h"
#include "mockcaldavserver.h"
#include "syncengine.h"

#include <QTemporaryDir>
#include <QTest>

using namespace KTasks;

static QString todoIcs(const QString &uid, const QString &summary, const QString &extra = QString())
{
    return QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Other client//EN\r\nBEGIN:VTODO\r\nUID:%1\r\nDTSTAMP:20260101T000000Z\r\n"
                          "CREATED:20260101T000000Z\r\nLAST-MODIFIED:20260101T000000Z\r\nSUMMARY:%2\r\n%3END:VTODO\r\nEND:VCALENDAR\r\n")
        .arg(uid, summary, extra);
}

static QString eventIcs(const QString &uid)
{
    return QStringLiteral("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:x\r\nBEGIN:VEVENT\r\nUID:%1\r\nDTSTAMP:20260101T000000Z\r\nDTSTART:20260101T100000Z\r\n"
                          "SUMMARY:evento\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n")
        .arg(uid);
}

class SyncTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<MockCalDavServer> server;
    std::unique_ptr<Database> db;
    std::unique_ptr<SyncEngine> engine;
    qint64 accountId = 0;
    QString tasksHref;

    SyncEngine::Result sync()
    {
        return engine->syncAccount(accountId, server->password);
    }
    TaskList onlyList()
    {
        const auto lists = db->lists();
        return lists.isEmpty() ? TaskList() : lists.first();
    }
    Task taskTitled(const QString &title)
    {
        for (const Task &t : db->tasks()) {
            if (t.title == title) {
                return t;
            }
        }
        return Task();
    }
    QString serverSummaries(const QString &col)
    {
        QStringList out;
        for (const auto &item : std::as_const(server->collections[col].items)) {
            if (auto t = IcsCodec::decode(item.ics)) {
                out << t->title;
            }
        }
        out.sort();
        return out.join(QLatin1Char('|'));
    }

private Q_SLOTS:
    void init()
    {
        dir = std::make_unique<QTemporaryDir>();
        server = std::make_unique<MockCalDavServer>();
        QVERIFY(server->isListening());
        tasksHref = server->addCollection("tasks", "Tarefas");
        db = std::make_unique<Database>(dir->filePath("a.db"));
        QVERIFY(db->open());
        Account a;
        a.name = "Nextcloud";
        a.serverUrl = server->url().toString();
        a.username = server->user;
        QVERIFY(db->insertAccount(a));
        accountId = a.id;
        engine = std::make_unique<SyncEngine>(dir->filePath("a.db"));
        engine->setTimeoutMs(5000);
        engine->setConflictSuffix("conflito");
    }

    void cleanup()
    {
        engine.reset();
        db.reset();
        server.reset();
        dir.reset();
    }

    void discoversAndPullsOnlyTodos()
    {
        server->addCollection("events", "Agenda", {"VEVENT"});
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Comprar leite"));
        server->putItem(tasksHref, "b.ics", todoIcs("b", "Pagar boleto"));
        server->putItem(tasksHref, "e.ics", eventIcs("e"));

        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pulled, 2);
        QCOMPARE(db->lists().size(), 1);
        QCOMPARE(onlyList().name, QString("Tarefas"));
        QCOMPARE(db->tasks().size(), 2);
        for (const Task &t : db->tasks()) {
            QCOMPARE(t.syncState, SyncState::Synced);
            QVERIFY(!t.etag.isEmpty());
        }
        const auto acc = db->account(accountId);
        QCOMPARE(acc->homeUrl, MockCalDavServer::home);
        QVERIFY(acc->lastSync.isValid());
        QVERIFY(acc->lastError.isEmpty());
    }

    void unchangedServerCostsOneRequest()
    {
        server->putItem(tasksHref, "a.ics", todoIcs("a", "x"));
        QVERIFY(sync().ok());
        server->clearLog();
        QVERIFY(sync().ok());
        QCOMPARE(server->requests.size(), 1); // only the calendar listing (ctag check)
    }

    void pushesNewLocalTask()
    {
        QVERIFY(sync().ok());
        Task t;
        t.listId = onlyList().id;
        t.title = "Nova tarefa";
        t.allDay = true;
        t.due = QDateTime(QDate(2026, 12, 1), QTime(0, 0));
        QVERIFY(db->insertTask(t));

        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pushed, 1);
        const Task local = *db->task(t.id);
        QCOMPARE(local.syncState, SyncState::Synced);
        QVERIFY(local.href.startsWith(tasksHref));
        QVERIFY(!local.etag.isEmpty());
        const auto remote = IcsCodec::decode(server->icsOf(local.href));
        QVERIFY(remote.has_value());
        QCOMPARE(remote->title, QString("Nova tarefa"));
        QCOMPARE(remote->due.date(), QDate(2026, 12, 1));

        // The upload must not come back as a download.
        server->clearLog();
        const auto again = sync();
        QVERIFY(again.ok());
        QCOMPARE(again.pulled, 0);
        QCOMPARE(server->count("PUT"), 0);
    }

    void localEditKeepsForeignProperties()
    {
        const QString href = server->putItem(tasksHref, "a.ics", todoIcs("a", "Original", "X-OC-HIDESUBTASKS:1\r\nX-FOREIGN:keep\r\n"));
        QVERIFY(sync().ok());
        Task t = taskTitled("Original");
        t.title = "Editada";
        t.modified = QDateTime::currentDateTimeUtc();
        QVERIFY(db->updateTaskUser(t));
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        const QString ics = server->icsOf(href);
        QVERIFY(ics.contains("SUMMARY:Editada"));
        QVERIFY2(ics.contains(QRegularExpression("X-FOREIGN[^:\\n]*:keep")), qPrintable(ics));
        QVERIFY(ics.contains(QRegularExpression("X-OC-HIDESUBTASKS[^:\\n]*:1")));
        QCOMPARE(db->task(t.id)->syncState, SyncState::Synced);
    }

    void pullsRemoteEditAndDelete()
    {
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um"));
        const QString b = server->putItem(tasksHref, "b.ics", todoIcs("b", "Dois"));
        QVERIFY(sync().ok());

        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um editado"));
        server->deleteItem(b);
        server->clearLog();
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(db->tasks().size(), 1);
        QCOMPARE(db->tasks().first().title, QString("Um editado"));
        QCOMPARE(server->count("REPORT"), 2); // incremental sync-collection + one multiget
    }

    void pushesLocalDelete()
    {
        const QString href = server->putItem(tasksHref, "a.ics", todoIcs("a", "Um"));
        QVERIFY(sync().ok());
        QVERIFY(db->removeTaskUser(taskTitled("Um").id));
        QVERIFY(db->tasks().isEmpty());
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QVERIFY(server->icsOf(href).isEmpty());
        QVERIFY(db->tasks(true).isEmpty()); // tombstone gone
    }

    void mergesEditsToDifferentFields()
    {
        const QString href = server->putItem(tasksHref, "a.ics", todoIcs("a", "Título", "DESCRIPTION:notas\r\n"));
        QVERIFY(sync().ok());
        Task t = taskTitled("Título");
        t.title = "Título local";
        QVERIFY(db->updateTaskUser(t));
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Título", "DESCRIPTION:notas remotas\r\nPRIORITY:1\r\n"));

        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QVERIFY(r.conflictTitles.isEmpty());
        const Task merged = *db->task(t.id);
        QCOMPARE(merged.title, QString("Título local"));
        QCOMPARE(merged.notes, QString("notas remotas"));
        QCOMPARE(merged.priority, 1);
        QCOMPARE(merged.syncState, SyncState::Synced);
        const auto remote = IcsCodec::decode(server->icsOf(href));
        QCOMPARE(remote->title, QString("Título local"));
        QCOMPARE(remote->notes, QString("notas remotas"));
        QCOMPARE(db->tasks().size(), 1);
    }

    void conflictKeepsBothVersions()
    {
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Título"));
        QVERIFY(sync().ok());
        Task t = taskTitled("Título");
        t.title = "Versão local";
        QVERIFY(db->updateTaskUser(t));
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Versão remota"));

        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.conflictTitles, QStringList{"Versão local"});
        QCOMPARE(db->task(t.id)->title, QString("Versão remota"));
        QCOMPARE(db->tasks().size(), 2);
        const Task copy = taskTitled("Versão local (conflito)");
        QVERIFY(copy.id > 0);
        QVERIFY(copy.uid != t.uid);
        // The copy is uploaded right away (or at the latest with the next sync).
        QVERIFY(sync().ok());
        QCOMPARE(serverSummaries(tasksHref), QString("Versão local (conflito)|Versão remota"));
        QCOMPARE(db->task(copy.id)->syncState, SyncState::Synced);
    }

    void conflictDetectedOnUpload()
    {
        // The remote change arrives between our pull and our PUT: the server answers 412.
        const QString href = server->putItem(tasksHref, "a.ics", todoIcs("a", "Título", "DESCRIPTION:n\r\n"));
        QVERIFY(sync().ok());
        Task t = taskTitled("Título");
        t.title = "Local";
        QVERIFY(db->updateTaskUser(t));
        // Make the list look unchanged so the pull is skipped, then change the item behind its back.
        server->collections[tasksHref].items[href] = MockCalDavServer::Item{todoIcs("a", "Título", "DESCRIPTION:remoto\r\n"), "\"changed\""};

        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        const auto remote = IcsCodec::decode(server->icsOf(href));
        QCOMPARE(remote->title, QString("Local"));
        QCOMPARE(remote->notes, QString("remoto"));
        QCOMPARE(db->task(t.id)->syncState, SyncState::Synced);
        QCOMPARE(db->task(t.id)->notes, QString("remoto"));
    }

    void localEditBeatsRemoteDelete()
    {
        const QString href = server->putItem(tasksHref, "a.ics", todoIcs("a", "Um"));
        QVERIFY(sync().ok());
        Task t = taskTitled("Um");
        t.title = "Um editado";
        QVERIFY(db->updateTaskUser(t));
        server->deleteItem(href);
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(serverSummaries(tasksHref), QString("Um editado"));
        QCOMPARE(db->task(t.id)->syncState, SyncState::Synced);
    }

    void remoteEditBeatsLocalDelete()
    {
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um"));
        QVERIFY(sync().ok());
        const Task t = taskTitled("Um");
        QVERIFY(db->removeTaskUser(t.id));
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um editado remotamente"));
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(db->tasks().size(), 1);
        QCOMPARE(db->tasks().first().title, QString("Um editado remotamente"));
        QCOMPARE(serverSummaries(tasksHref), QString("Um editado remotamente"));
    }

    void offlineKeepsChangesQueued()
    {
        QVERIFY(sync().ok());
        Task t;
        t.listId = onlyList().id;
        t.title = "Feita offline";
        QVERIFY(db->insertTask(t));

        server->offline = true;
        const auto r = sync();
        QCOMPARE(r.kind, SyncEngine::Offline);
        QCOMPARE(db->task(t.id)->syncState, SyncState::New);
        QVERIFY(db->account(accountId)->lastError.isEmpty()); // being offline is not an error worth nagging about
        QVERIFY(db->hasPendingChanges(accountId));

        server->offline = false;
        const auto again = sync();
        QVERIFY2(again.ok(), qPrintable(again.message));
        QCOMPARE(serverSummaries(tasksHref), QString("Feita offline"));
        QVERIFY(!db->hasPendingChanges(accountId));
    }

    void connectionDropMidSyncIsRecoverable()
    {
        QVERIFY(sync().ok());
        for (int i = 0; i < 3; ++i) {
            Task t;
            t.listId = onlyList().id;
            t.title = QString("t%1").arg(i);
            QVERIFY(db->insertTask(t));
        }
        server->failNext(503, 1, "PUT");
        const auto r = sync();
        QCOMPARE(r.kind, SyncEngine::Server);
        QCOMPARE(r.pushed, 2); // the others still went through
        QVERIFY(!db->account(accountId)->lastError.isEmpty());
        const auto again = sync();
        QVERIFY2(again.ok(), qPrintable(again.message));
        QCOMPARE(again.pushed, 1);
        QCOMPARE(serverSummaries(tasksHref), QString("t0|t1|t2"));
        QVERIFY(db->account(accountId)->lastError.isEmpty());
    }

    void wrongPasswordIsAnAuthError()
    {
        const auto r = engine->syncAccount(accountId, "wrong");
        QCOMPARE(r.kind, SyncEngine::Auth);
        QVERIFY(!db->account(accountId)->lastError.isEmpty());
    }

    void staleSyncTokenFallsBackToFullComparison()
    {
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um"));
        const QString b = server->putItem(tasksHref, "b.ics", todoIcs("b", "Dois"));
        QVERIFY(sync().ok());
        server->deleteItem(b);
        server->putItem(tasksHref, "c.ics", todoIcs("c", "Três"));
        server->invalidateTokens = true;
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QStringList titles;
        for (const Task &t : db->tasks()) {
            titles << t.title;
        }
        titles.sort();
        QCOMPARE(titles, QStringList({"Três", "Um"}));
    }

    void worksWithoutSyncCollection()
    {
        server->syncCollectionSupported = false;
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um"));
        QVERIFY(sync().ok());
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Um v2"));
        server->putItem(tasksHref, "b.ics", todoIcs("b", "Dois"));
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(db->tasks().size(), 2);
        QVERIFY(taskTitled("Um v2").id > 0);
    }

    void serverWithoutEtagOnPut()
    {
        server->sendEtagOnPut = false;
        QVERIFY(sync().ok());
        Task t;
        t.listId = onlyList().id;
        t.title = "x";
        QVERIFY(db->insertTask(t));
        QVERIFY(sync().ok());
        QVERIFY(!db->task(t.id)->etag.isEmpty());
        server->clearLog();
        const auto r = sync();
        QVERIFY(r.ok());
        QCOMPARE(r.pulled, 0);
        // A second edit must still pass the If-Match check.
        Task cur = *db->task(t.id);
        cur.title = "y";
        QVERIFY(db->updateTaskUser(cur));
        QVERIFY(sync().ok());
        QCOMPARE(serverSummaries(tasksHref), QString("y"));
    }

    void listLifecycle()
    {
        QVERIFY(sync().ok());
        // create
        TaskList l;
        l.accountId = accountId;
        l.name = "Compras da Semana";
        l.color = "#ff0000";
        QVERIFY(db->insertList(l));
        Task t;
        t.listId = l.id;
        t.title = "Pão";
        QVERIFY(db->insertTask(t));
        auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(server->collections.size(), 2);
        const QString href = db->list(l.id)->href;
        QVERIFY(href.startsWith(MockCalDavServer::home + "compras-da-semana-"));
        QCOMPARE(server->collections[href].name, QString("Compras da Semana"));
        QCOMPARE(server->collections[href].color, QString("#ff0000"));
        QCOMPARE(serverSummaries(href), QString("Pão"));
        QCOMPARE(db->list(l.id)->syncState, SyncState::Synced);

        // rename
        TaskList renamed = *db->list(l.id);
        renamed.name = "Mercado";
        QVERIFY(db->updateListUser(renamed));
        QVERIFY(sync().ok());
        QCOMPARE(server->collections[href].name, QString("Mercado"));
        QCOMPARE(db->list(l.id)->syncState, SyncState::Synced);

        // remote rename + remote new list
        server->collections[href].name = "Feira";
        server->addCollection("novo", "Criada no servidor");
        QVERIFY(sync().ok());
        QCOMPARE(db->list(l.id)->name, QString("Feira"));
        QCOMPARE(db->lists().size(), 3);

        // local delete
        QVERIFY(db->removeListUser(l.id));
        QCOMPARE(db->lists().size(), 2);
        QVERIFY(sync().ok());
        QVERIFY(!server->collections.contains(href));
        QCOMPARE(db->lists(true).size(), 2);

        // remote delete
        server->collections.remove(MockCalDavServer::home + "novo/");
        QVERIFY(sync().ok());
        QCOMPARE(db->lists().size(), 1);
    }

    void movingATaskBetweenLists()
    {
        const QString other = server->addCollection("outra", "Outra");
        const QString href = server->putItem(tasksHref, "a.ics", todoIcs("a", "Viajante"));
        QVERIFY(sync().ok());
        qint64 otherId = 0;
        for (const TaskList &l : db->lists()) {
            if (l.href == other) {
                otherId = l.id;
            }
        }
        QVERIFY(otherId > 0);
        QVERIFY(db->moveTask(taskTitled("Viajante").id, otherId));
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QVERIFY(server->icsOf(href).isEmpty());
        QCOMPARE(serverSummaries(other), QString("Viajante"));
        QCOMPARE(db->tasks(true).size(), 1);
        QCOMPARE(db->tasks().first().listId, otherId);
    }

    void readOnlyListIsNotWrittenTo()
    {
        server->collections[tasksHref].readOnly = true;
        server->putItem(tasksHref, "a.ics", todoIcs("a", "Somente leitura"));
        QVERIFY(sync().ok());
        QVERIFY(onlyList().readOnly);
        Task t = taskTitled("Somente leitura");
        t.title = "tentativa";
        QVERIFY(db->updateTaskUser(t));
        server->clearLog();
        QVERIFY(sync().ok());
        QCOMPARE(server->count("PUT"), 0);
    }

    void manyTasksAreFetchedInBatches()
    {
        for (int i = 0; i < 120; ++i) {
            server->putItem(tasksHref, QString("t%1.ics").arg(i), todoIcs(QString("t%1").arg(i), QString("Tarefa %1").arg(i)));
        }
        server->clearLog();
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(db->tasks().size(), 120);
        QCOMPARE(server->count("REPORT"), 4); // one listing + three multigets of 50/50/20
    }

    void uidsWithOddCharacters()
    {
        QVERIFY(sync().ok());
        Task t;
        t.listId = onlyList().id;
        t.uid = "weird uid/with:chars";
        t.title = "ç ã % espaço";
        QVERIFY(db->insertTask(t));
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(serverSummaries(tasksHref), QString("ç ã % espaço"));
        // and a remote file name with spaces
        server->putItem(tasksHref, "com espaço.ics", todoIcs("sp", "Espaço"));
        QVERIFY(sync().ok());
        QVERIFY(taskTitled("Espaço").id > 0);
        Task sp = taskTitled("Espaço");
        sp.title = "Espaço 2";
        QVERIFY(db->updateTaskUser(sp));
        QVERIFY(sync().ok());
        QVERIFY(server->icsOf(tasksHref + "com espaço.ics").contains("Espaço 2"));
    }

    void verifyChecksCredentialsAndServer()
    {
        server->addCollection("events", "Agenda", {"VEVENT"});
        auto v = engine->verify(server->url().toString(), server->user, server->password);
        QCOMPARE(v.kind, SyncEngine::NoError);
        QCOMPARE(v.homeUrl, MockCalDavServer::home);
        QCOMPARE(v.taskLists, 1);
        QVERIFY(v.davUrl.endsWith("/remote.php/dav/"));

        v = engine->verify(server->url().toString(), server->user, "nope");
        QCOMPARE(v.kind, SyncEngine::Auth);

        v = engine->verify("http://127.0.0.1:1", "u", "p");
        QCOMPARE(v.kind, SyncEngine::Offline);
    }

    void pendingChangesSurviveRestart()
    {
        QVERIFY(sync().ok());
        Task t;
        t.listId = onlyList().id;
        t.title = "Persistente";
        QVERIFY(db->insertTask(t));
        // "Restart": new connections, new engine.
        engine.reset();
        db.reset();
        db = std::make_unique<Database>(dir->filePath("a.db"));
        QVERIFY(db->open());
        QVERIFY(db->hasPendingChanges(accountId));
        engine = std::make_unique<SyncEngine>(dir->filePath("a.db"));
        QVERIFY(sync().ok());
        QCOMPARE(serverSummaries(tasksHref), QString("Persistente"));
    }

    void nextcloudNotesSyncAndOfflineToggle()
    {
        server->notesEnabled = true;
        const qint64 srvNoteId = server->putNote("Resumo Aula", "# Aula 1\nConteúdo remoto", "Estudos");

        // Local offline-only note must never be pushed or deleted by Nextcloud Notes sync.
        JournalNotebook localNb;
        localNb.name = "Pessoal";
        QVERIFY(db->insertJournalNotebook(localNb));
        JournalEntry offlineNote;
        offlineNote.notebookId = localNb.id;
        offlineNote.title = "Diário Privado Offline";
        offlineNote.content = "Só no PC";
        offlineNote.syncAccountId = 0;
        QVERIFY(db->insertJournalEntry(offlineNote));

        auto r = engine->syncNotesAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pulled, 1);
        QCOMPARE(server->notes.size(), 1); // offline note was not uploaded

        const auto pulled = db->journalEntryByRemote(accountId, srvNoteId);
        QVERIFY(pulled.has_value());
        QCOMPARE(pulled->title, QString("Resumo Aula"));
        QCOMPARE(pulled->syncState, SyncState::Synced);

        // Create a synced local note in notebook "Estudos" -> pushes via POST
        JournalEntry newSynced;
        newSynced.notebookId = pulled->notebookId;
        newSynced.title = "Nova Anotação Cloud";
        newSynced.content = "Sincronizar esta";
        newSynced.syncAccountId = accountId;
        newSynced.syncState = SyncState::New;
        QVERIFY(db->insertJournalEntry(newSynced));

        r = engine->syncNotesAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pushed, 1);
        QCOMPARE(server->notes.size(), 2);

        const auto uploaded = db->journalEntry(newSynced.id);
        QVERIFY(uploaded.has_value());
        QVERIFY(uploaded->remoteId > 0);
        QCOMPARE(uploaded->syncState, SyncState::Synced);

        // Concurrent edit conflict -> preserves local edit in offline copy and pulls server version
        JournalEntry localEdit = *uploaded;
        localEdit.content = "Edição local conflitante";
        localEdit.syncState = SyncState::Modified;
        QVERIFY(db->updateJournalEntry(localEdit));
        server->putNote("Nova Anotação Cloud", "Edição do servidor", "Estudos", uploaded->remoteId);

        r = engine->syncNotesAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.conflictTitles.size(), 1);
        QCOMPARE(db->journalEntry(newSynced.id)->content, QString("Edição do servidor"));

        // Switching synced note -> Offline Only deletes cloud copy and keeps local note
        const qint64 remoteToDelete = db->journalEntry(srvNoteId > 0 ? pulled->id : 0)->remoteId;
        QVERIFY(remoteToDelete > 0);
        JournalEntry makeOffline = *db->journalEntry(pulled->id);
        QVERIFY(db->queueJournalRemoteDelete(accountId, makeOffline.remoteId));
        makeOffline.syncAccountId = 0;
        makeOffline.remoteId = 0;
        makeOffline.etag.clear();
        makeOffline.syncState = SyncState::Synced;
        QVERIFY(db->updateJournalEntry(makeOffline));

        r = engine->syncNotesAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QVERIFY(!server->notes.contains(remoteToDelete));
        QVERIFY(db->journalEntry(pulled->id).has_value());
        QVERIFY(db->journalEntry(offlineNote.id).has_value());
    }

    void routineVeventSyncAndOfflineToggle()
    {
        // Server starts with no VEVENT calendar -> engine creates "Rotina" via MKCALENDAR
        RoutineBlock localOffline;
        localOffline.title = "Diário offline da rotina";
        localOffline.startMinute = 6 * 60;
        localOffline.durationMinutes = 30;
        localOffline.daysMask = 127;
        localOffline.syncAccountId = 0;
        QVERIFY(db->insertRoutineBlock(localOffline));

        RoutineBlock syncedBlock;
        syncedBlock.title = "Foco Profundo";
        syncedBlock.notes = "Sem distrações";
        syncedBlock.startMinute = 9 * 60;
        syncedBlock.durationMinutes = 90;
        syncedBlock.daysMask = (1 << 0) | (1 << 2) | (1 << 4);
        syncedBlock.color = "#9b59b6";
        syncedBlock.notifyEnabled = true;
        syncedBlock.notifyMinutesBefore = 10;
        syncedBlock.syncAccountId = accountId;
        syncedBlock.syncState = SyncState::New;
        QVERIFY(db->insertRoutineBlock(syncedBlock));
        QVERIFY(db->setRoutineLog(syncedBlock.id, QDate::currentDate(), RoutineLogStatus::Done));

        auto r = engine->syncRoutineAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pushed, 1);

        QString rotHref;
        for (auto it = server->collections.cbegin(); it != server->collections.cend(); ++it) {
            if (it.value().components.contains(QLatin1String("VEVENT"))) {
                rotHref = it.key();
            }
        }
        QVERIFY(!rotHref.isEmpty());
        QCOMPARE(server->collections[rotHref].items.size(), 1); // offline block never uploaded

        const auto uploaded = db->routineBlock(syncedBlock.id);
        QVERIFY(uploaded.has_value());
        QCOMPARE(uploaded->syncState, SyncState::Synced);
        QCOMPARE(uploaded->calendarHref, rotHref);
        QVERIFY(!uploaded->href.isEmpty());

        // Remote mobile client (e.g. DAVx5 / Nextcloud Calendar) creates a VEVENT -> pulled into KTasks
        RoutineBlock remoteMob;
        remoteMob.uid = "mobile-event-1";
        remoteMob.title = "Treino Academia";
        remoteMob.startMinute = 18 * 60;
        remoteMob.durationMinutes = 60;
        remoteMob.daysMask = (1 << 1) | (1 << 3);
        const QString remoteHref = server->putItem(rotHref, QStringLiteral("mobile-event-1.ics"), IcsCodec::encodeEvent(remoteMob));

        r = engine->syncRoutineAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pulled, 1);
        const auto pulled = db->routineBlockByHref(accountId, remoteHref);
        QVERIFY(pulled.has_value());
        QCOMPARE(pulled->title, QString("Treino Academia"));
        QCOMPARE(pulled->daysMask, (1 << 1) | (1 << 3));

        // Conflict on concurrent edit -> preserves local edits in offline copy, keeps remote winner
        RoutineBlock editLocal = *pulled;
        editLocal.title = "Treino Local";
        editLocal.syncState = SyncState::Modified;
        QVERIFY(db->updateRoutineBlockFull(editLocal));
        remoteMob.title = "Treino Mobile Remoto";
        server->putItem(rotHref, QStringLiteral("mobile-event-1.ics"), IcsCodec::encodeEvent(remoteMob));

        r = engine->syncRoutineAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.conflictTitles.size(), 1);
        QCOMPARE(db->routineBlock(pulled->id)->title, QString("Treino Mobile Remoto"));

        // Switch Synced -> Offline Only: queues remote DELETE and retains block locally
        const RoutineBlock toDetach = *db->routineBlock(uploaded->id);
        QVERIFY(db->queueRoutineRemoteDelete(accountId, toDetach.href, toDetach.etag));
        RoutineBlock offlineCopy = toDetach;
        offlineCopy.syncAccountId = 0;
        offlineCopy.calendarHref.clear();
        offlineCopy.href.clear();
        offlineCopy.etag.clear();
        offlineCopy.baseIcs.clear();
        offlineCopy.syncState = SyncState::Synced;
        QVERIFY(db->updateRoutineBlockFull(offlineCopy));

        r = engine->syncRoutineAccount(accountId, server->password);
        QVERIFY2(r.ok(), qPrintable(r.message));
        QVERIFY(!server->collections[rotHref].items.contains(toDetach.href));
        QVERIFY(db->routineBlock(toDetach.id).has_value());
        QCOMPARE(db->routineBlock(toDetach.id)->syncAccountId, qint64(0));
    }

    void quadrantSyncsWithCalDav()
    {
        QVERIFY(sync().ok());
        Task t;
        t.listId = onlyList().id;
        t.title = "Revisar contrato";
        t.quadrant = 2;
        t.priority = Task::priorityForQuadrant(2);
        QVERIFY(db->insertTask(t));
        const auto r = sync();
        QVERIFY2(r.ok(), qPrintable(r.message));
        QCOMPARE(r.pushed, 1);
        const auto uploaded = db->task(t.id);
        QVERIFY(uploaded.has_value());
        const QString remoteIcs = server->icsOf(uploaded->href);
        const auto decRemote = IcsCodec::decode(remoteIcs);
        QVERIFY(decRemote.has_value());
        QCOMPARE(decRemote->quadrant, 2);
        QVERIFY(remoteIcs.contains("X-KTASKS-QUADRANT"));
        QVERIFY(remoteIcs.contains("PRIORITY:5"));
    }
};

QTEST_GUILESS_MAIN(SyncTest)
#include "synctest.moc"
