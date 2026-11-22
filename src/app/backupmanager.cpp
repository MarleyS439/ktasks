// SPDX-License-Identifier: GPL-3.0-or-later
#include "backupmanager.h"
#include "settings.h"

#include <KLocalizedString>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>

using namespace KTasks;

BackupManager::BackupManager(Database *db, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_settings(settings)
{
    connect(m_settings, &Settings::autoBackupEnabledChanged, this, &BackupManager::checkSchedule);
    connect(m_settings, &Settings::autoBackupIntervalHoursChanged, this, &BackupManager::checkSchedule);
    connect(m_settings, &Settings::backupDirectoryChanged, this, &BackupManager::backupChanged);

    m_timer.setInterval(30 * 60 * 1000); // check every 30 minutes
    connect(&m_timer, &QTimer::timeout, this, &BackupManager::checkSchedule);
    if (m_settings->autoBackupEnabled()) {
        m_timer.start();
        QTimer::singleShot(10000, this, &BackupManager::checkSchedule);
    }
}

QString BackupManager::defaultDirectory() const
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return (docs.isEmpty() ? QDir::homePath() : docs) + QStringLiteral("/KTasks Backups");
}

QString BackupManager::lastBackupText() const
{
    const QString iso = m_settings->lastBackupAt();
    if (iso.isEmpty()) {
        return i18n("No local backup created yet");
    }
    const QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid()) {
        return i18n("No local backup created yet");
    }
    return i18nc("%1 is a date and time", "Last backup: %1", QLocale().toString(dt.toLocalTime(), QLocale::ShortFormat));
}

QVariantList BackupManager::availableBackups() const
{
    QVariantList out;
    const QString dirPath = m_settings->backupDirectory().isEmpty() ? defaultDirectory() : m_settings->backupDirectory();
    QDir dir(dirPath);
    if (!dir.exists()) {
        return out;
    }
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("ktasks-backup-*.db")}, QDir::Files, QDir::Name | QDir::Reversed);
    const QLocale locale;
    for (int i = 0; i < qMin<int>(15, files.size()); ++i) {
        const QFileInfo &fi = files.at(i);
        out.append(QVariantMap{
            {QStringLiteral("name"), fi.fileName()},
            {QStringLiteral("path"), fi.absoluteFilePath()},
            {QStringLiteral("url"), QUrl::fromLocalFile(fi.absoluteFilePath())},
            {QStringLiteral("sizeText"), locale.formattedDataSize(fi.size())},
            {QStringLiteral("dateText"), locale.toString(fi.lastModified(), QLocale::ShortFormat)},
        });
    }
    return out;
}

void BackupManager::setDirectoryFromUrl(const QUrl &dirUrl)
{
    const QString path = dirUrl.isLocalFile() ? dirUrl.toLocalFile() : dirUrl.toString();
    if (!path.isEmpty()) {
        m_settings->setBackupDirectory(path);
        Q_EMIT backupChanged();
    }
}

bool BackupManager::createBackupNow()
{
    QString dirPath = m_settings->backupDirectory().trimmed();
    if (dirPath.isEmpty()) {
        dirPath = defaultDirectory();
        m_settings->setBackupDirectory(dirPath);
    }
    QDir().mkpath(dirPath);
    const QString filename = QStringLiteral("ktasks-backup-%1.db").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
    const QString fullPath = QDir(dirPath).filePath(filename);

    QString err;
    if (!m_db->backupTo(fullPath, &err)) {
        Q_EMIT message(i18n("Backup failed: %1", err), true);
        return false;
    }
    m_settings->setLastBackupAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    pruneOldBackups(dirPath);
    Q_EMIT backupChanged();
    Q_EMIT message(i18n("Backup saved to %1", fullPath), false);
    return true;
}

bool BackupManager::exportBackupToFile(const QUrl &fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        return false;
    }
    QString err;
    if (!m_db->backupTo(path, &err)) {
        Q_EMIT message(i18n("Backup failed: %1", err), true);
        return false;
    }
    m_settings->setLastBackupAt(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    Q_EMIT backupChanged();
    Q_EMIT message(i18n("Backup saved to %1", path), false);
    return true;
}

bool BackupManager::restoreFromFile(const QUrl &fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (path.isEmpty()) {
        return false;
    }
    QString err;
    if (!m_db->restoreFrom(path, &err)) {
        Q_EMIT message(i18n("Could not restore backup: %1", err), true);
        return false;
    }
    Q_EMIT restored();
    Q_EMIT backupChanged();
    Q_EMIT message(i18n("Backup restored successfully."), false);
    return true;
}

void BackupManager::checkSchedule()
{
    if (!m_settings->autoBackupEnabled()) {
        m_timer.stop();
        return;
    }
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    const int hours = qMax(1, m_settings->autoBackupIntervalHours());
    const QDateTime last = QDateTime::fromString(m_settings->lastBackupAt(), Qt::ISODate);
    if (last.isValid() && last.secsTo(QDateTime::currentDateTimeUtc()) < qint64(hours) * 3600) {
        return;
    }
    createBackupNow();
}

void BackupManager::pruneOldBackups(const QString &dirPath)
{
    const int keep = qMax(1, m_settings->backupRetentionCount());
    QDir dir(dirPath);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("ktasks-backup-*.db")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keep; i < files.size(); ++i) {
        QFile::remove(files.at(i).absoluteFilePath());
    }
}
