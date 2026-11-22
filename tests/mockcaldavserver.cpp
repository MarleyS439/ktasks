// SPDX-License-Identifier: GPL-3.0-or-later
#include "mockcaldavserver.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpSocket>

static const char *kNs = R"(xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav" xmlns:cs="http://calendarserver.org/ns/" xmlns:a="http://apple.com/ns/ical/")";

static QByteArray esc(const QString &s)
{
    return s.toHtmlEscaped().toUtf8();
}

static QByteArray enc(const QString &path)
{
    return QUrl::toPercentEncoding(path, "/");
}

static QByteArray multistatus(const QByteArray &inner)
{
    return QByteArray("<?xml version=\"1.0\"?><d:multistatus ") + kNs + ">" + inner + "</d:multistatus>";
}

static QByteArray propResponse(const QString &href, const QByteArray &props)
{
    return "<d:response><d:href>" + enc(href) + "</d:href><d:propstat><d:prop>" + props + "</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>";
}

static QString tokenString(int n)
{
    return QStringLiteral("http://mock/sync/%1").arg(n);
}

MockCalDavServer::MockCalDavServer(QObject *parent)
    : QTcpServer(parent)
{
    listen(QHostAddress::LocalHost);
    connect(this, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *s = nextPendingConnection()) {
            if (offline) {
                s->abort();
                s->deleteLater();
                continue;
            }
            connect(s, &QTcpSocket::readyRead, this, [this, s] {
                onReadyRead(s);
            });
            connect(s, &QTcpSocket::disconnected, this, [this, s] {
                m_buffers.remove(s);
                s->deleteLater();
            });
        }
    });
}

QUrl MockCalDavServer::url() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort()));
}

QString MockCalDavServer::addCollection(const QString &slug, const QString &name, const QStringList &components)
{
    const QString href = home + slug + QLatin1Char('/');
    Collection c;
    c.name = name;
    c.components = components;
    collections.insert(href, c);
    return href;
}

void MockCalDavServer::touch(Collection &c, const QString &href, bool deleted)
{
    ++c.token;
    c.log.append({c.token, href, deleted});
}

QString MockCalDavServer::putItem(const QString &collection, const QString &fileName, const QString &ics)
{
    Collection &c = collections[collection];
    const QString href = collection + fileName;
    c.items[href] = Item{ics, QStringLiteral("\"e%1\"").arg(++m_etagCounter)};
    touch(c, href, false);
    return href;
}

void MockCalDavServer::deleteItem(const QString &href)
{
    Collection *c = collectionFor(href);
    if (c && c->items.remove(href)) {
        touch(*c, href, true);
    }
}

qint64 MockCalDavServer::putNote(const QString &title, const QString &content, const QString &category, qint64 id)
{
    const qint64 nid = id > 0 ? id : m_nextNoteId++;
    if (nid >= m_nextNoteId) {
        m_nextNoteId = nid + 1;
    }
    Note n;
    n.id = nid;
    n.title = title;
    n.content = content;
    n.category = category;
    n.etag = QStringLiteral("ne%1").arg(++m_etagCounter);
    n.modified = QDateTime::currentSecsSinceEpoch();
    notes.insert(nid, n);
    ++m_notesVersion;
    return nid;
}

QString MockCalDavServer::icsOf(const QString &href) const
{
    for (const Collection &c : collections) {
        if (c.items.contains(href)) {
            return c.items.value(href).ics;
        }
    }
    return {};
}

int MockCalDavServer::count(const QString &method) const
{
    int n = 0;
    for (const QString &r : requests) {
        n += r.startsWith(method + QLatin1Char(' '));
    }
    return n;
}

MockCalDavServer::Collection *MockCalDavServer::collectionFor(const QString &path, QString *href)
{
    const QString col = path.endsWith(QLatin1Char('/')) ? path : path.left(path.lastIndexOf(QLatin1Char('/')) + 1);
    auto it = collections.find(col);
    if (it == collections.end()) {
        return nullptr;
    }
    if (href) {
        *href = col;
    }
    return &it.value();
}

void MockCalDavServer::onReadyRead(QTcpSocket *socket)
{
    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    Request req;
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2) {
        socket->abort();
        return;
    }
    req.method = QString::fromLatin1(first.at(0));
    req.path = QUrl::fromEncoded(first.at(1)).path(QUrl::FullyDecoded);
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0) {
            req.headers.insert(line.left(colon).toLower(), line.mid(colon + 1).trimmed());
        }
    }
    const int length = req.headers.value("content-length").toInt();
    if (buf.size() < headerEnd + 4 + length) {
        return;
    }
    req.body = buf.mid(headerEnd + 4, length);
    buf.clear();
    handle(socket, req);
}

void MockCalDavServer::respond(QTcpSocket *socket, int status, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &headers)
{
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\n";
    for (const auto &h : headers) {
        out += h.first + ": " + h.second + "\r\n";
    }
    out += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    socket->write(out);
    socket->disconnectFromHost();
}

QByteArray MockCalDavServer::collectionResponse(const QString &href, const Collection &c) const
{
    QByteArray comps;
    for (const QString &comp : c.components) {
        comps += "<c:comp name=\"" + comp.toUtf8() + "\"/>";
    }
    QByteArray props = "<d:displayname>" + esc(c.name) + "</d:displayname><d:resourcetype><d:collection/><c:calendar/></d:resourcetype>"
        + "<c:supported-calendar-component-set>" + comps + "</c:supported-calendar-component-set><cs:getctag>" + esc(tokenString(c.token))
        + "</cs:getctag><d:sync-token>" + esc(tokenString(c.token)) + "</d:sync-token>";
    if (!c.color.isEmpty()) {
        props += "<a:calendar-color>" + esc(c.color) + "</a:calendar-color>";
    }
    props += c.readOnly ? "<d:current-user-privilege-set><d:privilege><d:read/></d:privilege></d:current-user-privilege-set>"
                        : "<d:current-user-privilege-set><d:privilege><d:read/></d:privilege><d:privilege><d:write/></d:privilege></d:current-user-privilege-set>";
    return propResponse(href, props);
}

void MockCalDavServer::handle(QTcpSocket *socket, const Request &req)
{
    requests << req.method + QLatin1Char(' ') + req.path;

    if (failCount > 0 && (failMethod.isEmpty() || failMethod == req.method)) {
        --failCount;
        respond(socket, failStatus);
        return;
    }
    const QByteArray expected = "Basic " + (user + QLatin1Char(':') + password).toUtf8().toBase64();
    if (req.headers.value("authorization") != expected) {
        respond(socket, 401, {}, {{"WWW-Authenticate", "Basic realm=\"mock\""}});
        return;
    }
    const QString body = QString::fromUtf8(req.body);
    const QString &path = req.path;

    if (path == QLatin1String("/.well-known/caldav")) {
        respond(socket, 301, {}, {{"Location", davRoot.toUtf8()}});
        return;
    }
    if (notesEnabled && handleNotesApi(socket, req)) {
        return;
    }
    if (!path.startsWith(davRoot)) {
        respond(socket, 404);
        return;
    }

    if (req.method == QLatin1String("PROPFIND")) {
        if (path == davRoot) {
            respond(socket, 207, multistatus(propResponse(path, "<d:current-user-principal><d:href>" + enc(principal) + "</d:href></d:current-user-principal>")));
        } else if (path == principal) {
            respond(socket, 207, multistatus(propResponse(path, "<c:calendar-home-set><d:href>" + enc(home) + "</d:href></c:calendar-home-set>")));
        } else if (path == home) {
            QByteArray inner = propResponse(home, "<d:resourcetype><d:collection/></d:resourcetype>");
            for (auto it = collections.cbegin(); it != collections.cend(); ++it) {
                inner += collectionResponse(it.key(), it.value());
            }
            // Something that is not a calendar, like Nextcloud's inbox.
            inner += propResponse(home + QStringLiteral("inbox/"), "<d:resourcetype><d:collection/><c:schedule-inbox/></d:resourcetype>");
            respond(socket, 207, multistatus(inner));
        } else if (Collection *c = collectionFor(path); c && c->items.contains(path)) {
            respond(socket, 207, multistatus(propResponse(path, "<d:getetag>" + esc(c->items.value(path).etag) + "</d:getetag>")));
        } else {
            respond(socket, 404);
        }
        return;
    }

    QString colHref;
    Collection *col = collectionFor(path, &colHref);

    if (req.method == QLatin1String("MKCALENDAR")) {
        if (col || !path.startsWith(home)) {
            respond(socket, 405);
            return;
        }
        Collection c;
        static const QRegularExpression nameRe(QStringLiteral("<d:displayname>(.*?)</d:displayname>")),
            colorRe(QStringLiteral("<a:calendar-color>(.*?)</a:calendar-color>")),
            compRe(QStringLiteral("<c:comp\\s+name=\"([A-Z]+)\""));
        c.name = nameRe.match(body).captured(1);
        c.color = colorRe.match(body).captured(1);
        QStringList comps;
        auto it = compRe.globalMatch(body);
        while (it.hasNext()) {
            comps << it.next().captured(1);
        }
        if (!comps.isEmpty()) {
            c.components = comps;
        }
        collections.insert(path, c);
        respond(socket, 201);
        return;
    }
    if (!col) {
        respond(socket, 404);
        return;
    }
    const bool isCollection = path == colHref;

    if (req.method == QLatin1String("PROPPATCH") && isCollection) {
        static const QRegularExpression nameRe(QStringLiteral("<d:displayname>(.*?)</d:displayname>")), colorRe(QStringLiteral("<a:calendar-color>(.*?)</a:calendar-color>"));
        if (col->readOnly) {
            respond(socket, 403);
            return;
        }
        col->name = nameRe.match(body).captured(1);
        const auto cm = colorRe.match(body);
        if (cm.hasMatch()) {
            col->color = cm.captured(1);
        }
        respond(socket, 207, multistatus(propResponse(path, "<d:displayname/>")));
        return;
    }

    if (req.method == QLatin1String("REPORT") && isCollection) {
        QByteArray inner;
        if (body.contains(QLatin1String("sync-collection"))) {
            if (!syncCollectionSupported) {
                respond(socket, 501);
                return;
            }
            static const QRegularExpression tokenRe(QStringLiteral("<d:sync-token>(.*?)</d:sync-token>"));
            const QString token = tokenRe.match(body).captured(1);
            if (invalidateTokens && !token.isEmpty()) {
                invalidateTokens = false;
                respond(socket, 403, "<?xml version=\"1.0\"?><d:error xmlns:d=\"DAV:\"><d:valid-sync-token/></d:error>");
                return;
            }
            if (token.isEmpty()) {
                for (auto it = col->items.cbegin(); it != col->items.cend(); ++it) {
                    inner += propResponse(it.key(), "<d:getetag>" + esc(it.value().etag) + "</d:getetag>");
                }
            } else {
                const int since = token.section(QLatin1Char('/'), -1).toInt();
                QMap<QString, bool> latest;
                for (const Change &ch : std::as_const(col->log)) {
                    if (ch.token > since) {
                        latest[ch.href] = ch.deleted;
                    }
                }
                for (auto it = latest.cbegin(); it != latest.cend(); ++it) {
                    if (it.value() || !col->items.contains(it.key())) {
                        inner += "<d:response><d:href>" + enc(it.key()) + "</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>";
                    } else {
                        inner += propResponse(it.key(), "<d:getetag>" + esc(col->items.value(it.key()).etag) + "</d:getetag>");
                    }
                }
            }
            inner += "<d:sync-token>" + esc(tokenString(col->token)) + "</d:sync-token>";
        } else if (body.contains(QLatin1String("calendar-multiget"))) {
            static const QRegularExpression hrefRe(QStringLiteral("<d:href>(.*?)</d:href>"));
            auto it = hrefRe.globalMatch(body);
            while (it.hasNext()) {
                const QString href = QUrl::fromPercentEncoding(it.next().captured(1).toUtf8());
                if (col->items.contains(href)) {
                    const Item &item = col->items[href];
                    inner += propResponse(href, "<d:getetag>" + esc(item.etag) + "</d:getetag><c:calendar-data>" + esc(item.ics) + "</c:calendar-data>");
                } else {
                    inner += "<d:response><d:href>" + enc(href) + "</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>";
                }
            }
        } else if (body.contains(QLatin1String("calendar-query"))) {
            const QByteArray targetComp = body.contains(QLatin1String("name=\"VEVENT\""))
                ? QByteArrayLiteral("BEGIN:VEVENT")
                : QByteArrayLiteral("BEGIN:VTODO");
            for (auto it = col->items.cbegin(); it != col->items.cend(); ++it) {
                if (it.value().ics.contains(targetComp)) {
                    inner += propResponse(it.key(), "<d:getetag>" + esc(it.value().etag) + "</d:getetag>");
                }
            }
        } else {
            respond(socket, 400);
            return;
        }
        respond(socket, 207, multistatus(inner));
        return;
    }

    if (req.method == QLatin1String("DELETE")) {
        if (isCollection) {
            if (col->readOnly) {
                respond(socket, 403);
                return;
            }
            collections.remove(colHref);
            respond(socket, 204);
            return;
        }
        if (!col->items.contains(path)) {
            respond(socket, 404);
            return;
        }
        const QByteArray ifMatch = req.headers.value("if-match");
        if (!ifMatch.isEmpty() && QString::fromUtf8(ifMatch) != col->items.value(path).etag) {
            respond(socket, 412);
            return;
        }
        col->items.remove(path);
        touch(*col, path, true);
        respond(socket, 204);
        return;
    }

    if (req.method == QLatin1String("GET") && !isCollection) {
        if (!col->items.contains(path)) {
            respond(socket, 404);
            return;
        }
        respond(socket, 200, col->items.value(path).ics.toUtf8(), {{"ETag", col->items.value(path).etag.toUtf8()}});
        return;
    }

    if (req.method == QLatin1String("PUT") && !isCollection) {
        if (col->readOnly) {
            respond(socket, 403);
            return;
        }
        const bool exists = col->items.contains(path);
        const QByteArray ifMatch = req.headers.value("if-match");
        if (req.headers.value("if-none-match") == "*" && exists) {
            respond(socket, 412);
            return;
        }
        if (!ifMatch.isEmpty() && (!exists || QString::fromUtf8(ifMatch) != col->items.value(path).etag)) {
            respond(socket, 412);
            return;
        }
        if (!body.contains(QLatin1String("BEGIN:VCALENDAR"))) {
            respond(socket, 415);
            return;
        }
        const QString etag = QStringLiteral("\"e%1\"").arg(++m_etagCounter);
        col->items[path] = Item{body, etag};
        touch(*col, path, false);
        QList<QPair<QByteArray, QByteArray>> headers;
        if (sendEtagOnPut) {
            headers.append(qMakePair(QByteArray("ETag"), etag.toUtf8()));
        }
        respond(socket, exists ? 204 : 201, {}, headers);
        return;
    }

    respond(socket, 405);
}

static QJsonObject noteToJson(const MockCalDavServer::Note &n)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), n.id);
    o.insert(QStringLiteral("title"), n.title);
    o.insert(QStringLiteral("content"), n.content);
    o.insert(QStringLiteral("category"), n.category);
    o.insert(QStringLiteral("etag"), n.etag);
    o.insert(QStringLiteral("modified"), n.modified);
    o.insert(QStringLiteral("favorite"), false);
    o.insert(QStringLiteral("readonly"), false);
    return o;
}

bool MockCalDavServer::handleNotesApi(QTcpSocket *socket, const Request &req)
{
    const QString prefix = QStringLiteral("/index.php/apps/notes/api/v1/notes");
    if (!req.path.startsWith(prefix)) {
        return false;
    }
    const QString rest = req.path.mid(prefix.size());
    if (rest.isEmpty() || rest == QLatin1String("/")) {
        if (req.method == QLatin1String("GET")) {
            const QString listEtag = QStringLiteral("\"nlist%1\"").arg(m_notesVersion);
            if (QString::fromUtf8(req.headers.value("if-none-match")) == listEtag) {
                respond(socket, 304);
                return true;
            }
            QJsonArray arr;
            for (const Note &n : std::as_const(notes)) {
                arr.append(noteToJson(n));
            }
            respond(socket, 200, QJsonDocument(arr).toJson(QJsonDocument::Compact), {{"Content-Type", "application/json"}, {"ETag", listEtag.toUtf8()}});
            return true;
        }
        if (req.method == QLatin1String("POST")) {
            const QJsonObject in = QJsonDocument::fromJson(req.body).object();
            const qint64 nid = putNote(in.value("title").toString(), in.value("content").toString(), in.value("category").toString());
            const Note &n = notes[nid];
            respond(socket, 200, QJsonDocument(noteToJson(n)).toJson(QJsonDocument::Compact), {{"Content-Type", "application/json"}, {"ETag", QStringLiteral("\"%1\"").arg(n.etag).toUtf8()}});
            return true;
        }
        respond(socket, 405);
        return true;
    }
    bool ok = false;
    const qint64 nid = rest.mid(1).toLongLong(&ok);
    if (!ok || nid <= 0) {
        respond(socket, 404);
        return true;
    }
    if (req.method == QLatin1String("GET")) {
        if (!notes.contains(nid)) {
            respond(socket, 404);
            return true;
        }
        const Note &n = notes[nid];
        respond(socket, 200, QJsonDocument(noteToJson(n)).toJson(QJsonDocument::Compact), {{"Content-Type", "application/json"}, {"ETag", QStringLiteral("\"%1\"").arg(n.etag).toUtf8()}});
        return true;
    }
    if (req.method == QLatin1String("PUT")) {
        if (!notes.contains(nid)) {
            respond(socket, 404);
            return true;
        }
        Note &n = notes[nid];
        const QString ifMatch = QString::fromUtf8(req.headers.value("if-match")).trimmed();
        const QString expectedQuoted = QStringLiteral("\"%1\"").arg(n.etag);
        if (!ifMatch.isEmpty() && ifMatch != expectedQuoted && ifMatch != n.etag) {
            respond(socket, 412);
            return true;
        }
        const QJsonObject in = QJsonDocument::fromJson(req.body).object();
        n.title = in.value("title").toString();
        n.content = in.value("content").toString();
        n.category = in.value("category").toString();
        n.etag = QStringLiteral("ne%1").arg(++m_etagCounter);
        n.modified = QDateTime::currentSecsSinceEpoch();
        ++m_notesVersion;
        respond(socket, 200, QJsonDocument(noteToJson(n)).toJson(QJsonDocument::Compact), {{"Content-Type", "application/json"}, {"ETag", QStringLiteral("\"%1\"").arg(n.etag).toUtf8()}});
        return true;
    }
    if (req.method == QLatin1String("DELETE")) {
        if (!notes.remove(nid)) {
            respond(socket, 404);
            return true;
        }
        ++m_notesVersion;
        respond(socket, 200, "{}");
        return true;
    }
    respond(socket, 405);
    return true;
}
