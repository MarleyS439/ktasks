// SPDX-License-Identifier: GPL-3.0-or-later
#include "icscodec.h"

#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

using namespace KTasks;

static const char *kNextcloudTodo = R"(BEGIN:VCALENDAR
VERSION:2.0
PRODID:-//Nextcloud Tasks v0.16.1
BEGIN:VTODO
UID:abc-123
CREATED:20260101T100000Z
LAST-MODIFIED:20260102T100000Z
DTSTAMP:20260102T100000Z
SUMMARY:Comprar leite
DESCRIPTION:Integral\, 2 litros
PRIORITY:1
DUE;VALUE=DATE:20261224
CATEGORIES:mercado,casa
RELATED-TO:parent-uid
PERCENT-COMPLETE:40
STATUS:IN-PROCESS
X-APPLE-SORT-ORDER:42
X-OC-HIDESUBTASKS:1
X-CUSTOM-THING;FOO=bar:keep me
BEGIN:VALARM
TRIGGER;RELATED=END:-PT15M
ACTION:DISPLAY
DESCRIPTION:Comprar leite
END:VALARM
END:VTODO
END:VCALENDAR
)";

class IcsCodecTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void decodesNextcloudTodo()
    {
        const auto t = IcsCodec::decode(QString::fromUtf8(kNextcloudTodo));
        QVERIFY(t.has_value());
        QCOMPARE(t->uid, QString("abc-123"));
        QCOMPARE(t->title, QString("Comprar leite"));
        QCOMPARE(t->notes, QString("Integral, 2 litros"));
        QCOMPARE(t->priority, 1);
        QCOMPARE(t->priorityLevel(), 3);
        QVERIFY(t->allDay);
        QCOMPARE(t->due.date(), QDate(2026, 12, 24));
        QVERIFY(!t->start.isValid());
        QCOMPARE(t->tags, QStringList({"mercado", "casa"}));
        QCOMPARE(t->parentUid, QString("parent-uid"));
        QCOMPARE(t->percent, 40);
        QCOMPARE(t->status, TaskStatus::InProcess);
        QCOMPARE(t->sortOrder, 42.0);
        QCOMPARE(t->alarms.size(), 1);
        QCOMPARE(t->alarms.first().kind, Alarm::RelativeToDue);
        QCOMPARE(t->alarms.first().offsetSecs, -900);
    }

    void freshTaskRoundTrips()
    {
        Task t;
        t.uid = "u1";
        t.title = "Reunião; com, vírgulas";
        t.notes = "multi\nlinha";
        t.priority = 5;
        t.allDay = false;
        t.start = QDateTime(QDate(2026, 5, 1), QTime(12, 0), QTimeZone::UTC);
        t.due = QDateTime(QDate(2026, 5, 2), QTime(17, 30), QTimeZone::UTC);
        t.tags = {"trabalho"};
        t.rrule = "FREQ=WEEKLY;BYDAY=MO";
        t.alarms = {Alarm{Alarm::RelativeToDue, {}, -3600}};
        t.sortOrder = 1234;
        t.created = QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
        t.modified = t.created;
        const QString ics = IcsCodec::encode(t);
        QVERIFY(ics.contains("BEGIN:VTODO"));
        const auto back = IcsCodec::decode(ics);
        QVERIFY(back.has_value());
        QCOMPARE(back->uid, t.uid);
        QCOMPARE(back->title, t.title);
        QCOMPARE(back->notes, t.notes);
        QCOMPARE(back->priority, t.priority);
        QCOMPARE(back->allDay, false);
        QCOMPARE(back->start, t.start);
        QCOMPARE(back->due, t.due);
        QCOMPARE(back->tags, t.tags);
        QVERIFY2(back->rrule.contains("FREQ=WEEKLY") && back->rrule.contains("BYDAY=MO"), qPrintable(back->rrule));
        QCOMPARE(back->alarms, t.alarms);
        QCOMPARE(back->sortOrder, t.sortOrder);
        QCOMPARE(back->status, TaskStatus::NeedsAction);
    }

    void allDayRoundTrips()
    {
        Task t;
        t.uid = "u2";
        t.title = "x";
        t.allDay = true;
        t.due = QDateTime(QDate(2026, 7, 4), QTime(0, 0));
        const QString ics = IcsCodec::encode(t);
        QVERIFY2(ics.contains("DUE;VALUE=DATE:20260704"), qPrintable(ics));
        const auto back = IcsCodec::decode(ics);
        QVERIFY(back->allDay);
        QCOMPARE(back->due.date(), QDate(2026, 7, 4));
        QVERIFY(!back->start.isValid());
    }

    void noDatesRoundTrips()
    {
        Task t;
        t.uid = "u3";
        t.title = "sem data";
        const auto back = IcsCodec::decode(IcsCodec::encode(t));
        QVERIFY(back.has_value());
        QVERIFY(!back->due.isValid());
        QVERIFY(!back->start.isValid());
        QVERIFY(back->allDay);
    }

    void completionRoundTrips()
    {
        Task t;
        t.uid = "u4";
        t.title = "feito";
        t.status = TaskStatus::Completed;
        t.percent = 100;
        t.completedAt = QDateTime(QDate(2026, 2, 3), QTime(4, 5, 6), QTimeZone::UTC);
        const QString ics = IcsCodec::encode(t);
        QVERIFY(ics.contains("STATUS:COMPLETED"));
        const auto back = IcsCodec::decode(ics);
        QCOMPARE(back->status, TaskStatus::Completed);
        QCOMPARE(back->completedAt, t.completedAt);

        // and back to open again
        Task reopened = *back;
        reopened.baseIcs = ics;
        reopened.status = TaskStatus::NeedsAction;
        reopened.percent = 0;
        reopened.completedAt = {};
        const auto again = IcsCodec::decode(IcsCodec::encode(reopened));
        QCOMPARE(again->status, TaskStatus::NeedsAction);
        QVERIFY(!again->completedAt.isValid());
    }

    void unknownPropertiesSurviveAnEdit()
    {
        const QString original = QString::fromUtf8(kNextcloudTodo);
        Task t = *IcsCodec::decode(original);
        t.baseIcs = original;
        t.title = "Comprar leite e pão";
        t.modified = QDateTime::currentDateTimeUtc();
        const QString ics = IcsCodec::encode(t);
        // KCalendarCore keeps the values of unknown properties (it rewrites their parameters).
        static const QRegularExpression hide("X-OC-HIDESUBTASKS[^:\n]*:1"), custom("X-CUSTOM-THING[^:\n]*:keep me"), order("X-APPLE-SORT-ORDER[^:\n]*:42");
        QVERIFY2(ics.contains(hide), qPrintable(ics));
        QVERIFY2(ics.contains(custom), qPrintable(ics));
        QVERIFY(ics.contains(order));
        QVERIFY(ics.contains("RELATED-TO:parent-uid"));
        const auto back = IcsCodec::decode(ics);
        QCOMPARE(back->title, QString("Comprar leite e pão"));
        QCOMPARE(back->percent, 40);
        QCOMPARE(back->status, TaskStatus::InProcess);
        QCOMPARE(back->due.date(), QDate(2026, 12, 24));
        QCOMPARE(back->alarms.size(), 1);
    }

    void decodeIgnoresEvents()
    {
        const QString ics = "BEGIN:VCALENDAR\nVERSION:2.0\nPRODID:x\nBEGIN:VEVENT\nUID:e1\nDTSTART:20260101T100000Z\nSUMMARY:evento\nEND:VEVENT\nEND:VCALENDAR\n";
        QVERIFY(!IcsCodec::decode(ics).has_value());
        QVERIFY(!IcsCodec::decode("garbage").has_value());
    }

    void exportAndImportMany()
    {
        QList<Task> tasks;
        for (int i = 0; i < 3; ++i) {
            Task t;
            t.uid = QString("m%1").arg(i);
            t.title = QString("tarefa %1").arg(i);
            tasks.append(t);
        }
        tasks[1].parentUid = "m0";
        const auto back = IcsCodec::decodeAll(IcsCodec::exportCalendar(tasks));
        QCOMPARE(back.size(), 3);
        int children = 0;
        for (const Task &t : back) {
            children += t.parentUid == "m0";
        }
        QCOMPARE(children, 1);
    }

    void routineEventRoundTrip()
    {
        RoutineBlock b;
        b.uid = "routine-1";
        b.title = "Treino matinal";
        b.notes = "Aquecimento + cardio";
        b.startMinute = 7 * 60 + 15;
        b.durationMinutes = 45;
        b.daysMask = (1 << 0) | (1 << 2) | (1 << 4); // Mon, Wed, Fri
        b.startDate = QDate(2026, 11, 23);
        b.color = "#27ae60";
        b.category = "Saúde";
        b.notifyEnabled = true;
        b.notifyMinutesBefore = 10;
        b.active = true;

        QList<RoutineLog> logs;
        RoutineLog l;
        l.logDate = QDate(2026, 11, 20);
        l.status = RoutineLogStatus::Done;
        logs << l;

        const QString ics = IcsCodec::encodeEvent(b, logs);
        QVERIFY(ics.contains("BEGIN:VEVENT"));
        QVERIFY(ics.contains("RRULE:"));
        QVERIFY(ics.contains("BEGIN:VALARM"));
        QVERIFY(!IcsCodec::decode(ics).has_value()); // decode() is VTODO-only

        const auto back = IcsCodec::decodeEvent(ics);
        QVERIFY(back.has_value());
        QCOMPARE(back->block.uid, b.uid);
        QCOMPARE(back->block.title, b.title);
        QCOMPARE(back->block.notes, b.notes);
        QCOMPARE(back->block.startMinute, b.startMinute);
        QCOMPARE(back->block.durationMinutes, b.durationMinutes);
        QCOMPARE(back->block.daysMask, b.daysMask);
        QCOMPARE(back->block.startDate, QDate(2026, 11, 23));
        QVERIFY(!back->block.specificDate.isValid());
        QCOMPARE(back->block.color, b.color);
        QCOMPARE(back->block.category, b.category);
        QCOMPARE(back->block.notifyEnabled, true);
        QCOMPARE(back->block.notifyMinutesBefore, 10);
        QCOMPARE(back->logs.size(), 1);
        QCOMPARE(back->logs.first().logDate, QDate(2026, 11, 20));
        QCOMPARE(back->logs.first().status, RoutineLogStatus::Done);

        // Single-day non-recurring planner block round-trip
        RoutineBlock single;
        single.uid = "single-1";
        single.title = "Revisar PR";
        single.startMinute = 14 * 60;
        single.durationMinutes = 90;
        single.specificDate = QDate(2026, 11, 22);
        single.daysMask = 0;
        const auto singleBack = IcsCodec::decodeEvent(IcsCodec::encodeEvent(single));
        QVERIFY(singleBack.has_value());
        QCOMPARE(singleBack->block.specificDate, QDate(2026, 11, 22));
        QCOMPARE(singleBack->block.startMinute, 14 * 60);
        QCOMPARE(singleBack->block.durationMinutes, 90);

        // External CalDAV recurring VEVENT (no X-KTASKS-* metadata) infers startDate from DTSTART
        const QString extIcs = QStringLiteral(
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "PRODID:-//Nextcloud Calendar//EN\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:ext-rec-1\r\n"
            "DTSTART:20261122T150000Z\r\n"
            "DTEND:20261122T160000Z\r\n"
            "SUMMARY:Curso externo\r\n"
            "RRULE:FREQ=WEEKLY;BYDAY=MO,WE\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n");
        const auto extBack = IcsCodec::decodeEvent(extIcs);
        QVERIFY(extBack.has_value());
        QVERIFY(!extBack->block.specificDate.isValid());
        QVERIFY(extBack->block.startDate.isValid());
        const QDate expectedExtDate = QDateTime(QDate(2026, 11, 22), QTime(15, 0), QTimeZone::UTC).toTimeZone(QTimeZone::systemTimeZone()).date();
        QCOMPARE(extBack->block.startDate, expectedExtDate);
    }

    void quadrantRoundTrips()
    {
        Task t;
        t.uid = "eq-1";
        t.title = "Definir arquitetura";
        t.quadrant = 2;
        t.priority = Task::priorityForQuadrant(2);
        t.estimatedMinutes = 50;
        t.trackedSeconds = 1800;
        t.pomodorosCompleted = 2;

        const QString ics = IcsCodec::encode(t);
        static const QRegularExpression quadRe(QStringLiteral("X-KTASKS-QUADRANT[^:\\n]*:2"));
        QVERIFY2(ics.contains(quadRe), qPrintable(ics));
        QVERIFY(ics.contains("PRIORITY:5"));

        const auto back = IcsCodec::decode(ics);
        QVERIFY(back.has_value());
        QCOMPARE(back->quadrant, 2);
        QCOMPARE(back->priorityLevel(), 2);
        QCOMPARE(back->estimatedMinutes, 50);
        QCOMPARE(back->trackedSeconds, 1800);
        QCOMPARE(back->pomodorosCompleted, 2);

        Task cleared = *back;
        cleared.baseIcs = ics;
        cleared.quadrant = 0;
        cleared.priority = 0;
        cleared.estimatedMinutes = 0;
        cleared.trackedSeconds = 0;
        cleared.pomodorosCompleted = 0;
        const QString clearedIcs = IcsCodec::encode(cleared);
        QVERIFY(!clearedIcs.contains("X-KTASKS-QUADRANT"));
        QCOMPARE(IcsCodec::decode(clearedIcs)->quadrant, 0);
        QCOMPARE(IcsCodec::decode(clearedIcs)->trackedSeconds, 0);
    }

    void routineFrequencyAndIntervalRoundTrip()
    {
        auto roundTrip = [](const QString &freq, int interval, int daysMask) {
            RoutineBlock b;
            b.uid = QStringLiteral("rt-") + freq;
            b.title = QStringLiteral("Bloco ") + freq;
            b.startDate = QDate(2026, 11, 2);
            b.repeatFreq = freq;
            b.repeatInterval = interval;
            b.daysMask = daysMask;
            const QString ics = IcsCodec::encodeEvent(b, {});
            const auto dec = IcsCodec::decodeEvent(ics);
            QVERIFY(dec.has_value());
            QCOMPARE(dec->block.repeatFreq, freq);
            QCOMPARE(dec->block.repeatInterval, interval);
            if (freq == QLatin1String("WEEKLY")) {
                QCOMPARE(dec->block.daysMask, daysMask);
            }
        };
        roundTrip(QStringLiteral("DAILY"), 3, 127);
        roundTrip(QStringLiteral("WEEKLY"), 2, (1 << 0) | (1 << 2));
        roundTrip(QStringLiteral("MONTHLY"), 2, 127);
        roundTrip(QStringLiteral("YEARLY"), 1, 127);
    }
};

QTEST_GUILESS_MAIN(IcsCodecTest)
#include "icscodectest.moc"
