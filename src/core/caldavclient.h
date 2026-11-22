// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QUrl>
#include <atomic>

namespace KTasks
{

struct DavResponse {
    int status = 0; // 0: the request never got an HTTP answer
    QNetworkReply::NetworkError netError = QNetworkReply::NoError;
    QString errorString;
    QByteArray body;
    QString etag;
    QUrl url; // after redirects

    bool ok() const
    {
        return status >= 200 && status < 300;
    }
    QString describe() const;
};

struct DavItem {
    QString href; // decoded absolute path
    int status = 200;
    QHash<QString, QString> props;

    QString etag() const
    {
        return props.value(QStringLiteral("getetag"));
    }
};

struct DavCollection {
    QString href;
    QString name;
    QString color;
    QString ctag;
    QString syncToken;
    bool isCalendar = false;
    bool supportsTodo = false;
    bool supportsEvent = false;
    bool readOnly = false;
};

/**
 * Minimal blocking CalDAV client. Every call spins a local event loop, so
 * it is meant to run on a worker thread (or in tests).
 */
class CalDavClient : public QObject
{
    Q_OBJECT
public:
    explicit CalDavClient(QObject *parent = nullptr);

    void setCredentials(const QString &user, const QString &password);
    void setBaseUrl(const QUrl &url);
    QUrl baseUrl() const
    {
        return m_base;
    }
    void setCancelFlag(const std::atomic_bool *flag)
    {
        m_cancel = flag;
    }
    void setTimeoutMs(int ms)
    {
        m_timeoutMs = ms;
    }

    /** Absolute URL for a decoded path on the server. */
    QUrl urlFor(const QString &path) const;
    /** Decoded absolute path from whatever a server puts into <href>. */
    static QString normalizeHref(const QString &href);

    DavResponse request(const QByteArray &method, const QUrl &url, const QByteArray &body = {}, const QList<QPair<QByteArray, QByteArray>> &headers = {});

    struct Discovery {
        bool ok = false;
        DavResponse response; // the failing response when !ok
        QString davUrl;
        QString principalPath;
        QString homePath;
    };
    /** Finds the principal and the calendar home, trying the usual entry points. */
    Discovery discover(const QUrl &serverUrl);

    bool listCollections(const QString &homePath, QList<DavCollection> *out, DavResponse *resp);

    struct SyncReport {
        bool ok = false;
        bool tokenInvalid = false;
        QString newToken;
        QList<DavItem> changed;
        QStringList deleted;
        DavResponse response;
    };
    SyncReport syncCollection(const QString &collectionPath, const QString &token);
    /** href + etag of every VTODO in the collection. */
    bool listTodos(const QString &collectionPath, QList<DavItem> *out, DavResponse *resp);
    /** href + etag of every VEVENT in the collection. */
    bool listEvents(const QString &collectionPath, QList<DavItem> *out, DavResponse *resp);
    /** Fetches calendar-data for the given hrefs. */
    bool multiget(const QString &collectionPath, const QStringList &hrefs, QList<DavItem> *out, DavResponse *resp);

    DavResponse get(const QString &path);
    DavResponse put(const QString &path, const QString &ics, const QString &ifMatch, bool ifNoneMatch);
    DavResponse remove(const QString &path, const QString &ifMatch);
    DavResponse mkCalendar(const QString &path, const QString &name, const QString &color);
    DavResponse mkEventCalendar(const QString &path, const QString &name, const QString &color);
    DavResponse propPatch(const QString &path, const QString &name, const QString &color);
    QString etagOf(const QString &path);

    static QList<DavItem> parseMultistatus(const QByteArray &xml, QString *syncToken = nullptr);

private:
    QNetworkAccessManager m_nam;
    QUrl m_base;
    QByteArray m_auth;
    const std::atomic_bool *m_cancel = nullptr;
    int m_timeoutMs = 30000;
};

}
