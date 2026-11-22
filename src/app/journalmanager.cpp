// SPDX-License-Identifier: GPL-3.0-or-later
#include "journalmanager.h"
#include "crypto.h"

#include <KLocalizedString>
#include <QFile>
#include <QLocale>

using namespace KTasks;

static QString foldText(const QString &s)
{
    QString out;
    const QString d = s.normalized(QString::NormalizationForm_D);
    out.reserve(d.size());
    for (const QChar c : d) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out;
}

JournalManager::JournalManager(Database *db, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(db)
{
    reload();
}

void JournalManager::ensureDefaultNotebook()
{
    m_notebooks = m_db->journalNotebooks();
    if (m_notebooks.isEmpty()) {
        JournalNotebook nb;
        nb.name = i18nc("default journal notebook name", "Personal");
        nb.color = QStringLiteral("#3daee9");
        m_db->insertJournalNotebook(nb);
        m_notebooks = m_db->journalNotebooks();
    }
}

void JournalManager::reload()
{
    ensureDefaultNotebook();
    m_accounts = m_db->accounts();
    m_entries = m_db->journalEntries(false);
    // Drop session keys for entries that no longer exist or are no longer locked.
    for (auto it = m_unlockedContent.begin(); it != m_unlockedContent.end();) {
        const int idx = indexById(it.key());
        if (idx < 0 || !m_entries.at(idx).isLocked) {
            m_unlockedPasswords.remove(it.key());
            it = m_unlockedContent.erase(it);
        } else {
            ++it;
        }
    }
    rebuildFilter();
    Q_EMIT notebooksChanged();
    Q_EMIT unlockedChanged();
    Q_EMIT entriesChanged();
}

QString JournalManager::accountNameFor(qint64 accountId) const
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

const JournalNotebook *JournalManager::notebookById(qint64 id) const
{
    for (const JournalNotebook &nb : m_notebooks) {
        if (nb.id == id) {
            return &nb;
        }
    }
    return nullptr;
}

QVariantList JournalManager::notebooks() const
{
    QHash<qint64, int> counts;
    for (const JournalEntry &e : m_entries) {
        ++counts[e.notebookId];
    }
    QVariantList out;
    for (const JournalNotebook &nb : m_notebooks) {
        out.append(QVariantMap{
            {QStringLiteral("id"), nb.id},
            {QStringLiteral("name"), nb.name},
            {QStringLiteral("color"), nb.color},
            {QStringLiteral("count"), counts.value(nb.id)},
            {QStringLiteral("syncAccountId"), nb.syncAccountId},
            {QStringLiteral("syncAccountName"), accountNameFor(nb.syncAccountId)},
            {QStringLiteral("synced"), nb.syncAccountId > 0},
        });
    }
    return out;
}

void JournalManager::setSelectedNotebookId(qlonglong id)
{
    if (m_selectedNotebookId != id) {
        m_selectedNotebookId = id;
        Q_EMIT selectedNotebookChanged();
        rebuildFilter();
    }
}

qlonglong JournalManager::createNotebook(const QString &name, const QString &color, qlonglong syncAccountId)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        return 0;
    }
    JournalNotebook nb;
    nb.name = clean;
    nb.color = color.isEmpty() ? QStringLiteral("#3daee9") : color;
    nb.syncAccountId = syncAccountId > 0 ? syncAccountId : 0;
    if (!m_db->insertJournalNotebook(nb)) {
        m_lastError = m_db->lastError();
        return 0;
    }
    reload();
    return nb.id;
}

bool JournalManager::renameNotebook(qlonglong id, const QString &name, const QString &color, qlonglong syncAccountId)
{
    const QString clean = name.trimmed();
    if (clean.isEmpty()) {
        return false;
    }
    const JournalNotebook *cur = notebookById(id);
    if (!cur) {
        return false;
    }
    JournalNotebook nb = *cur;
    nb.name = clean;
    if (!color.isEmpty()) {
        nb.color = color;
    }
    if (syncAccountId >= 0) {
        nb.syncAccountId = syncAccountId;
    }
    if (!m_db->updateJournalNotebook(nb)) {
        m_lastError = m_db->lastError();
        return false;
    }
    reload();
    return true;
}

bool JournalManager::setNotebookSyncAccount(qlonglong id, qlonglong accountId, bool applyToEntries)
{
    const JournalNotebook *cur = notebookById(id);
    if (!cur) {
        return false;
    }
    JournalNotebook nb = *cur;
    nb.syncAccountId = accountId > 0 ? accountId : 0;
    if (!m_db->updateJournalNotebook(nb)) {
        m_lastError = m_db->lastError();
        return false;
    }
    bool needSync = false;
    if (applyToEntries) {
        for (JournalEntry e : m_entries) {
            if (e.notebookId != id || e.isLocked) {
                continue;
            }
            if (nb.syncAccountId == 0 && e.syncAccountId > 0) {
                if (e.remoteId > 0) {
                    m_db->queueJournalRemoteDelete(e.syncAccountId, e.remoteId);
                    needSync = true;
                }
                e.syncAccountId = 0;
                e.remoteId = 0;
                e.etag.clear();
                e.remoteModified = 0;
                e.syncState = SyncState::Synced;
                m_db->updateJournalEntry(e);
            } else if (nb.syncAccountId > 0 && e.syncAccountId != nb.syncAccountId) {
                if (e.syncAccountId > 0 && e.remoteId > 0) {
                    m_db->queueJournalRemoteDelete(e.syncAccountId, e.remoteId);
                    e.remoteId = 0;
                    e.etag.clear();
                    e.remoteModified = 0;
                }
                e.syncAccountId = nb.syncAccountId;
                e.syncState = e.remoteId > 0 ? SyncState::Modified : SyncState::New;
                m_db->updateJournalEntry(e);
                needSync = true;
            }
        }
    }
    reload();
    if (needSync) {
        Q_EMIT syncNeeded();
    }
    return true;
}

bool JournalManager::deleteNotebook(qlonglong id)
{
    if (!m_db->removeJournalNotebook(id)) {
        m_lastError = m_db->lastError();
        return false;
    }
    if (m_selectedNotebookId == id) {
        m_selectedNotebookId = 0;
        Q_EMIT selectedNotebookChanged();
    }
    reload();
    Q_EMIT syncNeeded();
    return true;
}

QHash<int, QByteArray> JournalManager::roleNames() const
{
    return {
        {EntryIdRole, "entryId"},
        {TitleRole, "title"},
        {EntryDateRole, "entryDate"},
        {EntryDateTextRole, "entryDateText"},
        {TagsRole, "tags"},
        {MoodRole, "mood"},
        {LockedRole, "locked"},
        {UnlockedRole, "unlocked"},
        {ExcerptRole, "excerpt"},
        {ModifiedTextRole, "modifiedText"},
        {NotebookIdRole, "notebookId"},
        {NotebookNameRole, "notebookName"},
        {NotebookColorRole, "notebookColor"},
        {SyncAccountIdRole, "syncAccountId"},
        {SyncAccountNameRole, "syncAccountName"},
        {SyncedRole, "synced"},
        {SyncPendingRole, "syncPending"},
    };
}

int JournalManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_filtered.size();
}

QVariant JournalManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filtered.size()) {
        return {};
    }
    const JournalEntry &e = m_entries.at(m_filtered.at(index.row()));
    const bool unlocked = !e.isLocked || m_unlockedContent.contains(e.id);
    const QLocale locale;
    switch (role) {
    case EntryIdRole:
        return e.id;
    case Qt::DisplayRole:
    case TitleRole:
        return e.title.isEmpty() ? i18nc("untitled journal entry", "Untitled entry") : e.title;
    case EntryDateRole:
        return e.entryDate.toString(Qt::ISODate);
    case EntryDateTextRole:
        return locale.toString(e.entryDate, QStringLiteral("ddd, d MMM yyyy"));
    case TagsRole:
        return e.tags;
    case MoodRole:
        return e.mood;
    case LockedRole:
        return e.isLocked;
    case UnlockedRole:
        return unlocked;
    case ExcerptRole: {
        if (e.isLocked && !unlocked) {
            return i18n("Locked with password");
        }
        const QString &src = e.isLocked ? m_unlockedContent.value(e.id) : e.content;
        QString plain = src.simplified();
        if (plain.size() > 110) {
            plain = plain.left(107) + QStringLiteral("…");
        }
        return plain;
    }
    case ModifiedTextRole:
        return locale.toString(e.modified.toLocalTime(), QLocale::ShortFormat);
    case NotebookIdRole:
        return e.notebookId;
    case NotebookNameRole: {
        const JournalNotebook *nb = notebookById(e.notebookId);
        return nb ? nb->name : (m_notebooks.isEmpty() ? QString() : m_notebooks.first().name);
    }
    case NotebookColorRole: {
        const JournalNotebook *nb = notebookById(e.notebookId);
        return nb ? nb->color : QStringLiteral("#3daee9");
    }
    case SyncAccountIdRole:
        return e.syncAccountId;
    case SyncAccountNameRole:
        return accountNameFor(e.syncAccountId);
    case SyncedRole:
        return e.syncAccountId > 0;
    case SyncPendingRole:
        return e.syncAccountId > 0 && e.syncState != SyncState::Synced;
    }
    return {};
}

void JournalManager::setSearchText(const QString &text)
{
    if (m_search != text) {
        m_search = text;
        Q_EMIT searchTextChanged();
        rebuildFilter();
    }
}

int JournalManager::indexById(qlonglong id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void JournalManager::rebuildFilter()
{
    beginResetModel();
    m_filtered.clear();
    const QString needle = foldText(m_search.trimmed());
    for (int i = 0; i < m_entries.size(); ++i) {
        const JournalEntry &e = m_entries.at(i);
        if (m_selectedNotebookId > 0 && e.notebookId != m_selectedNotebookId) {
            continue;
        }
        if (needle.isEmpty()) {
            m_filtered.append(i);
            continue;
        }
        bool match = foldText(e.title).contains(needle) || foldText(e.entryDate.toString(Qt::ISODate)).contains(needle)
            || foldText(e.mood).contains(needle);
        if (!match) {
            for (const QString &tag : e.tags) {
                if (foldText(tag).contains(needle)) {
                    match = true;
                    break;
                }
            }
        }
        if (!match && (!e.isLocked || m_unlockedContent.contains(e.id))) {
            const QString &src = e.isLocked ? m_unlockedContent.value(e.id) : e.content;
            match = foldText(src).contains(needle);
        }
        if (match) {
            m_filtered.append(i);
        }
    }
    endResetModel();
    Q_EMIT countChanged();
}

qlonglong JournalManager::entryIdAt(int row) const
{
    if (row < 0 || row >= m_filtered.size()) {
        return 0;
    }
    return m_entries.at(m_filtered.at(row)).id;
}

qlonglong JournalManager::createEntry(const QString &title, const QString &entryDateIso, qlonglong notebookId)
{
    ensureDefaultNotebook();
    JournalEntry e;
    const qint64 fallbackNb = m_selectedNotebookId > 0 ? m_selectedNotebookId : m_notebooks.first().id;
    e.notebookId = notebookId > 0 ? notebookId : fallbackNb;
    if (const JournalNotebook *nb = notebookById(e.notebookId)) {
        e.syncAccountId = nb->syncAccountId;
        if (e.syncAccountId > 0) {
            e.syncState = SyncState::New;
        }
    }
    const QDate d = entryDateIso.isEmpty() ? QDate::currentDate() : QDate::fromString(entryDateIso, Qt::ISODate);
    e.entryDate = d.isValid() ? d : QDate::currentDate();
    e.title = title.trimmed().isEmpty() ? i18nc("default journal title, %1 is date", "Journal — %1", QLocale().toString(e.entryDate, QLocale::ShortFormat))
                                        : title.trimmed();
    e.content = QStringLiteral("# %1\n\n").arg(e.title);
    if (!m_db->insertJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return 0;
    }
    m_entries = m_db->journalEntries(false);
    rebuildFilter();
    Q_EMIT notebooksChanged();
    Q_EMIT entriesChanged();
    if (e.syncAccountId > 0) {
        Q_EMIT syncNeeded();
    }
    return e.id;
}

QVariantMap JournalManager::entry(qlonglong id) const
{
    QVariantMap out;
    const int idx = indexById(id);
    out[QStringLiteral("exists")] = idx >= 0;
    if (idx < 0) {
        return out;
    }
    const JournalEntry &e = m_entries.at(idx);
    const bool unlocked = !e.isLocked || m_unlockedContent.contains(e.id);
    const QLocale locale;
    out[QStringLiteral("id")] = e.id;
    out[QStringLiteral("uid")] = e.uid;
    out[QStringLiteral("title")] = e.title;
    out[QStringLiteral("content")] = unlocked ? (e.isLocked ? m_unlockedContent.value(e.id) : e.content) : QString();
    out[QStringLiteral("entryDate")] = e.entryDate.toString(Qt::ISODate);
    out[QStringLiteral("entryDateText")] = locale.toString(e.entryDate, QStringLiteral("ddd, d MMM yyyy"));
    out[QStringLiteral("tags")] = e.tags;
    out[QStringLiteral("mood")] = e.mood;
    out[QStringLiteral("locked")] = e.isLocked;
    out[QStringLiteral("unlocked")] = unlocked;
    out[QStringLiteral("createdText")] = locale.toString(e.created.toLocalTime(), QLocale::ShortFormat);
    out[QStringLiteral("modifiedText")] = locale.toString(e.modified.toLocalTime(), QLocale::ShortFormat);
    out[QStringLiteral("notebookId")] = e.notebookId;
    const JournalNotebook *nb = notebookById(e.notebookId);
    out[QStringLiteral("notebookName")] = nb ? nb->name : QString();
    out[QStringLiteral("notebookColor")] = nb ? nb->color : QStringLiteral("#3daee9");
    out[QStringLiteral("syncAccountId")] = e.syncAccountId;
    out[QStringLiteral("syncAccountName")] = accountNameFor(e.syncAccountId);
    out[QStringLiteral("synced")] = e.syncAccountId > 0;
    out[QStringLiteral("syncPending")] = e.syncAccountId > 0 && e.syncState != SyncState::Synced;
    out[QStringLiteral("remoteId")] = e.remoteId;
    return out;
}

bool JournalManager::updateEntry(qlonglong id, const QVariantMap &changes)
{
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    JournalEntry e = m_entries.at(idx);
    if (e.isLocked && !m_unlockedContent.contains(e.id)) {
        m_lastError = i18n("Unlock the entry before editing.");
        return false;
    }
    if (changes.contains(QStringLiteral("title"))) {
        const QString t = changes.value(QStringLiteral("title")).toString().trimmed();
        if (!t.isEmpty()) {
            e.title = t;
        }
    }
    if (changes.contains(QStringLiteral("entryDate"))) {
        const QDate d = QDate::fromString(changes.value(QStringLiteral("entryDate")).toString(), Qt::ISODate);
        if (d.isValid()) {
            e.entryDate = d;
        }
    }
    if (changes.contains(QStringLiteral("mood"))) {
        e.mood = changes.value(QStringLiteral("mood")).toString().trimmed();
    }
    if (changes.contains(QStringLiteral("notebookId"))) {
        const qint64 nbId = changes.value(QStringLiteral("notebookId")).toLongLong();
        if (nbId > 0) {
            e.notebookId = nbId;
        }
    }
    if (changes.contains(QStringLiteral("tags"))) {
        const QVariantList raw = changes.value(QStringLiteral("tags")).toList();
        QStringList clean;
        for (const QVariant &v : raw) {
            QString tag = v.toString().trimmed();
            while (tag.startsWith(QLatin1Char('#'))) {
                tag.remove(0, 1);
            }
            if (!tag.isEmpty() && !clean.contains(tag, Qt::CaseInsensitive)) {
                clean.append(tag);
            }
        }
        e.tags = clean;
    }
    if (changes.contains(QStringLiteral("content"))) {
        const QString text = changes.value(QStringLiteral("content")).toString();
        if (e.isLocked) {
            QString cipher;
            if (!Crypto::encryptText(text, m_unlockedPasswords.value(e.id), e.kdfSalt, cipher)) {
                m_lastError = i18n("Could not encrypt journal entry.");
                return false;
            }
            e.encryptedContent = cipher;
            e.content.clear();
            m_unlockedContent[e.id] = text;
        } else {
            e.content = text;
        }
    }
    e.modified = QDateTime::currentDateTimeUtc();
    if (e.syncAccountId > 0 && !e.isLocked) {
        e.syncState = e.remoteId > 0 ? SyncState::Modified : SyncState::New;
    }
    if (!m_db->updateJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_entries[idx] = e;
    rebuildFilter();
    Q_EMIT entriesChanged();
    if (e.syncAccountId > 0 && !e.isLocked) {
        Q_EMIT syncNeeded();
    }
    return true;
}

bool JournalManager::renameEntry(qlonglong id, const QString &newTitle)
{
    if (newTitle.trimmed().isEmpty()) {
        return false;
    }
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    JournalEntry e = m_entries.at(idx);
    e.title = newTitle.trimmed();
    e.modified = QDateTime::currentDateTimeUtc();
    if (e.syncAccountId > 0 && !e.isLocked) {
        e.syncState = e.remoteId > 0 ? SyncState::Modified : SyncState::New;
    }
    if (!m_db->updateJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_entries[idx] = e;
    rebuildFilter();
    Q_EMIT entriesChanged();
    if (e.syncAccountId > 0 && !e.isLocked) {
        Q_EMIT syncNeeded();
    }
    return true;
}

bool JournalManager::setEntrySyncAccount(qlonglong id, qlonglong accountId)
{
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    JournalEntry e = m_entries.at(idx);
    if (accountId > 0 && e.isLocked) {
        m_lastError = i18n("Unlock and remove password protection before syncing a note to Nextcloud Notes.");
        Q_EMIT message(m_lastError);
        return false;
    }
    bool needSync = false;
    if (accountId <= 0) {
        if (e.syncAccountId > 0 && e.remoteId > 0) {
            m_db->queueJournalRemoteDelete(e.syncAccountId, e.remoteId);
            needSync = true;
        }
        e.syncAccountId = 0;
        e.remoteId = 0;
        e.etag.clear();
        e.remoteModified = 0;
        e.syncState = SyncState::Synced;
    } else {
        if (e.syncAccountId != accountId) {
            if (e.syncAccountId > 0 && e.remoteId > 0) {
                m_db->queueJournalRemoteDelete(e.syncAccountId, e.remoteId);
                e.remoteId = 0;
                e.etag.clear();
                e.remoteModified = 0;
            }
            e.syncAccountId = accountId;
            e.syncState = e.remoteId > 0 ? SyncState::Modified : SyncState::New;
            needSync = true;
        }
    }
    e.modified = QDateTime::currentDateTimeUtc();
    if (!m_db->updateJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_entries[idx] = e;
    rebuildFilter();
    Q_EMIT notebooksChanged();
    Q_EMIT entriesChanged();
    if (needSync) {
        Q_EMIT syncNeeded();
    }
    return true;
}

qlonglong JournalManager::duplicateEntry(qlonglong id)
{
    const int idx = indexById(id);
    if (idx < 0) {
        return 0;
    }
    const JournalEntry &src = m_entries.at(idx);
    if (src.isLocked && !m_unlockedContent.contains(src.id)) {
        m_lastError = i18n("Unlock the entry before duplicating.");
        return 0;
    }
    JournalEntry copy;
    copy.notebookId = src.notebookId;
    copy.title = i18nc("copy of a journal entry", "%1 (copy)", src.title);
    copy.content = src.isLocked ? m_unlockedContent.value(src.id) : src.content;
    copy.entryDate = src.entryDate;
    copy.tags = src.tags;
    copy.mood = src.mood;
    if (!m_db->insertJournalEntry(copy)) {
        m_lastError = m_db->lastError();
        return 0;
    }
    m_entries = m_db->journalEntries(false);
    rebuildFilter();
    Q_EMIT notebooksChanged();
    Q_EMIT entriesChanged();
    return copy.id;
}

bool JournalManager::deleteEntry(qlonglong id)
{
    const int idx = indexById(id);
    const bool wasSynced = idx >= 0 && m_entries.at(idx).syncAccountId > 0 && m_entries.at(idx).remoteId > 0;
    if (!m_db->removeJournalEntryUser(id)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_unlockedContent.remove(id);
    m_unlockedPasswords.remove(id);
    m_entries = m_db->journalEntries(false);
    rebuildFilter();
    Q_EMIT notebooksChanged();
    Q_EMIT unlockedChanged();
    Q_EMIT entriesChanged();
    if (wasSynced) {
        Q_EMIT syncNeeded();
    }
    return true;
}

bool JournalManager::lockWithPassword(qlonglong id, const QString &password)
{
    if (password.isEmpty()) {
        m_lastError = i18n("Password cannot be empty.");
        return false;
    }
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    JournalEntry e = m_entries.at(idx);
    const QString plain = e.isLocked ? m_unlockedContent.value(e.id) : e.content;
    if (e.isLocked && !m_unlockedContent.contains(e.id)) {
        m_lastError = i18n("Entry is already locked.");
        return false;
    }
    Crypto::SealedData sealed;
    if (!Crypto::seal(plain, password, sealed)) {
        m_lastError = i18n("Encryption failed.");
        return false;
    }
    bool needSync = false;
    if (e.syncAccountId > 0) {
        if (e.remoteId > 0) {
            m_db->queueJournalRemoteDelete(e.syncAccountId, e.remoteId);
            needSync = true;
        }
        e.syncAccountId = 0;
        e.remoteId = 0;
        e.etag.clear();
        e.remoteModified = 0;
        e.syncState = SyncState::Synced;
        Q_EMIT message(i18n("Password-protected notes stay offline on this device and were detached from Nextcloud sync."));
    }
    e.isLocked = true;
    e.kdfSalt = sealed.salt;
    e.passwordHash = sealed.passwordHash;
    e.encryptedContent = sealed.ciphertext;
    e.content.clear();
    e.modified = QDateTime::currentDateTimeUtc();
    if (!m_db->updateJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_entries[idx] = e;
    m_unlockedContent.remove(id);
    m_unlockedPasswords.remove(id);
    rebuildFilter();
    Q_EMIT unlockedChanged();
    Q_EMIT entriesChanged();
    if (needSync) {
        Q_EMIT syncNeeded();
    }
    return true;
}

bool JournalManager::unlockEntry(qlonglong id, const QString &password)
{
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    const JournalEntry &e = m_entries.at(idx);
    if (!e.isLocked) {
        return true;
    }
    if (!Crypto::verifyPassword(password, e.kdfSalt, e.passwordHash)) {
        m_lastError = i18n("Incorrect password.");
        return false;
    }
    QString plain;
    if (!Crypto::decryptText(e.encryptedContent, password, e.kdfSalt, plain)) {
        m_lastError = i18n("Could not decrypt entry.");
        return false;
    }
    m_unlockedContent.insert(id, plain);
    m_unlockedPasswords.insert(id, password);
    rebuildFilter();
    Q_EMIT unlockedChanged();
    Q_EMIT entriesChanged();
    return true;
}

void JournalManager::relockEntry(qlonglong id)
{
    if (m_unlockedContent.remove(id) > 0) {
        m_unlockedPasswords.remove(id);
        rebuildFilter();
        Q_EMIT unlockedChanged();
        Q_EMIT entriesChanged();
    }
}

void JournalManager::lockAll()
{
    if (m_unlockedContent.isEmpty()) {
        return;
    }
    m_unlockedContent.clear();
    m_unlockedPasswords.clear();
    rebuildFilter();
    Q_EMIT unlockedChanged();
    Q_EMIT entriesChanged();
}

bool JournalManager::changePassword(qlonglong id, const QString &currentPassword, const QString &newPassword)
{
    if (newPassword.isEmpty()) {
        m_lastError = i18n("New password cannot be empty.");
        return false;
    }
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    JournalEntry e = m_entries.at(idx);
    if (!e.isLocked) {
        return lockWithPassword(id, newPassword);
    }
    if (!Crypto::verifyPassword(currentPassword, e.kdfSalt, e.passwordHash)) {
        m_lastError = i18n("Current password is incorrect.");
        return false;
    }
    QString plain;
    if (!Crypto::decryptText(e.encryptedContent, currentPassword, e.kdfSalt, plain)) {
        m_lastError = i18n("Could not decrypt entry.");
        return false;
    }
    Crypto::SealedData sealed;
    if (!Crypto::seal(plain, newPassword, sealed)) {
        m_lastError = i18n("Encryption failed.");
        return false;
    }
    e.kdfSalt = sealed.salt;
    e.passwordHash = sealed.passwordHash;
    e.encryptedContent = sealed.ciphertext;
    e.modified = QDateTime::currentDateTimeUtc();
    if (!m_db->updateJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_entries[idx] = e;
    if (m_unlockedContent.contains(id)) {
        m_unlockedPasswords.insert(id, newPassword);
    }
    Q_EMIT entriesChanged();
    return true;
}

bool JournalManager::removePassword(qlonglong id, const QString &currentPassword)
{
    const int idx = indexById(id);
    if (idx < 0) {
        return false;
    }
    JournalEntry e = m_entries.at(idx);
    if (!e.isLocked) {
        return true;
    }
    if (!Crypto::verifyPassword(currentPassword, e.kdfSalt, e.passwordHash)) {
        m_lastError = i18n("Incorrect password.");
        return false;
    }
    QString plain;
    if (!Crypto::decryptText(e.encryptedContent, currentPassword, e.kdfSalt, plain)) {
        m_lastError = i18n("Could not decrypt entry.");
        return false;
    }
    e.isLocked = false;
    e.content = plain;
    e.kdfSalt.clear();
    e.passwordHash.clear();
    e.encryptedContent.clear();
    e.modified = QDateTime::currentDateTimeUtc();
    if (!m_db->updateJournalEntry(e)) {
        m_lastError = m_db->lastError();
        return false;
    }
    m_entries[idx] = e;
    m_unlockedContent.remove(id);
    m_unlockedPasswords.remove(id);
    rebuildFilter();
    Q_EMIT unlockedChanged();
    Q_EMIT entriesChanged();
    return true;
}

bool JournalManager::exportMarkdown(qlonglong id, const QUrl &fileUrl) const
{
    const QVariantMap e = entry(id);
    if (!e.value(QStringLiteral("exists")).toBool()) {
        return false;
    }
    if (e.value(QStringLiteral("locked")).toBool() && !e.value(QStringLiteral("unlocked")).toBool()) {
        m_lastError = i18n("Unlock the entry before exporting.");
        return false;
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = f.errorString();
        return false;
    }
    f.write(e.value(QStringLiteral("content")).toString().toUtf8());
    return true;
}

QVariantList JournalManager::recentSummary(int limit) const
{
    const int n = qBound(1, limit, 12);
    QList<const JournalEntry *> items;
    items.reserve(m_entries.size());
    for (const JournalEntry &e : m_entries) {
        items.append(&e);
    }
    std::sort(items.begin(), items.end(), [](const JournalEntry *a, const JournalEntry *b) {
        if (a->entryDate != b->entryDate) {
            return a->entryDate > b->entryDate;
        }
        return a->modified > b->modified;
    });

    const QLocale locale;
    QVariantList out;
    const int count = qMin(n, int(items.size()));
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
        const JournalEntry *e = items.at(i);
        const bool unlocked = !e->isLocked || m_unlockedContent.contains(e->id);
        const JournalNotebook *nb = notebookById(e->notebookId);
        QString excerpt;
        if (!e->isLocked || unlocked) {
            const QString &src = e->isLocked ? m_unlockedContent.value(e->id) : e->content;
            excerpt = src.simplified();
            if (excerpt.size() > 96) {
                excerpt = excerpt.left(93) + QStringLiteral("…");
            }
        }
        out.append(QVariantMap{
            {QStringLiteral("id"), e->id},
            {QStringLiteral("title"), e->title.isEmpty() ? i18nc("untitled journal entry", "Untitled entry") : e->title},
            {QStringLiteral("entryDate"), e->entryDate.toString(Qt::ISODate)},
            {QStringLiteral("entryDateText"), locale.toString(e->entryDate, QStringLiteral("ddd, d MMM"))},
            {QStringLiteral("mood"), e->mood},
            {QStringLiteral("locked"), e->isLocked},
            {QStringLiteral("unlocked"), unlocked},
            {QStringLiteral("excerpt"), excerpt},
            {QStringLiteral("notebookName"), nb ? nb->name : QString()},
            {QStringLiteral("notebookColor"), nb ? nb->color : QStringLiteral("#3daee9")},
        });
    }
    return out;
}

qlonglong JournalManager::openOrCreateTodayEntry()
{
    const QDate today = QDate::currentDate();
    for (const JournalEntry &e : m_entries) {
        if (e.entryDate == today) {
            return e.id;
        }
    }
    return createEntry();
}
