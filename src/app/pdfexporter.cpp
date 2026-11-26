// SPDX-License-Identifier: GPL-3.0-or-later
#include "pdfexporter.h"
#include "eisenhowermanager.h"
#include "journalmanager.h"
#include "routinemanager.h"
#include "taskmodel.h"
#include "taskstore.h"

#include <KLocalizedString>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QPainter>
#include <QPdfWriter>
#ifdef KTASKS_HAVE_PRINTER
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>
#endif
#include <QTextDocument>
#include <algorithm>

using namespace KTasks;

PdfExporter::PdfExporter(TaskStore *store, TaskModel *taskModel, RoutineManager *routine, JournalManager *journal, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_taskModel(taskModel)
    , m_routine(routine)
    , m_journal(journal)
{
}

static QString esc(const QString &s)
{
    return s.toHtmlEscaped();
}

static QString markdownToSnippetHtml(const QString &md)
{
    if (md.trimmed().isEmpty()) {
        return {};
    }
    QTextDocument doc;
    doc.setMarkdown(md);
    QString html = doc.toHtml();
    const qsizetype bodyStart = html.indexOf(QLatin1String("<body"));
    if (bodyStart >= 0) {
        const qsizetype gt = html.indexOf(QLatin1Char('>'), bodyStart);
        const qsizetype bodyEnd = html.lastIndexOf(QLatin1String("</body>"));
        if (gt >= 0 && bodyEnd > gt) {
            return html.mid(gt + 1, bodyEnd - gt - 1);
        }
    }
    return esc(md).replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

static QString baseCss()
{
    return QStringLiteral(R"(
        body {
            font-family: 'Noto Sans', 'Liberation Sans', sans-serif;
            color: #232629;
            background: #ffffff;
            font-size: 10pt;
            margin: 0;
            padding: 0;
        }
        .header-box {
            border-bottom: 2px solid #3daee9;
            padding-bottom: 10px;
            margin-bottom: 14px;
        }
        .brand {
            font-size: 9pt;
            font-weight: 700;
            color: #1d99f3;
            letter-spacing: 0.6px;
            text-transform: uppercase;
        }
        h1 {
            font-size: 18pt;
            font-weight: 700;
            color: #232629;
            margin: 3px 0 4px 0;
        }
        .subtitle {
            font-size: 9pt;
            color: #7f8c8d;
        }
        .stats-bar {
            margin-top: 8px;
        }
        table.stats-table {
            border-collapse: collapse;
            margin-top: 8px;
        }
        td.stat-pill {
            background: #eff0f1;
            border: 1px solid #bdc3c7;
            padding: 3px 9px;
            font-size: 8.5pt;
            color: #232629;
            white-space: nowrap;
        }
        td.stat-pill-overdue {
            background: #fdecec;
            border: 1px solid #e79c9c;
            color: #b02a37;
        }
        td.stat-gap {
            width: 8px;
            padding: 0;
            border: none;
        }
        .group-title {
            font-size: 11.5pt;
            font-weight: 700;
            color: #1d99f3;
            background: #eff6fb;
            border-left: 4px solid #3daee9;
            padding: 5px 9px;
            margin: 14px 0 8px 0;
            page-break-after: avoid;
        }
        .task-card {
            border: 1px solid #dce1e5;
            border-radius: 5px;
            padding: 8px 10px;
            margin-bottom: 7px;
            background: #ffffff;
            page-break-inside: avoid;
        }
        .task-done {
            background: #f8f9fa;
            color: #7f8c8d;
        }
        .prio-high { border-left: 4px solid #da4453; }
        .prio-med  { border-left: 4px solid #f67400; }
        .prio-low  { border-left: 4px solid #27ae60; }
        .task-title {
            font-size: 10.5pt;
            font-weight: 600;
        }
        .strike {
            text-decoration: line-through;
            color: #7f8c8d;
        }
        .meta-line {
            margin-top: 4px;
            font-size: 8.5pt;
            color: #5d6770;
        }
        .badge {
            display: inline-block;
            border-radius: 3px;
            padding: 1px 6px;
            font-size: 8pt;
            margin-right: 5px;
            border: 1px solid #cfd5d9;
            background: #f4f6f8;
            color: #232629;
        }
        .badge-overdue { background: #fdecec; border-color: #e79c9c; color: #b02a37; }
        .badge-high { background: #fdecec; border-color: #e79c9c; color: #b02a37; font-weight: 600; }
        .badge-med  { background: #fff3e5; border-color: #f4be7e; color: #b85600; font-weight: 600; }
        .badge-low  { background: #eaf7ef; border-color: #9dd4b2; color: #1d7a46; font-weight: 600; }
        .badge-tag  { background: #eef6fc; border-color: #9fd0f0; color: #166ba0; }
        .subtasks {
            margin: 6px 0 0 18px;
            padding-left: 8px;
            border-left: 2px solid #e3e7ea;
            font-size: 9pt;
        }
        .subtask-item { margin: 2px 0; }
        .notes {
            margin-top: 6px;
            padding: 5px 8px;
            background: #f9fafb;
            border-radius: 3px;
            font-size: 9pt;
            color: #31363b;
        }
        table.routine {
            width: 100%;
            border-collapse: collapse;
            margin-top: 8px;
        }
        table.routine th {
            background: #eff0f1;
            color: #232629;
            text-align: left;
            padding: 6px 8px;
            border-bottom: 2px solid #3daee9;
            font-size: 9pt;
        }
        table.routine td {
            padding: 7px 8px;
            border-bottom: 1px solid #e3e7ea;
            vertical-align: top;
            font-size: 9.5pt;
        }
        .journal-body {
            line-height: 1.5;
            font-size: 10.5pt;
        }
        .empty {
            padding: 20px;
            text-align: center;
            color: #7f8c8d;
            border: 1px dashed #bdc3c7;
            border-radius: 5px;
        }
    )");
}

static QString viewDisplayTitle(const QString &scope, const TaskModel *model, const TaskStore *store)
{
    if (scope == QLatin1String("today")) {
        return i18n("Today's Tasks");
    }
    if (scope == QLatin1String("week")) {
        return i18n("Next 7 Days");
    }
    if (scope == QLatin1String("overdue")) {
        return i18n("Overdue Tasks");
    }
    if (scope == QLatin1String("all")) {
        return i18n("All Tasks");
    }
    if (scope == QLatin1String("matrix")) {
        return i18n("Priority Matrix (Eisenhower)");
    }
    if (scope == QLatin1String("routine")) {
        return i18n("Daily Routine");
    }
    if (scope == QLatin1String("journal")) {
        return i18n("Journal");
    }
    if (scope == QLatin1String("current") && model) {
        if (model->isListView()) {
            if (const TaskList *l = store->list(model->listId())) {
                return l->name;
            }
        }
        return model->title();
    }
    return i18n("Tasks");
}

QString PdfExporter::buildTasksHtml(const QString &scope, const QVariantMap &options) const
{
    const bool includeCompleted = options.value(QStringLiteral("includeCompleted"), false).toBool();
    const bool includeSubtasks = options.value(QStringLiteral("includeSubtasks"), true).toBool();
    const bool includeNotes = options.value(QStringLiteral("includeNotes"), true).toBool();
    const bool groupByDay = options.value(QStringLiteral("groupByDay"), scope == QLatin1String("week")).toBool();
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale;

    QString targetView = scope;
    if (scope == QLatin1String("current") && m_taskModel) {
        targetView = m_taskModel->view();
    }

    QList<const Task *> selected;
    int pendingCount = 0;
    int doneCount = 0;
    int overdueCount = 0;

    for (const Task &t : m_store->tasks()) {
        if (!t.parentUid.isEmpty() && targetView.startsWith(QLatin1String("list:"))) {
            continue; // subtasks rendered under parent in list view
        }
        if (!TaskModel::matchesView(t, targetView, now)) {
            continue;
        }
        if (t.isDone() && !includeCompleted && targetView != QLatin1String("completed")) {
            continue;
        }
        selected.append(&t);
        if (t.isDone()) {
            ++doneCount;
        } else {
            ++pendingCount;
            if (t.due.isValid() && (t.allDay ? t.due.date() < now.date() : t.due < now)) {
                ++overdueCount;
            }
        }
    }

    auto dueKey = [&](const Task *t) -> QDate {
        if (!t->due.isValid()) {
            return {};
        }
        return t->allDay ? t->due.date() : t->due.toLocalTime().date();
    };

    std::sort(selected.begin(), selected.end(), [&](const Task *a, const Task *b) {
        if (a->isDone() != b->isDone()) {
            return !a->isDone();
        }
        const QDate da = dueKey(a);
        const QDate db = dueKey(b);
        if (da != db) {
            if (!da.isValid()) {
                return false;
            }
            if (!db.isValid()) {
                return true;
            }
            return da < db;
        }
        if (a->priorityLevel() != b->priorityLevel()) {
            return a->priorityLevel() > b->priorityLevel();
        }
        return a->title.localeAwareCompare(b->title) < 0;
    });

    auto renderTask = [&](const Task *t) -> QString {
        QString cls = QStringLiteral("task-card");
        if (t->isDone()) {
            cls += QStringLiteral(" task-done");
        } else if (t->priorityLevel() == 3) {
            cls += QStringLiteral(" prio-high");
        } else if (t->priorityLevel() == 2) {
            cls += QStringLiteral(" prio-med");
        } else if (t->priorityLevel() == 1) {
            cls += QStringLiteral(" prio-low");
        }
        const QString check = t->isDone() ? QStringLiteral("☑") : QStringLiteral("☐");
        QString html = QStringLiteral("<div class=\"%1\"><div class=\"task-title %2\">%3 %4</div>")
                           .arg(cls, t->isDone() ? QStringLiteral("strike") : QString(), check, esc(t->title));

        QStringList badges;
        if (t->due.isValid()) {
            const bool isOver = !t->isDone() && (t->allDay ? t->due.date() < now.date() : t->due < now);
            badges << QStringLiteral("<span class=\"badge %1\">📅 %2</span>")
                          .arg(isOver ? QStringLiteral("badge-overdue") : QString(), esc(TaskModel::formatDue(*t, now)));
        }
        if (t->priorityLevel() == 3) {
            badges << QStringLiteral("<span class=\"badge badge-high\">! %1</span>").arg(esc(i18nc("priority", "High")));
        } else if (t->priorityLevel() == 2) {
            badges << QStringLiteral("<span class=\"badge badge-med\">! %1</span>").arg(esc(i18nc("priority", "Medium")));
        } else if (t->priorityLevel() == 1) {
            badges << QStringLiteral("<span class=\"badge badge-low\">! %1</span>").arg(esc(i18nc("priority", "Low")));
        }
        if (const TaskList *l = m_store->list(t->listId)) {
            badges << QStringLiteral("<span class=\"badge\">%1</span>").arg(esc(l->name));
        }
        if (!t->rrule.isEmpty()) {
            badges << QStringLiteral("<span class=\"badge\">↻ %1</span>").arg(esc(i18n("Recurring")));
        }
        for (const QString &tag : t->tags) {
            badges << QStringLiteral("<span class=\"badge badge-tag\">#%1</span>").arg(esc(tag));
        }
        if (!badges.isEmpty()) {
            html += QStringLiteral("<div class=\"meta-line\">%1</div>").arg(badges.join(QLatin1Char(' ')));
        }
        if (includeSubtasks) {
            const auto children = m_store->childrenOf(*t);
            if (!children.isEmpty()) {
                html += QStringLiteral("<div class=\"subtasks\">");
                for (const Task *c : children) {
                    if (!c->isDone() || includeCompleted) {
                        html += QStringLiteral("<div class=\"subtask-item %1\">%2 %3</div>")
                                    .arg(c->isDone() ? QStringLiteral("strike") : QString(),
                                         c->isDone() ? QStringLiteral("☑") : QStringLiteral("☐"),
                                         esc(c->title));
                    }
                }
                html += QStringLiteral("</div>");
            }
        }
        if (includeNotes && !t->notes.trimmed().isEmpty()) {
            html += QStringLiteral("<div class=\"notes\">%1</div>").arg(markdownToSnippetHtml(t->notes));
        }
        html += QStringLiteral("</div>");
        return html;
    };

    QString body;
    const QString title = viewDisplayTitle(scope, m_taskModel, m_store);
    const QString rangeLabel = (scope == QLatin1String("week"))
        ? QStringLiteral("%1 – %2").arg(locale.toString(now.date(), QLocale::ShortFormat), locale.toString(now.date().addDays(6), QLocale::ShortFormat))
        : locale.toString(now.date(), QStringLiteral("dddd, d MMMM yyyy"));

    body += QStringLiteral(
                "<div class=\"header-box\">"
                "<div class=\"brand\">KTasks</div>"
                "<h1>%1</h1>"
                "<div class=\"subtitle\">%2 · %3</div>"
                "<table class=\"stats-table\"><tr>"
                "<td class=\"stat-pill\">%4</td>"
                "<td class=\"stat-gap\"></td>"
                "<td class=\"stat-pill\">%5</td>"
                "%6"
                "</tr></table></div>")
                .arg(esc(title),
                     esc(rangeLabel),
                     esc(i18n("Generated %1", locale.toString(now, QLocale::ShortFormat))),
                     esc(i18np("%1 pending", "%1 pending", pendingCount)),
                     esc(i18np("%1 completed", "%1 completed", doneCount)),
                     overdueCount > 0 ? QStringLiteral("<td class=\"stat-gap\"></td><td class=\"stat-pill stat-pill-overdue\">%1</td>")
                                            .arg(esc(i18np("%1 overdue", "%1 overdue", overdueCount)))
                                      : QString());

    if (selected.isEmpty()) {
        body += QStringLiteral("<div class=\"empty\">%1</div>").arg(esc(i18n("No tasks to print for this selection.")));
    } else if (groupByDay) {
        QString currentGroup;
        for (const Task *t : selected) {
            const QDate d = dueKey(t);
            QString groupName;
            if (!d.isValid()) {
                groupName = i18n("No Date");
            } else if (d < now.date()) {
                groupName = i18n("Overdue");
            } else if (d == now.date()) {
                groupName = i18n("Today — %1", locale.toString(d, QStringLiteral("ddd, d MMM")));
            } else if (d == now.date().addDays(1)) {
                groupName = i18n("Tomorrow — %1", locale.toString(d, QStringLiteral("ddd, d MMM")));
            } else {
                groupName = locale.toString(d, QStringLiteral("dddd, d MMM"));
            }
            if (groupName != currentGroup) {
                currentGroup = groupName;
                body += QStringLiteral("<div class=\"group-title\">%1</div>").arg(esc(currentGroup));
            }
            body += renderTask(t);
        }
    } else {
        for (const Task *t : selected) {
            body += renderTask(t);
        }
    }

    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>%1</style></head><body>%2</body></html>").arg(baseCss(), body);
}

QString PdfExporter::buildMatrixHtml(const QVariantMap &options) const
{
    const bool includeCompleted = options.value(QStringLiteral("includeCompleted"), false).toBool();
    const QDateTime now = QDateTime::currentDateTime();
    const QLocale locale;

    QList<const Task *> buckets[5];
    int totalOpen = 0;
    for (const Task &t : m_store->tasks()) {
        if (t.isDone() && !includeCompleted) {
            continue;
        }
        if (!t.isDone()) {
            ++totalOpen;
        }
        const int b = EisenhowerManager::classifyBucket(t, now, 48);
        buckets[qBound(0, b, 4)].append(&t);
    }

    QString body = QStringLiteral(
                       "<div class=\"header-box\">"
                       "<div class=\"brand\">KTasks · %1</div>"
                       "<h1>%2</h1>"
                       "<div class=\"subtitle\">%3</div>"
                       "<table class=\"stats-table\"><tr>"
                       "<td class=\"stat-pill\">%4</td>"
                       "<td class=\"stat-gap\"></td>"
                       "<td class=\"stat-pill\">%5</td>"
                       "</tr></table></div>")
                       .arg(esc(i18n("Priority Matrix")),
                            esc(i18n("Priority Matrix (Eisenhower)")),
                            esc(i18n("Generated %1", locale.toString(now, QLocale::ShortFormat))),
                            esc(i18np("%1 open task", "%1 open tasks", totalOpen)),
                            esc(i18n("Q1: %1 · Q2: %2 · Q3: %3 · Q4: %4",
                                     buckets[1].size(), buckets[2].size(), buckets[3].size(), buckets[4].size())));

    auto renderQuadCell = [&](int q, const QString &title, const QString &sub, const QString &accent) {
        QString cell = QStringLiteral(
                           "<td style=\"width:50%%;vertical-align:top;padding:8px;border:1px solid #d0d7de;border-top:4px solid %1;\">"
                           "<div style=\"font-weight:700;font-size:10.5pt;color:%1;\">Q%2 · %3</div>"
                           "<div style=\"font-size:8.5pt;color:#5d6770;margin-bottom:6px;\">%4 (%5)</div>")
                           .arg(accent, QString::number(q), esc(title), esc(sub), QString::number(buckets[q].size()));
        if (buckets[q].isEmpty()) {
            cell += QStringLiteral("<div style=\"color:#7f8c8d;font-style:italic;font-size:9pt;\">%1</div>").arg(esc(i18n("Empty")));
        } else {
            for (const Task *t : buckets[q]) {
                const QString check = t->isDone() ? QStringLiteral("☑") : QStringLiteral("☐");
                const QString due = t->due.isValid() ? QStringLiteral(" <span style=\"color:#5d6770;font-size:8pt;\">(%1)</span>").arg(esc(TaskModel::formatDue(*t, now))) : QString();
                cell += QStringLiteral("<div style=\"margin:4px 0;font-size:9.5pt;%1\">%2 %3%4</div>")
                            .arg(t->isDone() ? QStringLiteral("text-decoration:line-through;color:#7f8c8d;") : QString(), check, esc(t->title), due);
            }
        }
        cell += QStringLiteral("</td>");
        return cell;
    };

    body += QStringLiteral("<table style=\"width:100%%;border-collapse:collapse;\"><tr>")
        + renderQuadCell(1, i18nc("quadrant title", "Do Now"), i18nc("quadrant axis", "Urgent & Important"), QStringLiteral("#e93d58"))
        + renderQuadCell(2, i18nc("quadrant title", "Schedule & Focus"), i18nc("quadrant axis", "Important · Not Urgent"), QStringLiteral("#2980b9"))
        + QStringLiteral("</tr><tr>")
        + renderQuadCell(3, i18nc("quadrant title", "Delegate / Quick Wins"), i18nc("quadrant axis", "Urgent · Less Important"), QStringLiteral("#f67400"))
        + renderQuadCell(4, i18nc("quadrant title", "Eliminate / Someday"), i18nc("quadrant axis", "Neither Urgent nor Important"), QStringLiteral("#7f8c8d"))
        + QStringLiteral("</tr></table>");

    if (!buckets[0].isEmpty()) {
        body += QStringLiteral("<div class=\"group-title\">%1 (%2)</div>")
                    .arg(esc(i18n("Awaiting Classification")), QString::number(buckets[0].size()));
        for (const Task *t : buckets[0]) {
            body += QStringLiteral("<div style=\"margin:3px 0;font-size:9pt;\">☐ %1</div>").arg(esc(t->title));
        }
    }
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>%1</style></head><body>%2</body></html>").arg(baseCss(), body);
}

QString PdfExporter::buildRoutineHtml(const QVariantMap &options) const
{
    Q_UNUSED(options)
    const QLocale locale;
    const QDate date = m_routine ? m_routine->selectedDate() : QDate::currentDate();
    const auto blocks = m_routine ? m_routine->blocksForDate(date) : QList<RoutineBlock>();
    const int doneCount = m_routine ? m_routine->completedBlocks() : 0;
    const int totalMins = m_routine ? m_routine->plannedMinutes() : 0;

    QString body = QStringLiteral(
                       "<div class=\"header-box\">"
                       "<div class=\"brand\">KTasks · %1</div>"
                       "<h1>%2</h1>"
                       "<div class=\"subtitle\">%3</div>"
                       "<table class=\"stats-table\"><tr>"
                       "<td class=\"stat-pill\">%4</td>"
                       "<td class=\"stat-gap\"></td>"
                       "<td class=\"stat-pill\">%5</td>"
                       "</tr></table></div>")
                       .arg(esc(i18n("Daily Routine")),
                            esc(locale.toString(date, QStringLiteral("dddd, d MMMM yyyy"))),
                            esc(i18n("Generated %1", locale.toString(QDateTime::currentDateTime(), QLocale::ShortFormat))),
                            esc(i18n("%1/%2 completed", doneCount, blocks.size())),
                            esc(totalMins >= 60
                                    ? (totalMins % 60 == 0 ? i18n("%1 h planned", totalMins / 60)
                                                           : i18n("%1 h %2 min planned", totalMins / 60, totalMins % 60))
                                    : i18n("%1 min planned", totalMins)));

    if (blocks.isEmpty()) {
        body += QStringLiteral("<div class=\"empty\">%1</div>").arg(esc(i18n("No routine blocks scheduled for this day.")));
    } else {
        body += QStringLiteral("<table class=\"routine\" width=\"100%\"><thead><tr><th>%1</th><th>%2</th><th>%3</th><th>%4</th></tr></thead><tbody>")
                    .arg(esc(i18n("Time")), esc(i18n("Activity")), esc(i18n("Category")), esc(i18n("Status")));
        for (const RoutineBlock &b : blocks) {
            const auto st = m_routine->statusForBlock(b.id, date);
            const QString stText = st == RoutineLogStatus::Done ? QStringLiteral("☑ ") + i18n("Done")
                : st == RoutineLogStatus::Skipped             ? QStringLiteral("↷ ") + i18n("Skipped")
                                                              : QStringLiteral("☐ ") + i18n("Planned");
            const QString timeStr = QStringLiteral("%1 – %2").arg(RoutineManager::formatMinutes(b.startMinute),
                                                                  RoutineManager::formatMinutes(qMin(1440, b.startMinute + b.durationMinutes)));
            const QString durText = b.durationMinutes >= 60
                ? (b.durationMinutes % 60 == 0 ? i18n("%1 h", b.durationMinutes / 60)
                                               : i18n("%1 h %2 min", b.durationMinutes / 60, b.durationMinutes % 60))
                : i18n("%1 min", b.durationMinutes);
            body += QStringLiteral("<tr><td style=\"white-space:nowrap;border-left:4px solid %1;\"><b>%2</b><br/><span style=\"color:#7f8c8d;font-size:8pt;\">%3</span></td>"
                                   "<td><b>%4</b>%5</td><td>%6</td><td>%7</td></tr>")
                        .arg(esc(b.color),
                             esc(timeStr),
                             esc(durText),
                             esc(b.title),
                             b.notes.trimmed().isEmpty() ? QString() : QStringLiteral("<div style=\"color:#5d6770;font-size:8.5pt;margin-top:2px;\">%1</div>").arg(esc(b.notes)),
                             esc(b.category),
                             esc(stText));
        }
        body += QStringLiteral("</tbody></table>");
    }
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>%1</style></head><body>%2</body></html>").arg(baseCss(), body);
}

QString PdfExporter::buildJournalHtml(const QVariantMap &options) const
{
    const qlonglong id = options.value(QStringLiteral("journalId")).toLongLong();
    const QVariantMap e = m_journal ? m_journal->entry(id) : QVariantMap();
    const QLocale locale;
    QString body;
    if (!e.value(QStringLiteral("exists")).toBool()) {
        body = QStringLiteral("<div class=\"empty\">%1</div>").arg(esc(i18n("Journal entry not found.")));
    } else if (e.value(QStringLiteral("locked")).toBool() && !e.value(QStringLiteral("unlocked")).toBool()) {
        body = QStringLiteral("<div class=\"empty\">%1</div>").arg(esc(i18n("Unlock the journal entry before exporting to PDF.")));
    } else {
        const QStringList tags = e.value(QStringLiteral("tags")).toStringList();
        QString tagHtml;
        for (const QString &t : tags) {
            tagHtml += QStringLiteral("<span class=\"badge badge-tag\">#%1</span> ").arg(esc(t));
        }
        body = QStringLiteral(
                   "<div class=\"header-box\">"
                   "<div class=\"brand\">KTasks · %1</div>"
                   "<h1>%2</h1>"
                   "<div class=\"subtitle\">%3</div>"
                   "%4</div>"
                   "<div class=\"journal-body\">%5</div>")
                   .arg(esc(i18n("Journal")),
                        esc(e.value(QStringLiteral("title")).toString()),
                        esc(e.value(QStringLiteral("entryDateText")).toString()),
                        tagHtml.isEmpty() ? QString() : QStringLiteral("<div class=\"meta-line\">%1</div>").arg(tagHtml),
                        markdownToSnippetHtml(e.value(QStringLiteral("content")).toString()));
    }
    return QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>%1</style></head><body>%2</body></html>").arg(baseCss(), body);
}

QString PdfExporter::buildHtml(const QVariantMap &options) const
{
    const QString scope = options.value(QStringLiteral("scope"), QStringLiteral("today")).toString();
    if (scope == QLatin1String("matrix")) {
        return buildMatrixHtml(options);
    }
    if (scope == QLatin1String("routine")) {
        return buildRoutineHtml(options);
    }
    if (scope == QLatin1String("journal")) {
        return buildJournalHtml(options);
    }
    return buildTasksHtml(scope, options);
}

void PdfExporter::renderToDevice(QPagedPaintDevice &printer, const QString &html, const QString &headerTitle) const
{
    QTextDocument doc;
    doc.setHtml(html);

    const QRectF pageRect = printer.pageLayout().paintRect(QPageLayout::Point);
    const qreal footerHeight = 26.0;
    const QSizeF bodySize(pageRect.width(), qMax<qreal>(100.0, pageRect.height() - footerHeight));
    doc.setPageSize(bodySize);

    QPainter painter(&printer);
    if (!painter.isActive()) {
        return;
    }

    const qreal scale = painter.viewport().width() / pageRect.width();
    painter.scale(scale, scale);

    const int pageCount = qMax(1, doc.pageCount());
    // Painter is already scaled to 1 logical unit = 1 Point (1/72 in); using pixelSize
    // prevents Qt from applying the HighResolution printer DPI a second time.
    QFont footerFont(QStringLiteral("Noto Sans"));
    footerFont.setPixelSize(8);
    const QString footerLeft = QStringLiteral("KTasks · %1").arg(headerTitle);

    for (int page = 0; page < pageCount; ++page) {
        if (page > 0) {
            printer.newPage();
        }
        painter.save();
        const QRectF viewRect(0, page * bodySize.height(), bodySize.width(), bodySize.height());
        painter.translate(0, -viewRect.top());
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.clip = viewRect;
        doc.documentLayout()->draw(&painter, ctx);
        painter.restore();

        // Footer
        painter.save();
        painter.setFont(footerFont);
        painter.setPen(QColor(QStringLiteral("#7f8c8d")));
        const qreal lineY = bodySize.height() + 5.0;
        painter.drawLine(QPointF(0, lineY), QPointF(bodySize.width(), lineY));
        const QRectF footerRect(0, lineY + 3.0, bodySize.width(), footerHeight - 8.0);
        painter.drawText(footerRect, Qt::AlignLeft | Qt::AlignVCenter, footerLeft);
        painter.drawText(footerRect, Qt::AlignRight | Qt::AlignVCenter, i18nc("page X of Y", "Page %1 of %2", page + 1, pageCount));
        painter.restore();
    }
}

bool PdfExporter::exportPdf(const QUrl &fileUrl, const QVariantMap &options)
{
    QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        return false;
    }
    if (!path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".pdf");
    }
    QPdfWriter writer(path);
    writer.setResolution(1200);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
    writer.setTitle(QFileInfo(path).completeBaseName());
    writer.setCreator(QStringLiteral("KTasks"));

    const QString scope = options.value(QStringLiteral("scope"), QStringLiteral("today")).toString();
    renderToDevice(writer, buildHtml(options), viewDisplayTitle(scope, m_taskModel, m_store));
    Q_EMIT message(i18n("PDF exported to %1", path), false);
    return true;
}

bool PdfExporter::print(const QVariantMap &options)
{
#ifndef KTASKS_HAVE_PRINTER
    Q_UNUSED(options)
    Q_EMIT message(i18n("Printing is not available on this device; export a PDF instead."), true);
    return false;
#else
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
    QPrintDialog dlg(&printer);
    dlg.setWindowTitle(i18nc("@title:window", "Print — KTasks"));
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    const QString scope = options.value(QStringLiteral("scope"), QStringLiteral("today")).toString();
    renderToDevice(printer, buildHtml(options), viewDisplayTitle(scope, m_taskModel, m_store));
    return true;
#endif
}

void PdfExporter::printPreview(const QVariantMap &options)
{
#ifndef KTASKS_HAVE_PRINTER
    Q_UNUSED(options)
    Q_EMIT message(i18n("Printing is not available on this device; export a PDF instead."), true);
#else
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QPageSize::A4));
    printer.setPageMargins(QMarginsF(14, 14, 14, 14), QPageLayout::Millimeter);
    const QString html = buildHtml(options);
    const QString scope = options.value(QStringLiteral("scope"), QStringLiteral("today")).toString();
    const QString headerTitle = viewDisplayTitle(scope, m_taskModel, m_store);

    QPrintPreviewDialog dlg(&printer);
    dlg.setWindowTitle(i18nc("@title:window", "Print Preview — KTasks"));
    connect(&dlg, &QPrintPreviewDialog::paintRequested, this, [this, html, headerTitle](QPrinter *p) {
        renderToDevice(*p, html, headerTitle);
    });
    dlg.exec();
#endif
}
