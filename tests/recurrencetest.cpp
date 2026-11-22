// SPDX-License-Identifier: GPL-3.0-or-later
#include "recurrence.h"

#include <QTest>
#include <QTimeZone>

using namespace KTasks;

static QDateTime day(int y, int m, int d)
{
    return QDateTime(QDate(y, m, d), QTime(0, 0));
}

class RecurrenceTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void dailyAdvancesOneDay()
    {
        Task t;
        t.allDay = true;
        t.due = day(2026, 11, 17);
        t.rrule = "FREQ=DAILY";
        t.status = TaskStatus::Completed;
        QVERIFY(Recurrence::advance(t, day(2026, 11, 17)));
        QCOMPARE(t.due.date(), QDate(2026, 11, 18));
        QCOMPARE(t.status, TaskStatus::NeedsAction);
    }

    void overdueSkipsToToday()
    {
        Task t;
        t.allDay = true;
        t.due = day(2026, 11, 1);
        t.rrule = "FREQ=DAILY";
        QVERIFY(Recurrence::advance(t, QDateTime(QDate(2026, 11, 17), QTime(15, 0))));
        QCOMPARE(t.due.date(), QDate(2026, 11, 17));
    }

    void weeklyByDay()
    {
        Task t;
        t.allDay = true;
        t.due = day(2026, 11, 16); // Monday
        t.rrule = "FREQ=WEEKLY;BYDAY=MO,TH";
        QVERIFY(Recurrence::advance(t, day(2026, 11, 16)));
        QCOMPARE(t.due.date(), QDate(2026, 11, 19));
    }

    void monthlyKeepsStartDueDistance()
    {
        Task t;
        t.allDay = true;
        t.start = day(2026, 1, 10);
        t.due = day(2026, 1, 12);
        t.rrule = "FREQ=MONTHLY";
        QVERIFY(Recurrence::advance(t, day(2026, 1, 10)));
        QCOMPARE(t.start.date(), QDate(2026, 2, 10));
        QCOMPARE(t.due.date(), QDate(2026, 2, 12));
    }

    void timedKeepsWallClock()
    {
        Task t;
        t.allDay = false;
        const QDateTime local(QDate(2026, 11, 17), QTime(9, 0), QTimeZone::systemTimeZone());
        t.due = local.toUTC();
        t.rrule = "FREQ=DAILY";
        QVERIFY(Recurrence::advance(t, local));
        QCOMPARE(t.due.toTimeZone(QTimeZone::systemTimeZone()).time(), QTime(9, 0));
        QCOMPARE(t.due.toTimeZone(QTimeZone::systemTimeZone()).date(), QDate(2026, 11, 18));
    }

    void countRunsOut()
    {
        Task t;
        t.allDay = true;
        t.due = day(2026, 11, 17);
        t.rrule = "FREQ=DAILY;COUNT=3";
        QVERIFY(Recurrence::advance(t, day(2026, 11, 17)));
        QVERIFY2(t.rrule.contains("COUNT=2"), qPrintable(t.rrule));
        QVERIFY(Recurrence::advance(t, day(2026, 11, 18)));
        QVERIFY2(t.rrule.contains("COUNT=1"), qPrintable(t.rrule));
        QCOMPARE(t.due.date(), QDate(2026, 11, 19));
        QVERIFY(!Recurrence::advance(t, day(2026, 11, 19)));
        QCOMPARE(t.due.date(), QDate(2026, 11, 19)); // untouched
    }

    void untilRunsOut()
    {
        Task t;
        t.allDay = true;
        t.due = day(2026, 11, 17);
        t.rrule = "FREQ=DAILY;UNTIL=20261118";
        QVERIFY(Recurrence::advance(t, day(2026, 11, 17)));
        QVERIFY(!Recurrence::advance(t, day(2026, 11, 18)));
    }

    void garbageRule()
    {
        Task t;
        t.due = day(2026, 11, 17);
        t.rrule = "NOT A RULE";
        QVERIFY(!Recurrence::advance(t, day(2026, 11, 17)));
        Task noDate;
        noDate.rrule = "FREQ=DAILY";
        QVERIFY(!Recurrence::advance(noDate, day(2026, 11, 17)));
    }

    void simpleRules()
    {
        auto s = Recurrence::parseSimple("FREQ=WEEKLY;INTERVAL=2");
        QVERIFY(s.valid);
        QCOMPARE(s.freq, QString("WEEKLY"));
        QCOMPARE(s.interval, 2);
        QVERIFY(Recurrence::parseSimple("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR").weekdays);
        QVERIFY(!Recurrence::parseSimple("FREQ=MONTHLY;BYDAY=2TU").valid);
        QVERIFY(!Recurrence::parseSimple("").valid);
        QCOMPARE(Recurrence::buildSimple("DAILY", 3, false), QString("FREQ=DAILY;INTERVAL=3"));
        QCOMPARE(Recurrence::buildSimple("", 1, false), QString());
    }

    void fullRulePartsAndRoundTrip()
    {
        auto p = Recurrence::parseRule("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE,FR;COUNT=8");
        QVERIFY(p.valid);
        QCOMPARE(p.freq, QString("WEEKLY"));
        QCOMPARE(p.interval, 2);
        QCOMPARE(p.byDays, QStringList({"MO", "WE", "FR"}));
        QCOMPARE(p.endMode, QString("count"));
        QCOMPARE(p.count, 8);
        QCOMPARE(Recurrence::buildRule(p), QString("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,WE,FR;COUNT=8"));

        auto u = Recurrence::parseRule("FREQ=MONTHLY;UNTIL=20270630");
        QVERIFY(u.valid);
        QCOMPARE(u.endMode, QString("until"));
        QCOMPARE(u.until, QDate(2027, 6, 30));
        QCOMPARE(Recurrence::buildRule(u), QString("FREQ=MONTHLY;UNTIL=20270630"));

        QVERIFY(!Recurrence::parseRule("FREQ=MONTHLY;BYDAY=2TU").valid);
    }
};

QTEST_GUILESS_MAIN(RecurrenceTest)
#include "recurrencetest.moc"
