// SPDX-License-Identifier: GPL-3.0-or-later
#include "naturalparser.h"

#include <QRegularExpression>
#include <QVector>

namespace KTasks
{

namespace
{

// Lower-cases and strips diacritics while keeping the string length, so
// match positions are valid in the original text too.
QString fold(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        QChar c = ch.toLower();
        if (c.decompositionTag() == QChar::Canonical) {
            const QString d = c.decomposition();
            if (!d.isEmpty()) {
                c = d.at(0);
            }
        }
        out.append(c);
    }
    return out;
}

const QStringList kWeekdaysPt{QStringLiteral("segunda"), QStringLiteral("terca"),  QStringLiteral("quarta"), QStringLiteral("quinta"),
                              QStringLiteral("sexta"),   QStringLiteral("sabado"), QStringLiteral("domingo")};
const QStringList kWeekdaysPtShort{QStringLiteral("seg"), QStringLiteral("ter"), QStringLiteral("qua"), QStringLiteral("qui"),
                                   QStringLiteral("sex"), QStringLiteral("sab"), QStringLiteral("dom")};
const QStringList kWeekdaysEn{QStringLiteral("monday"), QStringLiteral("tuesday"),  QStringLiteral("wednesday"), QStringLiteral("thursday"),
                              QStringLiteral("friday"), QStringLiteral("saturday"), QStringLiteral("sunday")};
const QStringList kWeekdaysEnShort{QStringLiteral("mon"), QStringLiteral("tue"), QStringLiteral("wed"), QStringLiteral("thu"),
                                   QStringLiteral("fri"), QStringLiteral("sat"), QStringLiteral("sun")};
const QStringList kByDay{QStringLiteral("MO"), QStringLiteral("TU"), QStringLiteral("WE"), QStringLiteral("TH"),
                         QStringLiteral("FR"), QStringLiteral("SA"), QStringLiteral("SU")};
const QStringList kMonthsPt{QStringLiteral("janeiro"), QStringLiteral("fevereiro"), QStringLiteral("marco"),    QStringLiteral("abril"),
                            QStringLiteral("maio"),    QStringLiteral("junho"),     QStringLiteral("julho"),    QStringLiteral("agosto"),
                            QStringLiteral("setembro"), QStringLiteral("outubro"),  QStringLiteral("novembro"), QStringLiteral("dezembro")};
const QStringList kMonthsEn{QStringLiteral("january"), QStringLiteral("february"), QStringLiteral("march"),    QStringLiteral("april"),
                            QStringLiteral("may"),     QStringLiteral("june"),     QStringLiteral("july"),     QStringLiteral("august"),
                            QStringLiteral("september"), QStringLiteral("october"), QStringLiteral("november"), QStringLiteral("december")};

// 1-7 (Monday-Sunday) or 0
int weekdayIndex(const QString &word)
{
    int i = kWeekdaysPt.indexOf(word);
    if (i < 0) {
        i = kWeekdaysEn.indexOf(word);
    }
    if (i < 0) {
        i = kWeekdaysPtShort.indexOf(word);
    }
    if (i < 0) {
        i = kWeekdaysEnShort.indexOf(word);
    }
    return i + 1;
}

// 1-12 or 0; accepts full names and 3-letter abbreviations in both languages
int monthIndex(const QString &word)
{
    for (const QStringList *names : {&kMonthsPt, &kMonthsEn}) {
        for (int i = 0; i < names->size(); ++i) {
            const QString &name = names->at(i);
            if (word == name || (word.size() == 3 && name.startsWith(word))) {
                return i + 1;
            }
        }
    }
    return 0;
}

QString unitToFreq(const QString &unit)
{
    if (unit.startsWith(QLatin1String("dia")) || unit.startsWith(QLatin1String("day"))) {
        return QStringLiteral("DAILY");
    }
    if (unit.startsWith(QLatin1String("semana")) || unit.startsWith(QLatin1String("week"))) {
        return QStringLiteral("WEEKLY");
    }
    if (unit.startsWith(QLatin1String("mes")) || unit.startsWith(QLatin1String("month"))) {
        return QStringLiteral("MONTHLY");
    }
    return QStringLiteral("YEARLY");
}

QDate addUnits(const QDate &d, int n, const QString &unit)
{
    const QString freq = unitToFreq(unit);
    if (freq == QLatin1String("DAILY")) {
        return d.addDays(n);
    }
    if (freq == QLatin1String("WEEKLY")) {
        return d.addDays(7 * n);
    }
    if (freq == QLatin1String("MONTHLY")) {
        return d.addMonths(n);
    }
    return d.addYears(n);
}

// Next date falling on the given weekday. @p allowToday: today counts.
QDate nextWeekday(const QDate &today, int weekday, bool allowToday)
{
    int delta = (weekday - today.dayOfWeek() + 7) % 7;
    if (delta == 0 && !allowToday) {
        delta = 7;
    }
    return today.addDays(delta);
}

class Scanner
{
public:
    explicit Scanner(const QString &text)
        : m_original(text)
        , m_folded(fold(text))
        , m_used(text.size(), false)
    {
    }

    // First match of @p pattern that does not overlap an earlier match; marks it consumed.
    QRegularExpressionMatch take(const QString &pattern)
    {
        const QRegularExpression re(pattern, QRegularExpression::UseUnicodePropertiesOption);
        auto it = re.globalMatch(m_folded);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength() == 0 || overlaps(m.capturedStart(), m.capturedEnd())) {
                continue;
            }
            for (qsizetype i = m.capturedStart(); i < m.capturedEnd(); ++i) {
                m_used[i] = true;
            }
            return m;
        }
        return {};
    }

    QString original(const QRegularExpressionMatch &m, int group) const
    {
        return m_original.mid(m.capturedStart(group), m.capturedLength(group));
    }

    QString remainder() const
    {
        QString out;
        for (qsizetype i = 0; i < m_original.size(); ++i) {
            out.append(m_used.at(i) ? QLatin1Char(' ') : m_original.at(i));
        }
        return out.simplified();
    }

private:
    bool overlaps(qsizetype from, qsizetype to) const
    {
        for (qsizetype i = from; i < to; ++i) {
            if (m_used.at(i)) {
                return true;
            }
        }
        return false;
    }

    QString m_original;
    QString m_folded;
    QVector<bool> m_used;
};

// Word boundaries that also treat '#', '@' and '!' as part of a word.
#define B_ "(?<![\\w#@!])"
#define E_ "(?!\\w)"
// Optional connector in front of a date ("até sexta", "by friday").
#define PRE_ "(?:(?:ate|para|pra|by|until|due|on|em|no|na)\\s+)?"
#define WD_PT_ "segunda|terca|quarta|quinta|sexta|sabado|domingo"
#define WD_EN_ "monday|tuesday|wednesday|thursday|friday|saturday|sunday"
#define WD_ANY_ "segunda|terca|quarta|quinta|sexta|sabado|domingo|seg|ter|qua|qui|sex|sab|dom|monday|tuesday|wednesday|thursday|friday|saturday|sunday|mon|tue|wed|thu|fri|sat|sun"
#define MON_PT_ "janeiro|fevereiro|marco|abril|maio|junho|julho|agosto|setembro|outubro|novembro|dezembro|jan|fev|mar|abr|mai|jun|jul|ago|set|out|nov|dez"
#define MON_EN_ "january|february|march|april|may|june|july|august|september|october|november|december|jan|feb|mar|apr|jun|jul|aug|sep|oct|nov|dec"

QDate resolveYearless(const QDate &today, int month, int day)
{
    QDate d(today.year(), month, day);
    if (!d.isValid()) {
        return {};
    }
    if (d < today) {
        d = QDate(today.year() + 1, month, day);
    }
    return d;
}

int fullYear(int y)
{
    return y < 100 ? 2000 + y : y;
}

}

ParsedInput parseNaturalInput(const QString &text, const QDateTime &now, bool monthFirst)
{
    ParsedInput out;
    const QDate today = now.date();
    Scanner sc(text);
    QRegularExpressionMatch m;

    // --- tags, list, priority ---
    while ((m = sc.take(QStringLiteral("(?<!\\S)#([\\w][\\w-]*)"))).hasMatch()) {
        const QString tag = sc.original(m, 1);
        if (!out.tags.contains(tag, Qt::CaseInsensitive)) {
            out.tags.append(tag);
        }
    }
    if ((m = sc.take(QStringLiteral("(?<!\\S)@([\\w][\\w-]*)"))).hasMatch()) {
        out.listName = sc.original(m, 1);
    }
    if ((m = sc.take(QStringLiteral("(?<!\\S)!(alta|high|urgente|urgent|media|medium|normal|baixa|low|[123])" E_))).hasMatch()) {
        const QString p = m.captured(1);
        if (p == QLatin1String("alta") || p == QLatin1String("high") || p.startsWith(QLatin1String("urgent")) || p == QLatin1String("1")) {
            out.priorityLevel = 3;
        } else if (p == QLatin1String("baixa") || p == QLatin1String("low") || p == QLatin1String("3")) {
            out.priorityLevel = 1;
        } else {
            out.priorityLevel = 2;
        }
    } else if ((m = sc.take(QStringLiteral("(?<!\\S)(!{1,3})(?!\\S)"))).hasMatch()) {
        out.priorityLevel = m.capturedLength(1);
    }

    // --- recurrence ---
    bool weekdayFromRecurrence = false;
    if ((m = sc.take(QStringLiteral(B_ "(?:(?:todos os|nos|every)\\s+)?(?:dias uteis|weekdays?)" E_))).hasMatch()) {
        out.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
        QDate d = today;
        while (d.dayOfWeek() > 5) {
            d = d.addDays(1);
        }
        out.date = d;
        weekdayFromRecurrence = true;
    } else if ((m = sc.take(QStringLiteral(B_ "(?:tod[ao]s?\\s+(?:[ao]s\\s+)?|every\\s+)(" WD_ANY_ ")(?:-feira)?s?(?:(?:\\s*,\\s*|\\s+(?:e|and)\\s+)(" WD_ANY_ ")(?:-feira)?s?)+" E_))).hasMatch()) {
        static const QRegularExpression wdToken(QStringLiteral(B_ "(" WD_ANY_ ")(?:-feira)?s?" E_));
        QList<int> days;
        auto it = wdToken.globalMatch(m.captured(0));
        while (it.hasNext()) {
            const int wd = weekdayIndex(it.next().captured(1));
            if (wd > 0 && !days.contains(wd)) {
                days.append(wd);
            }
        }
        QStringList byDays;
        QDate earliest;
        for (int i = 0; i < 7; ++i) {
            if (days.contains(i + 1)) {
                byDays.append(kByDay.at(i));
                const QDate cand = nextWeekday(today, i + 1, true);
                if (!earliest.isValid() || cand < earliest) {
                    earliest = cand;
                }
            }
        }
        if (!byDays.isEmpty()) {
            out.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=") + byDays.join(QLatin1Char(','));
            out.date = earliest;
            weekdayFromRecurrence = true;
        }
    } else if ((m = sc.take(QStringLiteral(B_ "(?:a cada|every)\\s+(\\d+)\\s+(dia|semana|mes|ano|day|week|month|year)(?:es|s)?" E_))).hasMatch()) {
        const int n = qMax(1, m.captured(1).toInt());
        out.rrule = QStringLiteral("FREQ=") + unitToFreq(m.captured(2));
        if (n > 1) {
            out.rrule += QStringLiteral(";INTERVAL=%1").arg(n);
        }
    } else if ((m = sc.take(QStringLiteral(B_ "(?:tod[ao]s?\\s+(?:[ao]s\\s+)?(" WD_PT_ ")(?:-feira)?s?|every\\s+(" WD_EN_ "))" E_))).hasMatch()) {
        const int wd = weekdayIndex(m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
        out.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=") + kByDay.at(wd - 1);
        out.date = nextWeekday(today, wd, true);
        weekdayFromRecurrence = true;
    } else if ((m = sc.take(QStringLiteral(B_ "(?:todos os dias|todo dia|diariamente|every day|daily)" E_))).hasMatch()) {
        out.rrule = QStringLiteral("FREQ=DAILY");
    } else if ((m = sc.take(QStringLiteral(B_ "(?:todas as semanas|toda semana|semanalmente|every week|weekly)" E_))).hasMatch()) {
        out.rrule = QStringLiteral("FREQ=WEEKLY");
    } else if ((m = sc.take(QStringLiteral(B_ "(?:todos os meses|todo mes|mensalmente|every month|monthly)" E_))).hasMatch()) {
        out.rrule = QStringLiteral("FREQ=MONTHLY");
    } else if ((m = sc.take(QStringLiteral(B_ "(?:todos os anos|todo ano|anualmente|every year|yearly|annually)" E_))).hasMatch()) {
        out.rrule = QStringLiteral("FREQ=YEARLY");
    }
    if (!out.rrule.isEmpty() && (m = sc.take(QStringLiteral(B_ "(?:(?:por|durante|for)\\s+)?(\\d+)\\s+(?:vezes|vez|times?)" E_))).hasMatch()) {
        const int cnt = qMax(1, m.captured(1).toInt());
        out.rrule += QStringLiteral(";COUNT=%1").arg(cnt);
    }

    // --- dates ---
    bool tonight = false;
    QDate date;
    if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:depois de amanha|day after tomorrow)" E_))).hasMatch()) {
        date = today.addDays(2);
    } else if ((m = sc.take(QStringLiteral(B_ "(?:hoje a noite|tonight)" E_))).hasMatch()) {
        date = today;
        tonight = true;
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:hoje|today)" E_))).hasMatch()) {
        date = today;
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:amanha|tomorrow)" E_))).hasMatch()) {
        date = today.addDays(1);
    } else if ((m = sc.take(QStringLiteral(B_ "(?:em|daqui a|in)\\s+(\\d+)\\s+(dia|semana|mes|ano|day|week|month|year)(?:es|s)?" E_))).hasMatch()) {
        date = addUnits(today, m.captured(1).toInt(), m.captured(2));
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:(?:proxim[ao]|next)\\s+(semana|mes|ano|week|month|year)|(semana|mes|ano) que vem)" E_))).hasMatch()) {
        date = addUnits(today, 1, m.captured(1).isEmpty() ? m.captured(2) : m.captured(1));
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:(?:este|neste|no|this)\\s+)?(?:fim de semana|weekend)" E_))).hasMatch()) {
        date = nextWeekday(today, 6, true);
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(\\d{4})-(\\d{2})-(\\d{2})" E_))).hasMatch()) {
        date = QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:dia\\s+)?(\\d{1,2})/(\\d{1,2})(?:/(\\d{4}|\\d{2}))?" E_))).hasMatch()) {
        const int a = m.captured(1).toInt();
        const int b = m.captured(2).toInt();
        // Read in the locale's order unless that gives an impossible month.
        const bool firstIsMonth = monthFirst ? a <= 12 : (b > 12 && a <= 12);
        const int day = firstIsMonth ? b : a;
        const int month = firstIsMonth ? a : b;
        date = m.captured(3).isEmpty() ? resolveYearless(today, month, day) : QDate(fullYear(m.captured(3).toInt()), month, day);
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(?:dia\\s+)?(\\d{1,2})\\s+de\\s+(" MON_PT_ ")(?:\\s+de\\s+(\\d{4}))?" E_))).hasMatch()) {
        const int month = monthIndex(m.captured(2));
        const int day = m.captured(1).toInt();
        date = m.captured(3).isEmpty() ? resolveYearless(today, month, day) : QDate(m.captured(3).toInt(), month, day);
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(" MON_EN_ ")\\s+(\\d{1,2})(?:st|nd|rd|th)?(?:,?\\s+(\\d{4}))?" E_))).hasMatch()) {
        const int month = monthIndex(m.captured(1));
        const int day = m.captured(2).toInt();
        date = m.captured(3).isEmpty() ? resolveYearless(today, month, day) : QDate(m.captured(3).toInt(), month, day);
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "(\\d{1,2})(?:st|nd|rd|th)?\\s+(?:of\\s+)?(" MON_EN_ ")(?:,?\\s+(\\d{4}))?" E_))).hasMatch()) {
        const int month = monthIndex(m.captured(2));
        const int day = m.captured(1).toInt();
        date = m.captured(3).isEmpty() ? resolveYearless(today, month, day) : QDate(m.captured(3).toInt(), month, day);
    } else if ((m = sc.take(QStringLiteral(B_ PRE_ "dia\\s+(\\d{1,2})" E_))).hasMatch()) {
        const int day = m.captured(1).toInt();
        QDate d(today.year(), today.month(), 1);
        for (int i = 0; i < 13 && !date.isValid(); ++i) {
            const QDate candidate(d.year(), d.month(), day);
            if (candidate.isValid() && candidate >= today) {
                date = candidate;
            }
            d = d.addMonths(1);
        }
    } else if (!weekdayFromRecurrence
               && (m = sc.take(QStringLiteral(B_ PRE_ "(?:(?:n[ao]|proxim[ao]|next|est[ae]|this)\\s+)?(" WD_PT_ "|" WD_EN_ ")(?:-feira)?" E_))).hasMatch()) {
        date = nextWeekday(today, weekdayIndex(m.captured(1)), false);
    }
    if (date.isValid()) {
        out.date = date;
    }

    // --- time ---
    QTime time;
    auto applyMeridiem = [](int hour, const QString &ampm) {
        if (ampm == QLatin1String("pm") && hour < 12) {
            return hour + 12;
        }
        if (ampm == QLatin1String("am") && hour == 12) {
            return 0;
        }
        return hour;
    };
    if ((m = sc.take(QStringLiteral(B_ "(?:(?:as|at)\\s+)?(\\d{1,2})(?::|h)(\\d{2})\\s?(am|pm)?" E_))).hasMatch()) {
        time = QTime(applyMeridiem(m.captured(1).toInt(), m.captured(3)), m.captured(2).toInt());
    } else if ((m = sc.take(QStringLiteral(B_ "(?:(?:as|at)\\s+)?(\\d{1,2})\\s?(am|pm)" E_))).hasMatch()) {
        time = QTime(applyMeridiem(m.captured(1).toInt(), m.captured(2)), 0);
    } else if ((m = sc.take(QStringLiteral(B_ "(?:(?:as|at)\\s+)?(\\d{1,2})\\s?(?:h|hs|horas?)" E_))).hasMatch()) {
        time = QTime(m.captured(1).toInt(), 0);
    } else if ((m = sc.take(QStringLiteral(B_ "(?:as|at)\\s+(\\d{1,2})(?![\\w/:.-])"))).hasMatch()) {
        time = QTime(m.captured(1).toInt(), 0);
    } else if ((m = sc.take(QStringLiteral(B_ "(?:(?:ao|at)\\s+)?(?:meio-dia|meio dia|noon|midday)" E_))).hasMatch()) {
        time = QTime(12, 0);
    } else if (out.date.isValid()) {
        // Parts of the day only count next to a date ("amanhã à tarde").
        if ((m = sc.take(QStringLiteral(B_ "(?:de manha|pela manha|in the morning)" E_))).hasMatch()) {
            time = QTime(9, 0);
        } else if ((m = sc.take(QStringLiteral(B_ "(?:a tarde|de tarde|in the afternoon)" E_))).hasMatch()) {
            time = QTime(15, 0);
        } else if ((m = sc.take(QStringLiteral(B_ "(?:a noite|de noite|in the evening)" E_))).hasMatch()) {
            time = QTime(20, 0);
        }
    }
    if (!time.isValid() && tonight) {
        time = QTime(20, 0);
    }
    if (time.isValid() && weekdayFromRecurrence && out.date == today && time <= now.time()) {
        // Today's occurrence is already over: start with the next one.
        const QStringList days = out.rrule.section(QLatin1String("BYDAY="), 1).split(QLatin1Char(','));
        QDate d = today.addDays(1);
        while (!days.contains(kByDay.at(d.dayOfWeek() - 1))) {
            d = d.addDays(1);
        }
        out.date = d;
    }
    if (time.isValid()) {
        out.time = time;
        if (!out.date.isValid()) {
            out.date = time > now.time() ? today : today.addDays(1);
        }
    }
    if (!out.rrule.isEmpty() && !out.date.isValid()) {
        out.date = today;
    }

    // --- title ---
    QString title = sc.remainder();
    static const QRegularExpression trailing(QStringLiteral("\\s+(?:até|ate|para|pra|by|until|due|on|at|às|as)$"),
                                             QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    title.remove(trailing);
    title = title.trimmed();
    while (title.endsWith(QLatin1Char(',')) || title.endsWith(QLatin1Char('-'))) {
        title.chop(1);
        title = title.trimmed();
    }
    out.title = title.isEmpty() ? text.simplified() : title;
    return out;
}

}
