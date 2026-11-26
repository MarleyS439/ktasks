// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>

class JournalManager;
class RoutineManager;
class TaskModel;
class TaskStore;
class QPagedPaintDevice;

/**
 * Generates KDE Breeze-styled printouts and PDF exports for tasks (Today,
 * Week / Next 7 days, Overdue, current view/list), daily routine, or journal
 * entries. PDFs are written with QPdfWriter everywhere; printing (desktop
 * only) uses Qt6::PrintSupport.
 */
class PdfExporter : public QObject
{
    Q_OBJECT
public:
    PdfExporter(TaskStore *store, TaskModel *taskModel, RoutineManager *routine, JournalManager *journal, QObject *parent = nullptr);

    Q_INVOKABLE bool exportPdf(const QUrl &fileUrl, const QVariantMap &options);
    Q_INVOKABLE bool print(const QVariantMap &options);
    Q_INVOKABLE void printPreview(const QVariantMap &options);

    /** Exposed for unit testing HTML generation. */
    QString buildHtml(const QVariantMap &options) const;

Q_SIGNALS:
    void message(const QString &text, bool error);

private:
    QString buildTasksHtml(const QString &scope, const QVariantMap &options) const;
    QString buildMatrixHtml(const QVariantMap &options) const;
    QString buildRoutineHtml(const QVariantMap &options) const;
    QString buildJournalHtml(const QVariantMap &options) const;
    void renderToDevice(QPagedPaintDevice &device, const QString &html, const QString &headerTitle) const;

    TaskStore *m_store;
    TaskModel *m_taskModel;
    RoutineManager *m_routine;
    JournalManager *m_journal;
};
