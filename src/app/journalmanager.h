// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"

#include <QAbstractListModel>
#include <QHash>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

/**
 * Manages Markdown journal entries organized into Notebooks, with optional
 * per-entry AES-256-GCM password locking. Unlocked plaintext is kept only in
 * memory for the active session and never written unencrypted to disk while locked.
 */
class JournalManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool hasUnlockedProtected READ hasUnlockedProtected NOTIFY unlockedChanged)
    Q_PROPERTY(QVariantList notebooks READ notebooks NOTIFY notebooksChanged)
    Q_PROPERTY(qlonglong selectedNotebookId READ selectedNotebookId WRITE setSelectedNotebookId NOTIFY selectedNotebookChanged)

public:
    enum Roles {
        EntryIdRole = Qt::UserRole + 1,
        TitleRole,
        EntryDateRole,
        EntryDateTextRole,
        TagsRole,
        MoodRole,
        LockedRole,
        UnlockedRole,
        ExcerptRole,
        ModifiedTextRole,
        NotebookIdRole,
        NotebookNameRole,
        NotebookColorRole,
        SyncAccountIdRole,
        SyncAccountNameRole,
        SyncedRole,
        SyncPendingRole,
    };

    explicit JournalManager(KTasks::Database *db, QObject *parent = nullptr);

    void reload();

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString searchText() const
    {
        return m_search;
    }
    void setSearchText(const QString &text);
    int count() const
    {
        return m_filtered.size();
    }
    bool hasUnlockedProtected() const
    {
        return !m_unlockedPasswords.isEmpty();
    }
    QVariantList notebooks() const;
    qlonglong selectedNotebookId() const
    {
        return m_selectedNotebookId;
    }
    void setSelectedNotebookId(qlonglong id);

    // Notebooks
    Q_INVOKABLE qlonglong createNotebook(const QString &name, const QString &color = QStringLiteral("#3daee9"), qlonglong syncAccountId = 0);
    Q_INVOKABLE bool renameNotebook(qlonglong id, const QString &name, const QString &color = QString(), qlonglong syncAccountId = -1);
    Q_INVOKABLE bool setNotebookSyncAccount(qlonglong id, qlonglong accountId, bool applyToEntries = true);
    Q_INVOKABLE bool deleteNotebook(qlonglong id);

    Q_INVOKABLE qlonglong entryIdAt(int row) const;
    Q_INVOKABLE qlonglong createEntry(const QString &title = QString(), const QString &entryDateIso = QString(), qlonglong notebookId = 0);
    Q_INVOKABLE QVariantMap entry(qlonglong id) const;
    Q_INVOKABLE bool updateEntry(qlonglong id, const QVariantMap &changes);
    Q_INVOKABLE bool renameEntry(qlonglong id, const QString &newTitle);
    Q_INVOKABLE bool setEntrySyncAccount(qlonglong id, qlonglong accountId);
    Q_INVOKABLE qlonglong duplicateEntry(qlonglong id);
    Q_INVOKABLE bool deleteEntry(qlonglong id);

    // Password protection
    Q_INVOKABLE bool lockWithPassword(qlonglong id, const QString &password);
    Q_INVOKABLE bool unlockEntry(qlonglong id, const QString &password);
    Q_INVOKABLE void relockEntry(qlonglong id);
    Q_INVOKABLE void lockAll();
    Q_INVOKABLE bool changePassword(qlonglong id, const QString &currentPassword, const QString &newPassword);
    Q_INVOKABLE bool removePassword(qlonglong id, const QString &currentPassword);

    // Export
    Q_INVOKABLE bool exportMarkdown(qlonglong id, const QUrl &fileUrl) const;
    Q_INVOKABLE QVariantList recentSummary(int limit = 3) const;
    Q_INVOKABLE qlonglong openOrCreateTodayEntry();
    Q_INVOKABLE QString lastError() const
    {
        return m_lastError;
    }

Q_SIGNALS:
    void searchTextChanged();
    void countChanged();
    void unlockedChanged();
    void entriesChanged();
    void notebooksChanged();
    void selectedNotebookChanged();
    void syncNeeded();
    void message(const QString &text);

private:
    void ensureDefaultNotebook();
    void rebuildFilter();
    int indexById(qlonglong id) const;
    const KTasks::JournalNotebook *notebookById(qint64 id) const;
    QString accountNameFor(qint64 accountId) const;

    KTasks::Database *m_db;
    QList<KTasks::JournalNotebook> m_notebooks;
    QList<KTasks::Account> m_accounts;
    QList<KTasks::JournalEntry> m_entries;
    QList<int> m_filtered;
    QHash<qint64, QString> m_unlockedContent;
    QHash<qint64, QString> m_unlockedPasswords;
    QString m_search;
    qlonglong m_selectedNotebookId = 0; // 0 = all notebooks
    mutable QString m_lastError;
};
