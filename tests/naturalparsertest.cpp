// SPDX-License-Identifier: GPL-3.0-or-later
#include "naturalparser.h"

#include <QTest>

using namespace KTasks;

// Tuesday, 17 November 2026, 10:00
static const QDateTime kNow(QDate(2026, 11, 17), QTime(10, 0));

class NaturalParserTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parses_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QDate>("date");
        QTest::addColumn<QTime>("time");
        QTest::addColumn<int>("priority");
        QTest::addColumn<QStringList>("tags");
        QTest::addColumn<QString>("rrule");

        const QDate today = kNow.date();
        auto row = [](const char *in, const char *title, QDate d = {}, QTime t = {}, int p = 0, QStringList tags = {}, const char *rrule = "") {
            QTest::newRow(in) << QString::fromUtf8(in) << QString::fromUtf8(title) << d << t << p << tags << QString::fromLatin1(rrule);
        };

        row("Comprar pão", "Comprar pão");
        row("Pagar boleto amanhã 14h !alta #financas", "Pagar boleto", today.addDays(1), QTime(14, 0), 3, {"financas"});
        row("Pagar boleto amanhã às 14h30", "Pagar boleto", today.addDays(1), QTime(14, 30));
        row("Ligar para o João hoje", "Ligar para o João", today);
        row("Entregar relatório até sexta", "Entregar relatório", QDate(2026, 11, 20));
        row("Reunião na próxima segunda-feira às 9", "Reunião", QDate(2026, 11, 23), QTime(9, 0));
        row("Dentista terça 15:00", "Dentista", QDate(2026, 11, 24), QTime(15, 0)); // today is Tuesday: next week
        row("Consulta depois de amanhã", "Consulta", today.addDays(2));
        row("Renovar seguro em 3 semanas", "Renovar seguro", today.addDays(21));
        row("Revisar contrato daqui a 2 dias", "Revisar contrato", today.addDays(2));
        row("Viagem 25/12", "Viagem", QDate(2026, 12, 25));
        row("Imposto 10/01", "Imposto", QDate(2027, 1, 10)); // already past this year
        row("Festa 05/03/2027", "Festa", QDate(2027, 3, 5));
        row("Aniversário 3 de março", "Aniversário", QDate(2027, 3, 3));
        row("Prova dia 30", "Prova", QDate(2026, 11, 30));
        row("Planejar semana que vem", "Planejar", today.addDays(7));
        row("Academia todo dia 7h", "Academia", today.addDays(1), QTime(7, 0), 0, {}, "FREQ=DAILY"); // 7h already passed today
        row("Tirar o lixo toda segunda", "Tirar o lixo", QDate(2026, 11, 23), {}, 0, {}, "FREQ=WEEKLY;BYDAY=MO");
        row("Backup a cada 2 semanas", "Backup", today, {}, 0, {}, "FREQ=WEEKLY;INTERVAL=2");
        row("Aluguel todo mês dia 5", "Aluguel", QDate(2026, 12, 5), {}, 0, {}, "FREQ=MONTHLY");
        row("Standup dias úteis 9h30", "Standup", today.addDays(1), QTime(9, 30), 0, {}, "FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
        row("Jantar hoje à noite", "Jantar", today, QTime(20, 0));
        row("Estudar amanhã à tarde #facul #prova", "Estudar", today.addDays(1), QTime(15, 0), 0, {"facul", "prova"});
        row("Almoço meio-dia", "Almoço", today, QTime(12, 0));
        row("Urgente !!!", "Urgente", {}, {}, 3);
        row("Ler livro !baixa", "Ler livro", {}, {}, 1);
        row("Revisar PR !2", "Revisar PR", {}, {}, 2);

        row("Buy milk tomorrow at 5pm #home", "Buy milk", today.addDays(1), QTime(17, 0), 0, {"home"});
        row("Call mom every friday at 6pm", "Call mom", QDate(2026, 11, 20), QTime(18, 0), 0, {}, "FREQ=WEEKLY;BYDAY=FR");
        row("Submit report by friday !high", "Submit report", QDate(2026, 11, 20), {}, 3);
        row("Pay rent on Dec 1", "Pay rent", QDate(2026, 12, 1));
        row("Party 4th of July 2027", "Party", QDate(2027, 7, 4));
        row("Review in 2 weeks", "Review", today.addDays(14));
        row("Standup every weekday 9:30am", "Standup", today.addDays(1), QTime(9, 30), 0, {}, "FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
        row("Lunch at noon", "Lunch", today, QTime(12, 0));
        row("Release 2027-02-01", "Release", QDate(2027, 2, 1));
        row("Meeting next week", "Meeting", today.addDays(7));
        row("Water plants every 3 days", "Water plants", today, {}, 0, {}, "FREQ=DAILY;INTERVAL=3");
        row("Treino toda segunda e quarta", "Treino", QDate(2026, 11, 18), {}, 0, {}, "FREQ=WEEKLY;BYDAY=MO,WE");
        row("Tomar remédio todo dia por 5 vezes", "Tomar remédio", today, {}, 0, {}, "FREQ=DAILY;COUNT=5");
        row("Pilates every mon, wed and fri 10 times", "Pilates", QDate(2026, 11, 18), {}, 0, {}, "FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=10");

        // Things that must NOT be misread
        row("Comprar 2 pães e 3 leites", "Comprar 2 pães e 3 leites");
        row("Ler a noite escura", "Ler a noite escura");
        row("Email fulano@example.com", "Email fulano@example.com");
        row("Issue C# compiler", "Issue C# compiler");
        row("Versão 1.2.3 do app", "Versão 1.2.3 do app");
    }

    void parses()
    {
        QFETCH(QString, input);
        QFETCH(QString, title);
        QFETCH(QDate, date);
        QFETCH(QTime, time);
        QFETCH(int, priority);
        QFETCH(QStringList, tags);
        QFETCH(QString, rrule);
        const ParsedInput p = parseNaturalInput(input, kNow);
        QCOMPARE(p.title, title);
        QCOMPARE(p.date, date);
        QCOMPARE(p.time, time);
        QCOMPARE(p.priorityLevel, priority);
        QCOMPARE(p.tags, tags);
        QCOMPARE(p.rrule, rrule);
    }

    void listName()
    {
        const ParsedInput p = parseNaturalInput("Comprar tinta @Casa amanhã", kNow);
        QCOMPARE(p.listName, QString("Casa"));
        QCOMPARE(p.title, QString("Comprar tinta"));
    }

    void monthFirst()
    {
        QCOMPARE(parseNaturalInput("Trip 12/25", kNow, true).date, QDate(2026, 12, 25));
        QCOMPARE(parseNaturalInput("Trip 3/4/2027", kNow, true).date, QDate(2027, 3, 4));
        QCOMPARE(parseNaturalInput("Trip 3/4/2027", kNow, false).date, QDate(2027, 4, 3));
        QCOMPARE(parseNaturalInput("Trip 25/12", kNow, true).date, QDate(2026, 12, 25)); // 25 cannot be a month
    }

    void onlyTokensKeepsOriginalAsTitle()
    {
        const ParsedInput p = parseNaturalInput("amanhã", kNow);
        QCOMPARE(p.title, QString("amanhã"));
        QCOMPARE(p.date, kNow.date().addDays(1));
    }

    void invalidDatesAreIgnored()
    {
        QVERIFY(!parseNaturalInput("Algo 31/02", kNow).date.isValid());
        QVERIFY(!parseNaturalInput("Algo 25h", kNow).time.isValid());
    }
};

QTEST_GUILESS_MAIN(NaturalParserTest)
#include "naturalparsertest.moc"
