// SPDX-License-Identifier: GPL-3.0-or-later
#include "caldavclient.h"
#include "logging.h"

#include <QEventLoop>
#include <QPointer>
#include <QTimer>
#include <QXmlStreamReader>

namespace KTasks
{

QString DavResponse::describe() const
{
    if (status == 0) {
        return errorString;
    }
    return QStringLiteral("HTTP %1").arg(status);
}

CalDavClient::CalDavClient(QObject *parent)
    : QObject(parent)
{
    m_nam.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

void CalDavClient::setCredentials(const QString &user, const QString &password)
{
    m_auth = "Basic " + (user + QLatin1Char(':') + password).toUtf8().toBase64();
}

void CalDavClient::setBaseUrl(const QUrl &url)
{
    m_base = url;
}

QUrl CalDavClient::urlFor(const QString &path) const
{
    QUrl u = m_base;
    u.setPath(path, QUrl::DecodedMode);
    u.setQuery(QString());
    u.setFragment(QString());
    return u;
}

QString CalDavClient::normalizeHref(const QString &href)
{
    const QString trimmed = href.trimmed();
    QUrl u = QUrl::fromEncoded(trimmed.toUtf8());
    QString path = u.path(QUrl::FullyDecoded);
    if (path.isEmpty()) {
        path = QUrl::fromPercentEncoding(trimmed.toUtf8());
    }
    if (!path.startsWith(QLatin1Char('/'))) {
        path.prepend(QLatin1Char('/'));
    }
    return path;
}

DavResponse CalDavClient::request(const QByteArray &method, const QUrl &url, const QByteArray &body, const QList<QPair<QByteArray, QByteArray>> &headers)
{
    DavResponse out;
    QUrl target = url;
    for (int hop = 0; hop < 6; ++hop) {
        if (m_cancel && m_cancel->load()) {
            out = DavResponse();
            out.netError = QNetworkReply::OperationCanceledError;
            out.errorString = QStringLiteral("cancelled");
            return out;
        }
        QNetworkRequest req(target);
        req.setTransferTimeout(m_timeoutMs);
        req.setRawHeader("User-Agent", "KTasks/1.0");
        // Never send the credentials to another host than the configured one.
        if (!m_auth.isEmpty() && (hop == 0 || target.host() == url.host())) {
            req.setRawHeader("Authorization", m_auth);
        }
        for (const auto &h : headers) {
            req.setRawHeader(h.first, h.second);
        }
        if (!body.isEmpty() && !req.hasRawHeader("Content-Type")) {
            req.setRawHeader("Content-Type", "application/xml; charset=utf-8");
        }
        QPointer<QNetworkReply> reply = m_nam.sendCustomRequest(req, method, body);
        if (!reply) {
            out = DavResponse();
            out.netError = QNetworkReply::UnknownNetworkError;
            out.errorString = QStringLiteral("failed to create request");
            return out;
        }
        QEventLoop loop;
        QTimer poll;
        poll.setInterval(150);
        connect(&poll, &QTimer::timeout, reply, [this, reply] {
            if (reply && m_cancel && m_cancel->load()) {
                reply->abort();
            }
        });
        poll.start();
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        if (!reply->isFinished()) {
            loop.exec();
        }
        if (!reply) {
            out = DavResponse();
            out.netError = QNetworkReply::OperationCanceledError;
            out.errorString = QStringLiteral("cancelled");
            return out;
        }

        out = DavResponse();
        out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        out.netError = reply->error();
        out.errorString = reply->errorString();
        out.body = reply->readAll();
        out.etag = QString::fromUtf8(reply->rawHeader("ETag"));
        out.url = target;
        const QByteArray location = reply->rawHeader("Location");
        reply->deleteLater();

        qCDebug(lcDav) << method << target.path() << "->" << out.status << out.errorString;
        const bool redirect = out.status == 301 || out.status == 302 || out.status == 307 || out.status == 308;
        if (redirect && !location.isEmpty()) {
            const QUrl next = target.resolved(QUrl::fromEncoded(location));
            if (target.scheme() == QLatin1String("https") && next.scheme() != QLatin1String("https")) {
                out.status = 0;
                out.netError = QNetworkReply::InsecureRedirectError;
                out.errorString = QStringLiteral("refusing redirect from https to http");
                return out;
            }
            target = next;
            continue;
        }
        return out;
    }
    out.status = 0;
    out.netError = QNetworkReply::TooManyRedirectsError;
    out.errorString = QStringLiteral("too many redirects");
    return out;
}

// ---- XML ----

// Flattens the content of a property: plain text for leaves, otherwise the
// space-separated names of its descendants (hrefs and <comp name> by value).
static QString readValue(QXmlStreamReader &x)
{
    QString text;
    QStringList tokens;
    bool hasChildren = false;
    while (!x.atEnd()) {
        const auto tt = x.readNext();
        if (tt == QXmlStreamReader::EndElement) {
            break;
        }
        if (tt == QXmlStreamReader::Characters) {
            text += x.text();
        } else if (tt == QXmlStreamReader::StartElement) {
            hasChildren = true;
            const QString name = x.name().toString();
            if (name == QLatin1String("comp")) {
                tokens << x.attributes().value(QLatin1String("name")).toString();
                x.skipCurrentElement();
            } else if (name == QLatin1String("href")) {
                tokens << x.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            } else {
                tokens << name;
                const QString sub = readValue(x);
                if (!sub.isEmpty()) {
                    tokens << sub;
                }
            }
        }
    }
    return hasChildren ? tokens.join(QLatin1Char(' ')) : text.trimmed();
}

QList<DavItem> CalDavClient::parseMultistatus(const QByteArray &xml, QString *syncToken)
{
    QList<DavItem> out;
    QXmlStreamReader x(xml);
    if (!x.readNextStartElement() || x.name() != QLatin1String("multistatus")) {
        return out;
    }
    while (x.readNextStartElement()) {
        if (x.name() == QLatin1String("sync-token")) {
            const QString token = x.readElementText().trimmed();
            if (syncToken) {
                *syncToken = token;
            }
        } else if (x.name() == QLatin1String("response")) {
            DavItem item;
            while (x.readNextStartElement()) {
                if (x.name() == QLatin1String("href")) {
                    item.href = normalizeHref(x.readElementText());
                } else if (x.name() == QLatin1String("status")) {
                    item.status = x.readElementText().section(QLatin1Char(' '), 1, 1).toInt();
                } else if (x.name() == QLatin1String("propstat")) {
                    QHash<QString, QString> props;
                    int status = 200;
                    while (x.readNextStartElement()) {
                        if (x.name() == QLatin1String("prop")) {
                            while (x.readNextStartElement()) {
                                const QString name = x.name().toString();
                                props.insert(name, readValue(x));
                            }
                        } else if (x.name() == QLatin1String("status")) {
                            status = x.readElementText().section(QLatin1Char(' '), 1, 1).toInt();
                        } else {
                            x.skipCurrentElement();
                        }
                    }
                    if (status >= 200 && status < 300) {
                        item.props.insert(props);
                    }
                } else {
                    x.skipCurrentElement();
                }
            }
            out.append(item);
        } else {
            x.skipCurrentElement();
        }
    }
    return out;
}

static QByteArray xmlEscape(const QString &s)
{
    return s.toHtmlEscaped().toUtf8();
}

static QByteArray hrefXml(const QString &path)
{
    return "<d:href>" + xmlEscape(QString::fromLatin1(QUrl::toPercentEncoding(path, "/"))) + "</d:href>";
}

static bool sameCollection(QString a, QString b)
{
    if (!a.endsWith(QLatin1Char('/'))) {
        a += QLatin1Char('/');
    }
    if (!b.endsWith(QLatin1Char('/'))) {
        b += QLatin1Char('/');
    }
    return a == b;
}

// ---- Discovery ----

CalDavClient::Discovery CalDavClient::discover(const QUrl &serverUrl)
{
    Discovery d;
    static const QByteArray body = R"(<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:current-user-principal/></d:prop></d:propfind>)";

    QList<QUrl> candidates;
    QUrl base = serverUrl;
    if (base.path().isEmpty()) {
        base.setPath(QStringLiteral("/"));
    }
    auto withPath = [&](const QString &p) {
        QUrl u = base;
        u.setPath(p);
        return u;
    };
    QString prefix = base.path();
    if (!prefix.endsWith(QLatin1Char('/'))) {
        prefix += QLatin1Char('/');
    }
    candidates << base;
    if (!prefix.contains(QLatin1String("remote.php"))) {
        candidates << withPath(prefix + QStringLiteral("remote.php/dav/")); // Nextcloud, also in a sub-directory
    }
    candidates << withPath(QStringLiteral("/.well-known/caldav"));

    DavResponse firstFailure;
    for (const QUrl &candidate : std::as_const(candidates)) {
        const DavResponse r = request("PROPFIND", candidate, body, {{"Depth", "0"}});
        if (r.status == 0 || r.status == 401) {
            // Unreachable or wrong credentials: other entry points will not help.
            d.response = r;
            return d;
        }
        if (r.status != 207) {
            if (firstFailure.status == 0) {
                firstFailure = r;
            }
            continue;
        }
        const auto items = parseMultistatus(r.body);
        QString principal;
        for (const DavItem &it : items) {
            principal = it.props.value(QStringLiteral("current-user-principal"));
            if (!principal.isEmpty()) {
                break;
            }
        }
        if (principal.isEmpty() || principal == QLatin1String("unauthenticated")) {
            if (firstFailure.status == 0) {
                firstFailure = r;
                firstFailure.status = 404;
            }
            continue;
        }
        QUrl dav = r.url;
        dav.setUserInfo(QString());
        setBaseUrl(dav);
        d.davUrl = dav.toString();
        d.principalPath = normalizeHref(principal);

        static const QByteArray homeBody =
            R"(<?xml version="1.0"?><d:propfind xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav"><d:prop><c:calendar-home-set/></d:prop></d:propfind>)";
        const DavResponse hr = request("PROPFIND", urlFor(d.principalPath), homeBody, {{"Depth", "0"}});
        if (hr.status != 207) {
            d.response = hr;
            return d;
        }
        for (const DavItem &it : parseMultistatus(hr.body)) {
            const QString home = it.props.value(QStringLiteral("calendar-home-set"));
            if (!home.isEmpty()) {
                d.homePath = normalizeHref(home.section(QLatin1Char(' '), 0, 0));
                break;
            }
        }
        if (d.homePath.isEmpty()) {
            d.response = hr;
            d.response.status = 404;
            return d;
        }
        d.ok = true;
        return d;
    }
    d.response = firstFailure;
    if (d.response.status == 0 && d.response.errorString.isEmpty()) {
        d.response.status = 404;
    }
    return d;
}

bool CalDavClient::listCollections(const QString &homePath, QList<DavCollection> *out, DavResponse *resp)
{
    static const QByteArray body = R"(<?xml version="1.0"?>
<d:propfind xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav" xmlns:cs="http://calendarserver.org/ns/" xmlns:a="http://apple.com/ns/ical/">
<d:prop><d:displayname/><d:resourcetype/><a:calendar-color/><c:supported-calendar-component-set/><cs:getctag/><d:sync-token/><d:current-user-privilege-set/></d:prop>
</d:propfind>)";
    const DavResponse r = request("PROPFIND", urlFor(homePath), body, {{"Depth", "1"}});
    if (resp) {
        *resp = r;
    }
    if (r.status != 207) {
        return false;
    }
    for (const DavItem &it : parseMultistatus(r.body)) {
        if (sameCollection(it.href, homePath)) {
            continue;
        }
        DavCollection c;
        c.href = it.href.endsWith(QLatin1Char('/')) ? it.href : it.href + QLatin1Char('/');
        c.name = it.props.value(QStringLiteral("displayname"));
        const QStringList types = it.props.value(QStringLiteral("resourcetype")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        c.isCalendar = types.contains(QLatin1String("calendar"));
        const QString comps = it.props.value(QStringLiteral("supported-calendar-component-set"));
        // A calendar that does not say what it supports accepts everything.
        const QStringList compList = comps.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        c.supportsTodo = compList.isEmpty() || compList.contains(QLatin1String("VTODO"));
        c.supportsEvent = compList.isEmpty() || compList.contains(QLatin1String("VEVENT"));
        c.color = it.props.value(QStringLiteral("calendar-color")).left(7);
        c.ctag = it.props.value(QStringLiteral("getctag"));
        c.syncToken = it.props.value(QStringLiteral("sync-token"));
        const QStringList privs = it.props.value(QStringLiteral("current-user-privilege-set")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        c.readOnly = !privs.isEmpty() && !privs.contains(QLatin1String("write")) && !privs.contains(QLatin1String("write-content"))
            && !privs.contains(QLatin1String("all"));
        if (c.name.isEmpty()) {
            c.name = c.href.section(QLatin1Char('/'), -2, -2);
        }
        out->append(c);
    }
    return true;
}

CalDavClient::SyncReport CalDavClient::syncCollection(const QString &collectionPath, const QString &token)
{
    SyncReport rep;
    const QByteArray body = R"(<?xml version="1.0"?><d:sync-collection xmlns:d="DAV:"><d:sync-token>)" + xmlEscape(token)
        + R"(</d:sync-token><d:sync-level>1</d:sync-level><d:prop><d:getetag/></d:prop></d:sync-collection>)";
    rep.response = request("REPORT", urlFor(collectionPath), body, {{"Depth", "1"}});
    if (rep.response.status != 207) {
        // 403/409 with valid-sync-token, or servers that answer 400/410/507 for stale tokens
        rep.tokenInvalid = rep.response.status == 403 || rep.response.status == 409 || rep.response.status == 410 || rep.response.status == 507
            || rep.response.status == 400 || rep.response.body.contains("valid-sync-token");
        return rep;
    }
    for (const DavItem &it : parseMultistatus(rep.response.body, &rep.newToken)) {
        if (sameCollection(it.href, collectionPath)) {
            continue;
        }
        if (it.status == 404 || (it.props.isEmpty() && it.status != 200)) {
            rep.deleted << it.href;
        } else {
            rep.changed << it;
        }
    }
    rep.ok = true;
    return rep;
}

bool CalDavClient::listTodos(const QString &collectionPath, QList<DavItem> *out, DavResponse *resp)
{
    static const QByteArray body = R"(<?xml version="1.0"?>
<c:calendar-query xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav"><d:prop><d:getetag/></d:prop>
<c:filter><c:comp-filter name="VCALENDAR"><c:comp-filter name="VTODO"/></c:comp-filter></c:filter></c:calendar-query>)";
    const DavResponse r = request("REPORT", urlFor(collectionPath), body, {{"Depth", "1"}});
    if (resp) {
        *resp = r;
    }
    if (r.status != 207) {
        return false;
    }
    for (const DavItem &it : parseMultistatus(r.body)) {
        if (!sameCollection(it.href, collectionPath) && !it.etag().isEmpty()) {
            out->append(it);
        }
    }
    return true;
}

bool CalDavClient::listEvents(const QString &collectionPath, QList<DavItem> *out, DavResponse *resp)
{
    static const QByteArray body = R"(<?xml version="1.0"?>
<c:calendar-query xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav"><d:prop><d:getetag/></d:prop>
<c:filter><c:comp-filter name="VCALENDAR"><c:comp-filter name="VEVENT"/></c:comp-filter></c:filter></c:calendar-query>)";
    const DavResponse r = request("REPORT", urlFor(collectionPath), body, {{"Depth", "1"}});
    if (resp) {
        *resp = r;
    }
    if (r.status != 207) {
        return false;
    }
    for (const DavItem &it : parseMultistatus(r.body)) {
        if (!sameCollection(it.href, collectionPath) && !it.etag().isEmpty()) {
            out->append(it);
        }
    }
    return true;
}

bool CalDavClient::multiget(const QString &collectionPath, const QStringList &hrefs, QList<DavItem> *out, DavResponse *resp)
{
    QByteArray body = R"(<?xml version="1.0"?><c:calendar-multiget xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav"><d:prop><d:getetag/><c:calendar-data/></d:prop>)";
    for (const QString &h : hrefs) {
        body += hrefXml(h);
    }
    body += "</c:calendar-multiget>";
    const DavResponse r = request("REPORT", urlFor(collectionPath), body, {{"Depth", "1"}});
    if (resp) {
        *resp = r;
    }
    if (r.status != 207) {
        return false;
    }
    out->append(parseMultistatus(r.body));
    return true;
}

DavResponse CalDavClient::get(const QString &path)
{
    return request("GET", urlFor(path));
}

DavResponse CalDavClient::put(const QString &path, const QString &ics, const QString &ifMatch, bool ifNoneMatch)
{
    QList<QPair<QByteArray, QByteArray>> headers{{"Content-Type", "text/calendar; charset=utf-8"}};
    if (ifNoneMatch) {
        headers.append(qMakePair(QByteArray("If-None-Match"), QByteArray("*")));
    } else if (!ifMatch.isEmpty()) {
        headers.append(qMakePair(QByteArray("If-Match"), ifMatch.toUtf8()));
    }
    return request("PUT", urlFor(path), ics.toUtf8(), headers);
}

DavResponse CalDavClient::remove(const QString &path, const QString &ifMatch)
{
    QList<QPair<QByteArray, QByteArray>> headers;
    if (!ifMatch.isEmpty()) {
        headers.append(qMakePair(QByteArray("If-Match"), ifMatch.toUtf8()));
    }
    return request("DELETE", urlFor(path), {}, headers);
}

static QByteArray buildMkCalendarBody(const QString &name, const QString &color, const QByteArray &compName)
{
    QByteArray body = R"(<?xml version="1.0"?><c:mkcalendar xmlns:d="DAV:" xmlns:c="urn:ietf:params:xml:ns:caldav" xmlns:a="http://apple.com/ns/ical/"><d:set><d:prop><d:displayname>)"
        + xmlEscape(name) + "</d:displayname>";
    if (!color.isEmpty()) {
        body += "<a:calendar-color>" + xmlEscape(color) + "</a:calendar-color>";
    }
    body += "<c:supported-calendar-component-set><c:comp name=\"" + compName + "\"/></c:supported-calendar-component-set></d:prop></d:set></c:mkcalendar>";
    return body;
}

DavResponse CalDavClient::mkCalendar(const QString &path, const QString &name, const QString &color)
{
    return request("MKCALENDAR", urlFor(path), buildMkCalendarBody(name, color, "VTODO"));
}

DavResponse CalDavClient::mkEventCalendar(const QString &path, const QString &name, const QString &color)
{
    return request("MKCALENDAR", urlFor(path), buildMkCalendarBody(name, color, "VEVENT"));
}

DavResponse CalDavClient::propPatch(const QString &path, const QString &name, const QString &color)
{
    QByteArray body = R"(<?xml version="1.0"?><d:propertyupdate xmlns:d="DAV:" xmlns:a="http://apple.com/ns/ical/"><d:set><d:prop><d:displayname>)" + xmlEscape(name)
        + "</d:displayname>";
    if (!color.isEmpty()) {
        body += "<a:calendar-color>" + xmlEscape(color) + "</a:calendar-color>";
    }
    body += "</d:prop></d:set></d:propertyupdate>";
    return request("PROPPATCH", urlFor(path), body);
}

QString CalDavClient::etagOf(const QString &path)
{
    static const QByteArray body = R"(<?xml version="1.0"?><d:propfind xmlns:d="DAV:"><d:prop><d:getetag/></d:prop></d:propfind>)";
    const DavResponse r = request("PROPFIND", urlFor(path), body, {{"Depth", "0"}});
    if (r.status != 207) {
        return {};
    }
    for (const DavItem &it : parseMultistatus(r.body)) {
        if (!it.etag().isEmpty()) {
            return it.etag();
        }
    }
    return {};
}

}
