// SPDX-License-Identifier: GPL-3.0-or-later
#include "merge.h"

#include <QTest>

using namespace KTasks;

class MergeTest : public QObject
{
    Q_OBJECT
    static Task base()
    {
        Task t;
        t.id = 7;
        t.uid = "u";
        t.title = "título";
        t.notes = "notas";
        t.priority = 5;
        t.tags = {"a"};
        t.sortOrder = 10;
        return t;
    }
private Q_SLOTS:
    void differentFieldsMergeCleanly()
    {
        Task local = base(), remote = base();
        local.title = "título local";
        remote.notes = "notas remotas";
        remote.priority = 1;
        const MergeResult r = mergeTasks(base(), local, remote);
        QVERIFY(!r.conflict);
        QVERIFY(r.localChangesKept);
        QCOMPARE(r.merged.title, QString("título local"));
        QCOMPARE(r.merged.notes, QString("notas remotas"));
        QCOMPARE(r.merged.priority, 1);
        QCOMPARE(r.merged.id, 7);
    }

    void sameChangeOnBothSidesIsNotAConflict()
    {
        Task local = base(), remote = base();
        local.title = remote.title = "igual";
        const MergeResult r = mergeTasks(base(), local, remote);
        QVERIFY(!r.conflict);
        QVERIFY(!r.localChangesKept);
    }

    void sameFieldDifferentValuesConflicts()
    {
        Task local = base(), remote = base();
        local.title = "L";
        local.notes = "minhas notas";
        remote.title = "R";
        const MergeResult r = mergeTasks(base(), local, remote);
        QVERIFY(r.conflict);
        QCOMPARE(r.conflictFields, QStringList{"title"});
        QCOMPARE(r.merged.title, QString("R"));
        QCOMPARE(r.merged.notes, QString("notas")); // on conflict the task becomes the remote version
        QCOMPARE(r.merged.id, 7);
    }

    void bothCompletedIsNotAConflict()
    {
        Task local = base(), remote = base();
        local.status = remote.status = TaskStatus::Completed;
        local.percent = remote.percent = 100;
        local.completedAt = QDateTime(QDate(2026, 1, 1), QTime(1, 0), QTimeZone::UTC);
        remote.completedAt = QDateTime(QDate(2026, 1, 1), QTime(2, 0), QTimeZone::UTC);
        QVERIFY(!mergeTasks(base(), local, remote).conflict);
    }

    void onlyRemoteChanged()
    {
        Task remote = base();
        remote.title = "novo";
        remote.tags = {"a", "b"};
        const MergeResult r = mergeTasks(base(), base(), remote);
        QVERIFY(!r.conflict);
        QVERIFY(!r.localChangesKept);
        QVERIFY(sameContent(r.merged, remote));
    }

    void sortOrderNeverConflicts()
    {
        Task local = base(), remote = base();
        local.sortOrder = 1;
        remote.sortOrder = 2;
        const MergeResult r = mergeTasks(base(), local, remote);
        QVERIFY(!r.conflict);
        QCOMPARE(r.merged.sortOrder, 2.0);
    }
};

QTEST_GUILESS_MAIN(MergeTest)
#include "mergetest.moc"
