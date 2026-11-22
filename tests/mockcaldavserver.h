// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QMap>
#include <QTcpServer>
#include <QUrl>

/**
 * A small in-process CalDAV server, close enough to Nextcloud/sabre-dav for
 * the sync engine: discovery, calendar listing, sync-collection,
 * calendar-query, multiget, conditional PUT/DELETE, MKCALENDAR, PROPPATCH.
 * Faults can be injected to exercise the error paths.
 */
class MockCalDavServer : public QTcpServer
{
    Q_OBJECT
public:
    struct Item {
        QString ics;
        QString etag;
    };
    struct Note {
        qint64 id = 0;
        QString title;
        QString content;
        QString category;
        QString etag;
        qint64 modified = 0;
    };
    struct Change {
        int token;
        QString href;
        bool deleted;
    };
    struct Collection {
        QString name;
        QString color;
        QStringList components{QStringLiteral("VTODO")};
        QMap<QString, Item> items; // by href
        int token = 1;
        QList<Change> log;
        bool readOnly = false;
    };

    static inline const QString davRoot = QStringLiteral("/remote.php/dav/");
    static inline const QString principal = QStringLiteral("/remote.php/dav/principals/users/user/");
    static inline const QString home = QStringLiteral("/remote.php/dav/calendars/user/");

    explicit MockCalDavServer(QObject *parent = nullptr);

    QUrl url() const;
    QString addCollection(const QString &slug, const QString &name, const QStringList &components = {QStringLiteral("VTODO")});
    /** Server-side change, as if made by another client. Returns the href. */
    QString putItem(const QString &collection, const QString &fileName, const QString &ics);
    void deleteItem(const QString &href);
    qint64 putNote(const QString &title, const QString &content, const QString &category = QString(), qint64 id = 0);
    QString icsOf(const QString &href) const;
    int count(const QString &method) const;
    void clearLog()
    {
        requests.clear();
    }
    /** The next @p times requests with @p method (empty: any) answer @p status. */
    void failNext(int status, int times = 1, const QString &method = QString())
    {
        failStatus = status;
        failCount = times;
        failMethod = method;
    }

    QMap<QString, Collection> collections; // by href
    QMap<qint64, Note> notes;
    QString user = QStringLiteral("user");
    QString password = QStringLiteral("secret");
    bool offline = false;
    bool notesEnabled = false;
    bool syncCollectionSupported = true;
    bool invalidateTokens = false;
    bool sendEtagOnPut = true;
    QStringList requests; // "METHOD /path"

private:
    struct Request {
        QString method;
        QString path;
        QHash<QByteArray, QByteArray> headers;
        QByteArray body;
    };
    bool handleNotesApi(QTcpSocket *socket, const Request &req);
    void onReadyRead(class QTcpSocket *socket);
    void handle(QTcpSocket *socket, const Request &req);
    void respond(QTcpSocket *socket, int status, const QByteArray &body = {}, const QList<QPair<QByteArray, QByteArray>> &headers = {});
    QByteArray collectionResponse(const QString &href, const Collection &c) const;
    Collection *collectionFor(const QString &path, QString *href = nullptr);
    void touch(Collection &c, const QString &href, bool deleted);

    QHash<QTcpSocket *, QByteArray> m_buffers;
    int m_etagCounter = 0;
    int m_nextNoteId = 1;
    int m_notesVersion = 1;
    int failStatus = 0;
    int failCount = 0;
    QString failMethod;
};
