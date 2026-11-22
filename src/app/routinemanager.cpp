// SPDX-License-Identifier: GPL-3.0-or-later
#include "routinemanager.h"

#include <KLocalizedString>
#include <QLocale>
#include <algorithm>

using namespace KTasks;

RoutineManager::RoutineManager(Database *db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
    , m_selectedDate(QDate::currentDate())
{
    reload();
}

void RoutineManager::reload()
{
    m_accounts = m_db->accounts();
    m_blocks = m_db->routineBlocks(true, false, false);
    rebuildDay();
    Q_EMIT changed();
}

QString RoutineManager::accountNameFor(qint64 accountId) const
{
    if (accountId <= 0) {
        return {};
    }
    for (const Account &a : m_accounts) {
        if (a.id == accountId) {
            return a.name;
        }
    }
    return {};
}

QHash<int, QByteArray> RoutineManager::roleNames() const
{
    return {
        {BlockIdRole, "blockId"},
        {UidRole, "uid"},
        {TitleRole, "title"},
        {NotesRole, "notes"},
        {StartMinuteRole, "startMinute"},
        {DurationMinutesRole, "durationMinutes"},
        {TimeRangeRole, "timeRange"},
        {ColorRole, "color"},
        {CategoryRole, "category"},
        {StatusRole, "status"},
        {IsTemplateRole, "isTemplate"},
        {DaysMaskRole, "daysMask"},
        {LinkedTaskIdRole, "linkedTaskId"},
        {NotifyEnabledRole, "notifyEnabled"},
        {NotifyMinutesBeforeRole, "notifyMinutesBefore"},
        {SyncAccountIdRole, "syncAccountId"},
        {SyncAccountNameRole, "syncAccountName"},
        {SyncedRole, "synced"},
        {SyncPendingRole, "syncPending"},
    };
}

int RoutineManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_dayItems.size();
}

QVariant RoutineManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_dayItems.size()) {
        return {};
    }
    const DayItem &item = m_dayItems.at(index.row());
    const RoutineBlock &b = item.block;
    switch (role) {
    case BlockIdRole:
        return b.id;
    case UidRole:
        return b.uid;
    case Qt::DisplayRole:
    case TitleRole:
        return b.title;
    case NotesRole:
        return b.notes;
    case StartMinuteRole:
        return b.startMinute;
    case DurationMinutesRole:
        return b.durationMinutes;
    case TimeRangeRole:
        return QStringLiteral("%1 – %2").arg(formatMinutes(b.startMinute), formatMinutes(qMin(1440, b.startMinute + b.durationMinutes)));
    case ColorRole:
        return b.color;
    case CategoryRole:
        return b.category;
    case StatusRole:
        return int(item.status);
    case IsTemplateRole:
        return !b.specificDate.isValid();
    case DaysMaskRole:
        return b.daysMask;
    case LinkedTaskIdRole:
        return b.linkedTaskId;
    case NotifyEnabledRole:
        return b.notifyEnabled;
    case NotifyMinutesBeforeRole:
        return b.notifyMinutesBefore;
    case SyncAccountIdRole:
        return b.syncAccountId;
    case SyncAccountNameRole:
        return accountNameFor(b.syncAccountId);
    case SyncedRole:
        return b.syncAccountId > 0;
    case SyncPendingRole:
        return b.syncAccountId > 0 && b.syncState != SyncState::Synced;
    }
    return {};
}

QString RoutineManager::formatMinutes(int minutes)
{
    const int clamped = qBound(0, minutes, 1440);
    const int h = (clamped / 60) % 24;
    const int m = clamped % 60;
    if (clamped == 1440) {
        return QStringLiteral("24:00");
    }
    return QStringLiteral("%1:%2").arg(h, 2, 10, QLatin1Char('0')).arg(m, 2, 10, QLatin1Char('0'));
}

bool RoutineManager::matchesDate(const RoutineBlock &b, const QDate &date)
{
    if (!b.active || !date.isValid()) {
        return false;
    }
    if (b.specificDate.isValid()) {
        return b.specificDate == date;
    }
    if (b.startDate.isValid() && date < b.startDate) {
        return false;
    }
    const int bit = 1 << (date.dayOfWeek() - 1); // Qt dayOfWeek: 1=Mon..7=Sun
    return (b.daysMask & bit) != 0;
}

QList<RoutineBlock> RoutineManager::blocksForDate(const QDate &date) const
{
    QList<RoutineBlock> out;
    for (const RoutineBlock &b : m_blocks) {
        if (matchesDate(b, date)) {
            out.append(b);
        }
    }
    std::sort(out.begin(), out.end(), [](const RoutineBlock &a, const RoutineBlock &b) {
        return a.startMinute != b.startMinute ? a.startMinute < b.startMinute : a.id < b.id;
    });
    return out;
}

RoutineLogStatus RoutineManager::statusForBlock(qint64 blockId, const QDate &date) const
{
    const auto logs = m_db->routineLogsForDate(date);
    for (const RoutineLog &l : logs) {
        if (l.blockId == blockId) {
            return l.status;
        }
    }
    return RoutineLogStatus::Pending;
}

void RoutineManager::rebuildDay()
{
    beginResetModel();
    m_dayItems.clear();
    const auto logs = m_db->routineLogsForDate(m_selectedDate);
    QHash<qint64, RoutineLogStatus> statuses;
    for (const RoutineLog &l : logs) {
        statuses.insert(l.blockId, l.status);
    }
    for (const RoutineBlock &b : blocksForDate(m_selectedDate)) {
        m_dayItems.append(DayItem{b, statuses.value(b.id, RoutineLogStatus::Pending)});
    }
    endResetModel();
}

void RoutineManager::setSelectedDateIso(const QString &iso)
{
    const QDate d = QDate::fromString(iso, Qt::ISODate);
    if (d.isValid() && d != m_selectedDate) {
        m_selectedDate = d;
        rebuildDay();
        Q_EMIT selectedDateChanged();
        Q_EMIT changed();
    }
}

QString RoutineManager::selectedDateText() const
{
    const QDate today = QDate::currentDate();
    if (m_selectedDate == today) {
        return i18nc("routine date", "Today — %1", QLocale().toString(m_selectedDate, QStringLiteral("ddd, d MMM")));
    }
    if (m_selectedDate == today.addDays(1)) {
        return i18nc("routine date", "Tomorrow — %1", QLocale().toString(m_selectedDate, QStringLiteral("ddd, d MMM")));
    }
    if (m_selectedDate == today.addDays(-1)) {
        return i18nc("routine date", "Yesterday — %1", QLocale().toString(m_selectedDate, QStringLiteral("ddd, d MMM")));
    }
    return QLocale().toString(m_selectedDate, QStringLiteral("dddd, d MMM yyyy"));
}

int RoutineManager::completedBlocks() const
{
    int done = 0;
    for (const DayItem &item : m_dayItems) {
        if (item.status == RoutineLogStatus::Done) {
            ++done;
        }
    }
    return done;
}

int RoutineManager::plannedMinutes() const
{
    int mins = 0;
    for (const DayItem &item : m_dayItems) {
        if (item.status != RoutineLogStatus::Skipped) {
            mins += item.block.durationMinutes;
        }
    }
    return mins;
}

QVariantList RoutineManager::templates() const
{
    QVariantList out;
    for (const RoutineBlock &b : m_blocks) {
        if (b.specificDate.isValid()) {
            continue;
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), b.id},
            {QStringLiteral("title"), b.title},
            {QStringLiteral("notes"), b.notes},
            {QStringLiteral("startMinute"), b.startMinute},
            {QStringLiteral("durationMinutes"), b.durationMinutes},
            {QStringLiteral("timeRange"), QStringLiteral("%1 – %2").arg(formatMinutes(b.startMinute), formatMinutes(qMin(1440, b.startMinute + b.durationMinutes)))},
            {QStringLiteral("daysMask"), b.daysMask},
            {QStringLiteral("startDate"), b.startDate.isValid() ? b.startDate.toString(Qt::ISODate) : QString()},
            {QStringLiteral("startDateText"), b.startDate.isValid() ? QLocale().toString(b.startDate, QLocale::ShortFormat) : QString()},
            {QStringLiteral("color"), b.color},
            {QStringLiteral("category"), b.category},
            {QStringLiteral("notifyEnabled"), b.notifyEnabled},
            {QStringLiteral("notifyMinutesBefore"), b.notifyMinutesBefore},
            {QStringLiteral("active"), b.active},
            {QStringLiteral("syncAccountId"), b.syncAccountId},
            {QStringLiteral("syncAccountName"), accountNameFor(b.syncAccountId)},
            {QStringLiteral("synced"), b.syncAccountId > 0},
        });
    }
    return out;
}

void RoutineManager::goToday()
{
    setSelectedDateIso(QDate::currentDate().toString(Qt::ISODate));
}

void RoutineManager::shiftDays(int delta)
{
    setSelectedDateIso(m_selectedDate.addDays(delta).toString(Qt::ISODate));
}

void RoutineManager::shiftWeeks(int delta)
{
    setSelectedDateIso(m_selectedDate.addDays(delta * 7).toString(Qt::ISODate));
}

void RoutineManager::shiftMonths(int delta)
{
    setSelectedDateIso(m_selectedDate.addMonths(delta).toString(Qt::ISODate));
}

QString RoutineManager::weekTitle() const
{
    const QDate mon = m_selectedDate.addDays(-(m_selectedDate.dayOfWeek() - 1));
    const QDate sun = mon.addDays(6);
    const QLocale locale;
    if (mon.month() == sun.month()) {
        return QStringLiteral("%1 – %2").arg(mon.day()).arg(locale.toString(sun, QStringLiteral("d MMM yyyy")));
    }
    return QStringLiteral("%1 – %2").arg(locale.toString(mon, QStringLiteral("d MMM")), locale.toString(sun, QStringLiteral("d MMM yyyy")));
}

QString RoutineManager::monthTitle() const
{
    QString title = QLocale().toString(m_selectedDate, QStringLiteral("MMMM yyyy"));
    if (!title.isEmpty()) {
        title[0] = title.at(0).toUpper();
    }
    return title;
}

QVariantList RoutineManager::weekDays() const
{
    QVariantList days;
    const QDate mon = m_selectedDate.addDays(-(m_selectedDate.dayOfWeek() - 1));
    const QDate today = QDate::currentDate();
    const QLocale locale;
    for (int i = 0; i < 7; ++i) {
        const QDate d = mon.addDays(i);
        QHash<qint64, RoutineLogStatus> statuses;
        for (const RoutineLog &l : m_db->routineLogsForDate(d)) {
            statuses.insert(l.blockId, l.status);
        }
        QVariantList items;
        for (const RoutineBlock &b : blocksForDate(d)) {
            items.append(QVariantMap{
                {QStringLiteral("id"), b.id},
                {QStringLiteral("title"), b.title},
                {QStringLiteral("startMinute"), b.startMinute},
                {QStringLiteral("durationMinutes"), b.durationMinutes},
                {QStringLiteral("timeRange"), QStringLiteral("%1–%2").arg(formatMinutes(b.startMinute), formatMinutes(qMin(1440, b.startMinute + b.durationMinutes)))},
                {QStringLiteral("color"), b.color},
                {QStringLiteral("status"), int(statuses.value(b.id, RoutineLogStatus::Pending))},
            });
        }
        days.append(QVariantMap{
            {QStringLiteral("iso"), d.toString(Qt::ISODate)},
            {QStringLiteral("dayName"), locale.dayName(d.dayOfWeek(), QLocale::ShortFormat)},
            {QStringLiteral("dayNum"), d.day()},
            {QStringLiteral("isToday"), d == today},
            {QStringLiteral("isSelected"), d == m_selectedDate},
            {QStringLiteral("items"), items},
        });
    }
    return days;
}

QVariantList RoutineManager::monthCells() const
{
    QVariantList cells;
    const QDate first(m_selectedDate.year(), m_selectedDate.month(), 1);
    const QDate gridStart = first.addDays(-(first.dayOfWeek() - 1));
    const QDate today = QDate::currentDate();

    // Index open tasks by due date for quick calendar badges
    QHash<QDate, QStringList> tasksByDate;
    for (const Task &t : m_db->tasks(false)) {
        if (!t.isDone() && t.due.isValid()) {
            const QDate d = t.allDay ? t.due.date() : t.due.toLocalTime().date();
            tasksByDate[d].append(t.title);
        }
    }

    for (int i = 0; i < 42; ++i) {
        const QDate d = gridStart.addDays(i);
        QVariantList entries;
        for (const RoutineBlock &b : blocksForDate(d)) {
            entries.append(QVariantMap{
                {QStringLiteral("title"), QStringLiteral("%1 %2").arg(formatMinutes(b.startMinute), b.title)},
                {QStringLiteral("color"), b.color},
                {QStringLiteral("isTask"), false},
            });
        }
        const QStringList dayTasks = tasksByDate.value(d);
        for (const QString &tt : dayTasks) {
            entries.append(QVariantMap{
                {QStringLiteral("title"), tt},
                {QStringLiteral("color"), QStringLiteral("#f59e0b")},
                {QStringLiteral("isTask"), true},
            });
        }
        cells.append(QVariantMap{
            {QStringLiteral("iso"), d.toString(Qt::ISODate)},
            {QStringLiteral("dayNum"), d.day()},
            {QStringLiteral("inMonth"), d.month() == m_selectedDate.month()},
            {QStringLiteral("isToday"), d == today},
            {QStringLiteral("isSelected"), d == m_selectedDate},
            {QStringLiteral("entries"), entries},
        });
    }
    return cells;
}

static void applyBlockFields(RoutineBlock &b, const QVariantMap &fields, const QDate &fallbackDate)
{
    if (fields.contains(QStringLiteral("title"))) {
        b.title = fields.value(QStringLiteral("title")).toString().trimmed();
    }
    if (fields.contains(QStringLiteral("notes"))) {
        b.notes = fields.value(QStringLiteral("notes")).toString();
    }
    if (fields.contains(QStringLiteral("startMinute"))) {
        b.startMinute = qBound(0, fields.value(QStringLiteral("startMinute")).toInt(), 1435);
    }
    if (fields.contains(QStringLiteral("durationMinutes"))) {
        b.durationMinutes = qBound(5, fields.value(QStringLiteral("durationMinutes")).toInt(), 720);
    }
    if (fields.contains(QStringLiteral("isTemplate"))) {
        const bool isTemplate = fields.value(QStringLiteral("isTemplate")).toBool();
        if (isTemplate) {
            b.specificDate = QDate();
            if (b.daysMask == 0) {
                b.daysMask = 127;
            }
        } else {
            const QDate sd = QDate::fromString(fields.value(QStringLiteral("specificDate")).toString(), Qt::ISODate);
            b.specificDate = sd.isValid() ? sd : fallbackDate;
            b.startDate = QDate();
            b.daysMask = 0;
        }
    }
    if (fields.contains(QStringLiteral("daysMask"))) {
        b.daysMask = fields.value(QStringLiteral("daysMask")).toInt();
    }
    if (fields.contains(QStringLiteral("specificDate")) && !fields.contains(QStringLiteral("isTemplate"))) {
        const QString s = fields.value(QStringLiteral("specificDate")).toString();
        b.specificDate = s.isEmpty() ? QDate() : QDate::fromString(s, Qt::ISODate);
    }
    if (fields.contains(QStringLiteral("startDate"))) {
        const QString s = fields.value(QStringLiteral("startDate")).toString().trimmed();
        b.startDate = (b.specificDate.isValid() || s.isEmpty()) ? QDate() : QDate::fromString(s, Qt::ISODate);
    }
    if (b.specificDate.isValid()) {
        b.startDate = QDate();
    }
    if (fields.contains(QStringLiteral("linkedTaskId"))) {
        b.linkedTaskId = fields.value(QStringLiteral("linkedTaskId")).toLongLong();
    }
    if (fields.contains(QStringLiteral("color"))) {
        const QString c = fields.value(QStringLiteral("color")).toString().trimmed();
        b.color = c.isEmpty() ? QStringLiteral("#3daee9") : c;
    }
    if (fields.contains(QStringLiteral("category"))) {
        b.category = fields.value(QStringLiteral("category")).toString().trimmed();
    }
    if (fields.contains(QStringLiteral("notifyEnabled"))) {
        b.notifyEnabled = fields.value(QStringLiteral("notifyEnabled")).toBool();
    }
    if (fields.contains(QStringLiteral("notifyMinutesBefore"))) {
        b.notifyMinutesBefore = qBound(0, fields.value(QStringLiteral("notifyMinutesBefore")).toInt(), 120);
    }
    if (fields.contains(QStringLiteral("active"))) {
        b.active = fields.value(QStringLiteral("active")).toBool();
    }
    if (fields.contains(QStringLiteral("syncAccountId"))) {
        b.syncAccountId = qMax<qint64>(0, fields.value(QStringLiteral("syncAccountId")).toLongLong());
    }
}

qlonglong RoutineManager::addBlock(const QVariantMap &fields)
{
    RoutineBlock b;
    applyBlockFields(b, fields, m_selectedDate);
    if (b.title.isEmpty()) {
        return 0;
    }
    if (b.syncAccountId > 0) {
        b.syncState = SyncState::New;
    }
    if (!m_db->insertRoutineBlock(b)) {
        return 0;
    }
    reload();
    if (b.syncAccountId > 0) {
        Q_EMIT syncNeeded();
    }
    return b.id;
}

bool RoutineManager::updateBlock(qlonglong id, const QVariantMap &fields)
{
    const auto existing = m_db->routineBlock(id);
    if (!existing) {
        return false;
    }
    RoutineBlock b = *existing;
    const qint64 oldAcc = b.syncAccountId;
    const QString oldHref = b.href;
    const QString oldEtag = b.etag;
    applyBlockFields(b, fields, m_selectedDate);
    if (b.title.isEmpty()) {
        return false;
    }
    bool needSync = b.syncAccountId > 0;
    if (oldAcc != b.syncAccountId && oldAcc > 0 && !oldHref.isEmpty()) {
        m_db->queueRoutineRemoteDelete(oldAcc, oldHref, oldEtag);
        b.calendarHref.clear();
        b.href.clear();
        b.etag.clear();
        b.baseIcs.clear();
        needSync = true;
    }
    if (!m_db->updateRoutineBlockUser(b)) {
        return false;
    }
    reload();
    if (needSync) {
        Q_EMIT syncNeeded();
    }
    return true;
}

bool RoutineManager::setBlockSyncAccount(qlonglong id, qlonglong accountId)
{
    return updateBlock(id, {{QStringLiteral("syncAccountId"), accountId}});
}

bool RoutineManager::removeBlock(qlonglong id)
{
    const auto existing = m_db->routineBlock(id);
    const bool hadRemote = existing && existing->syncAccountId > 0 && !existing->href.isEmpty();
    if (!m_db->removeRoutineBlockUser(id)) {
        return false;
    }
    reload();
    if (hadRemote) {
        Q_EMIT syncNeeded();
    }
    return true;
}

void RoutineManager::setBlockStatus(qlonglong id, int status)
{
    setBlockStatusForDate(id, m_selectedDate, status);
}

void RoutineManager::setBlockStatusForDate(qlonglong id, const QDate &date, int status)
{
    if (!date.isValid()) {
        return;
    }
    const auto st = RoutineLogStatus(qBound(0, status, 2));
    if (m_db->setRoutineLog(id, date, st)) {
        if (const auto existing = m_db->routineBlock(id); existing && existing->syncAccountId > 0) {
            m_db->updateRoutineBlockUser(*existing);
            reload();
            Q_EMIT syncNeeded();
        } else {
            if (date == m_selectedDate) {
                rebuildDay();
            }
            Q_EMIT changed();
        }
    }
}

qlonglong RoutineManager::scheduleTask(qlonglong taskId, const QString &taskTitle, int startMinute, int durationMinutes, const QString &color)
{
    if (taskTitle.trimmed().isEmpty()) {
        return 0;
    }
    QVariantMap fields;
    fields[QStringLiteral("title")] = taskTitle.trimmed();
    fields[QStringLiteral("startMinute")] = startMinute;
    fields[QStringLiteral("durationMinutes")] = durationMinutes > 0 ? durationMinutes : 60;
    fields[QStringLiteral("isTemplate")] = false;
    fields[QStringLiteral("specificDate")] = m_selectedDate.toString(Qt::ISODate);
    fields[QStringLiteral("daysMask")] = 0;
    fields[QStringLiteral("linkedTaskId")] = taskId;
    fields[QStringLiteral("color")] = color.isEmpty() ? QStringLiteral("#27ae60") : color;
    fields[QStringLiteral("category")] = i18nc("routine category for scheduled task", "Task");
    fields[QStringLiteral("notifyEnabled")] = true;
    fields[QStringLiteral("notifyMinutesBefore")] = 5;
    return addBlock(fields);
}

QVariantMap RoutineManager::blockInfo(qlonglong id) const
{
    for (const RoutineBlock &b : m_blocks) {
        if (b.id == id) {
            return {
                {QStringLiteral("exists"), true},
                {QStringLiteral("id"), b.id},
                {QStringLiteral("title"), b.title},
                {QStringLiteral("notes"), b.notes},
                {QStringLiteral("startMinute"), b.startMinute},
                {QStringLiteral("durationMinutes"), b.durationMinutes},
                {QStringLiteral("isTemplate"), !b.specificDate.isValid()},
                {QStringLiteral("daysMask"), b.daysMask},
                {QStringLiteral("specificDate"), b.specificDate.isValid() ? b.specificDate.toString(Qt::ISODate) : QString()},
                {QStringLiteral("startDate"), b.startDate.isValid() ? b.startDate.toString(Qt::ISODate) : QString()},
                {QStringLiteral("startDateText"), b.startDate.isValid() ? QLocale().toString(b.startDate, QLocale::ShortFormat) : QString()},
                {QStringLiteral("linkedTaskId"), b.linkedTaskId},
                {QStringLiteral("color"), b.color},
                {QStringLiteral("category"), b.category},
                {QStringLiteral("notifyEnabled"), b.notifyEnabled},
                {QStringLiteral("notifyMinutesBefore"), b.notifyMinutesBefore},
                {QStringLiteral("active"), b.active},
                {QStringLiteral("syncAccountId"), b.syncAccountId},
                {QStringLiteral("syncAccountName"), accountNameFor(b.syncAccountId)},
                {QStringLiteral("synced"), b.syncAccountId > 0},
            };
        }
    }
    return {{QStringLiteral("exists"), false}};
}
