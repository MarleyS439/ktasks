// SPDX-License-Identifier: GPL-3.0-or-later
// Manual check against a real server (not run by ctest):
//   livesync https://cloud.example.com user app-password
// Uses a throw-away database; creates, edits and deletes one task named "KTasks livesync".
#include "database.h"
#include "icscodec.h"
#include "syncengine.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>

using namespace KTasks;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 4) {
        out << "usage: livesync <server url> <user> <password>\n";
        return 2;
    }
    const QString url = QString::fromLocal8Bit(argv[1]), user = QString::fromLocal8Bit(argv[2]), pass = QString::fromLocal8Bit(argv[3]);
    QTemporaryDir dir;
    Database db(dir.filePath("live.db"));
    if (!db.open()) {
        return 1;
    }
    SyncEngine engine(dir.filePath("live.db"));
    const auto v = engine.verify(url, user, pass);
    out << "verify: kind=" << v.kind << " " << v.message << " dav=" << v.davUrl << " home=" << v.homeUrl << " taskLists=" << v.taskLists << "\n";
    if (v.kind != SyncEngine::NoError) {
        return 1;
    }
    Account a;
    a.name = "live";
    a.serverUrl = url;
    a.username = user;
    db.insertAccount(a);
    auto report = [&](const char *step, const SyncEngine::Result &r) {
        out << step << ": kind=" << r.kind << " " << r.message << " pulled=" << r.pulled << " pushed=" << r.pushed << " conflicts=" << r.conflictTitles.size()
            << " lists=" << db.lists().size() << " tasks=" << db.tasks().size() << "\n";
        out.flush();
        return r.ok();
    };
    if (!report("first sync", engine.syncAccount(a.id, pass))) {
        return 1;
    }
    qint64 listId = 0;
    for (const TaskList &l : db.lists()) {
        if (!l.readOnly) {
            listId = l.id;
            break;
        }
    }
    if (listId == 0) {
        TaskList l;
        l.accountId = a.id;
        l.name = "KTasks livesync";
        db.insertList(l);
        listId = l.id;
        out << "no writable task list on the server: creating one\n";
    }
    Task t;
    t.listId = listId;
    t.title = "KTasks livesync";
    t.allDay = false;
    t.due = QDateTime::currentDateTimeUtc().addDays(1);
    t.tags = {"ktasks"};
    t.alarms = {Alarm{Alarm::RelativeToDue, {}, -900}};
    db.insertTask(t);
    if (!report("upload", engine.syncAccount(a.id, pass))) {
        return 1;
    }
    Task cur = *db.task(t.id);
    out << "  state=" << int(cur.syncState) << " href=" << cur.href << " etag=" << cur.etag << "\n";
    cur.title = "KTasks livesync (edited)";
    cur.status = TaskStatus::Completed;
    db.updateTaskUser(cur);
    if (!report("edit", engine.syncAccount(a.id, pass))) {
        return 1;
    }
    // A second, fresh database must see exactly what we uploaded.
    QTemporaryDir dir2;
    Database db2(dir2.filePath("live2.db"));
    db2.open();
    Account b = a;
    b.id = 0;
    db2.insertAccount(b);
    SyncEngine engine2(dir2.filePath("live2.db"));
    const auto r2 = engine2.syncAccount(b.id, pass);
    bool seen = false;
    for (const Task &other : db2.tasks()) {
        if (other.uid == cur.uid) {
            seen = other.title == cur.title && other.isDone() && other.tags == cur.tags && other.alarms.size() == 1 && other.due == db.task(t.id)->due;
        }
    }
    out << "second device: kind=" << r2.kind << " sees the edited task correctly: " << (seen ? "yes" : "NO") << "\n";
    db.removeTaskUser(t.id);
    if (!report("delete", engine.syncAccount(a.id, pass))) {
        return 1;
    }
    const auto r3 = engine2.syncAccount(b.id, pass);
    bool gone = true;
    for (const Task &other : db2.tasks()) {
        gone = gone && other.uid != cur.uid;
    }
    out << "second device after delete: kind=" << r3.kind << " task gone: " << (gone ? "yes" : "NO") << "\n";
    return seen && gone ? 0 : 1;
}
