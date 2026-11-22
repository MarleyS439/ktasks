// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "database.h"

#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class Settings;

/**
 * Manages manual and scheduled local SQLite snapshots in a user-chosen
 * directory alongside Nextcloud/CalDAV sync, plus safe validation & restore.
 */
class BackupManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastBackupText READ lastBackupText NOTIFY backupChanged)
    Q_PROPERTY(QVariantList availableBackups READ availableBackups NOTIFY backupChanged)

public:
    BackupManager(KTasks::Database *db, Settings *settings, QObject *parent = nullptr);

    QString lastBackupText() const;
    QVariantList availableBackups() const;

    Q_INVOKABLE QString defaultDirectory() const;
    Q_INVOKABLE bool createBackupNow();
    Q_INVOKABLE bool exportBackupToFile(const QUrl &fileUrl);
    Q_INVOKABLE bool restoreFromFile(const QUrl &fileUrl);
    Q_INVOKABLE void setDirectoryFromUrl(const QUrl &dirUrl);

Q_SIGNALS:
    void backupChanged();
    void restored();
    void message(const QString &text, bool error);

private:
    void checkSchedule();
    void pruneOldBackups(const QString &dirPath);

    KTasks::Database *m_db;
    Settings *m_settings;
    QTimer m_timer;
};
