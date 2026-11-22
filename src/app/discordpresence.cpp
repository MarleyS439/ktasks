// SPDX-License-Identifier: GPL-3.0-or-later
#include "discordpresence.h"
#include "settings.h"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QtEndian>
#include <cstring>

using namespace KTasks;

namespace
{
constexpr quint32 kOpHandshake = 0;
constexpr quint32 kOpFrame = 1;
constexpr quint32 kOpClose = 2;
constexpr quint32 kOpPing = 3;
constexpr quint32 kOpPong = 4;
} // namespace

DiscordPresence::DiscordPresence(Settings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(5000);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &DiscordPresence::connectNextSocket);

    m_testClearTimer.setSingleShot(true);
    m_testClearTimer.setInterval(6000);
    connect(&m_testClearTimer, &QTimer::timeout, this, [this] {
        m_testPending = false;
        if (!m_state.hasSession) {
            sendClearActivity();
            disconnectSocket(false);
        }
    });

    connect(m_settings, &Settings::discordPresenceEnabledChanged, this, &DiscordPresence::onSettingsChanged);
    connect(m_settings, &Settings::discordShowTaskTitleChanged, this, &DiscordPresence::onSettingsChanged);
    connect(m_settings, &Settings::discordClientIdChanged, this, &DiscordPresence::onSettingsChanged);
    connect(m_settings, &Settings::discordLargeImageChanged, this, &DiscordPresence::onSettingsChanged);

    onSettingsChanged();
}

DiscordPresence::~DiscordPresence()
{
    if (m_ready && !QStandardPaths::isTestModeEnabled()) {
        sendClearActivity();
    }
    disconnectSocket(false);
}

QByteArray DiscordPresence::encodeFrame(quint32 opCode, const QByteArray &payload)
{
    QByteArray out(8 + payload.size(), Qt::Uninitialized);
    const quint32 opLe = qToLittleEndian(opCode);
    const quint32 lenLe = qToLittleEndian(static_cast<quint32>(payload.size()));
    std::memcpy(out.data(), &opLe, 4);
    std::memcpy(out.data() + 4, &lenLe, 4);
    if (!payload.isEmpty()) {
        std::memcpy(out.data() + 8, payload.constData(), payload.size());
    }
    return out;
}

bool DiscordPresence::decodeFrameHeader(const QByteArray &bytes, quint32 &opCode, quint32 &payloadLength)
{
    if (bytes.size() < 8) {
        return false;
    }
    const auto *raw = reinterpret_cast<const unsigned char *>(bytes.constData());
    opCode = qFromLittleEndian<quint32>(raw);
    payloadLength = qFromLittleEndian<quint32>(raw + 4);
    return true;
}

bool DiscordPresence::isValidClientId(const QString &clientId)
{
    const QString s = clientId.trimmed();
    if (s.size() < 15 || s.size() > 22) {
        return false;
    }
    for (const QChar c : s) {
        if (!c.isDigit()) {
            return false;
        }
    }
    return true;
}

QStringList DiscordPresence::candidateSocketPaths()
{
    QStringList dirs;
    const QString xdg = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!xdg.isEmpty()) {
        dirs << xdg << (xdg + QStringLiteral("/app/com.discordapp.Discord")) << (xdg + QStringLiteral("/snap.discord"));
    }
    dirs << QStringLiteral("/tmp");

    QStringList paths;
    for (const QString &dir : std::as_const(dirs)) {
        for (int i = 0; i < 10; ++i) {
            paths << QStringLiteral("%1/discord-ipc-%2").arg(dir).arg(i);
        }
    }
    return paths;
}

QJsonObject DiscordPresence::buildSetActivityPayload(qint64 pid,
                                                     const ActivityState &state,
                                                     bool showTaskTitle,
                                                     const QString &largeImageKey,
                                                     qint64 nowEpochSecs,
                                                     const QString &nonce)
{
    QString details;
    QString statusLine;
    QString modeLabel;
    QString stateLabel;
    if (state.mode == FocusMode::Break) {
        modeLabel = i18nc("discord focus mode", "Pomodoro Break");
        stateLabel = state.running ? i18nc("discord focus state", "Taking a break") : i18nc("discord focus state", "Paused");
    } else if (state.mode == FocusMode::Stopwatch) {
        modeLabel = i18nc("discord focus mode", "Focus Stopwatch");
        stateLabel = state.running ? i18nc("discord focus state", "In focus") : i18nc("discord focus state", "Paused");
    } else {
        modeLabel = i18nc("discord focus mode", "Pomodoro %1/4", state.cycleNumber);
        stateLabel = state.running ? i18nc("discord focus state", "In focus") : i18nc("discord focus state", "Paused");
    }

    if (showTaskTitle && !state.taskTitle.trimmed().isEmpty()) {
        details = state.taskTitle.trimmed();
        statusLine = QStringLiteral("%1 · %2").arg(modeLabel, stateLabel);
    } else {
        details = modeLabel;
        statusLine = stateLabel;
    }

    QJsonObject activity{
        {QStringLiteral("details"), details},
        {QStringLiteral("state"), statusLine},
        {QStringLiteral("instance"), false},
    };

    if (state.running && nowEpochSecs > 0) {
        QJsonObject timestamps;
        if (state.mode == FocusMode::Stopwatch) {
            timestamps.insert(QStringLiteral("start"), qMax<qint64>(0, nowEpochSecs - state.elapsedSeconds));
        } else if (state.remainingSeconds > 0) {
            timestamps.insert(QStringLiteral("end"), nowEpochSecs + state.remainingSeconds);
        }
        if (!timestamps.isEmpty()) {
            activity.insert(QStringLiteral("timestamps"), timestamps);
        }
    }

    const QString img = largeImageKey.trimmed();
    if (!img.isEmpty()) {
        activity.insert(QStringLiteral("assets"),
                        QJsonObject{
                            {QStringLiteral("large_image"), img},
                            {QStringLiteral("large_text"), QStringLiteral("KTasks")},
                        });
    }

    return QJsonObject{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"),
         QJsonObject{
             {QStringLiteral("pid"), pid},
             {QStringLiteral("activity"), activity},
         }},
        {QStringLiteral("nonce"), nonce},
    };
}

QJsonObject DiscordPresence::buildClearActivityPayload(qint64 pid, const QString &nonce)
{
    return QJsonObject{
        {QStringLiteral("cmd"), QStringLiteral("SET_ACTIVITY")},
        {QStringLiteral("args"), QJsonObject{{QStringLiteral("pid"), pid}}},
        {QStringLiteral("nonce"), nonce},
    };
}

void DiscordPresence::updateFocusState(const ActivityState &state)
{
    m_state = state;
    if (m_state.hasSession) {
        m_testPending = false;
        m_testClearTimer.stop();
    }

    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }

    const bool enabled = m_settings->discordPresenceEnabled();
    const QString cid = m_settings->discordClientId().trimmed();
    if (!enabled || !isValidClientId(cid)) {
        if (m_ready) {
            sendClearActivity();
        }
        disconnectSocket(true);
        return;
    }

    if (!m_state.hasSession) {
        if (m_ready) {
            sendClearActivity();
        }
        m_reconnectTimer.stop();
        disconnectSocket(false);
        setStatus(i18nc("discord status", "Ready (updates automatically during focus)"));
        return;
    }

    if (cid != m_connectedClientId) {
        disconnectSocket(false);
    }

    if (m_ready) {
        sendCurrentActivity();
    } else if (!m_socket || m_socket->state() == QLocalSocket::UnconnectedState) {
        connectNextSocket();
    }
}

void DiscordPresence::testConnection()
{
    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }
    const QString cid = m_settings->discordClientId().trimmed();
    if (!isValidClientId(cid)) {
        setStatus(i18nc("discord status", "Enter a valid numeric Discord Application ID first"));
        return;
    }
    m_testPending = !m_state.hasSession;
    if (cid != m_connectedClientId) {
        disconnectSocket(false);
    }
    if (m_ready) {
        if (m_state.hasSession) {
            sendCurrentActivity();
        } else {
            ActivityState demo;
            demo.hasSession = true;
            demo.running = true;
            demo.mode = FocusMode::Pomodoro;
            demo.cycleNumber = 1;
            demo.remainingSeconds = 25 * 60;
            const QString nonce = QString::number(++m_nonceSeq);
            sendJsonFrame(kOpFrame,
                          buildSetActivityPayload(QCoreApplication::applicationPid(),
                                                  demo,
                                                  false,
                                                  m_settings->discordLargeImage(),
                                                  QDateTime::currentSecsSinceEpoch(),
                                                  nonce));
            m_testClearTimer.start();
        }
    } else {
        m_pendingSockets.clear();
        connectNextSocket();
    }
}

void DiscordPresence::onSettingsChanged()
{
    const bool enabled = m_settings->discordPresenceEnabled();
    const QString cid = m_settings->discordClientId().trimmed();
    if (!enabled) {
        m_testPending = false;
        m_testClearTimer.stop();
        m_reconnectTimer.stop();
        if (m_ready && !QStandardPaths::isTestModeEnabled()) {
            sendClearActivity();
        }
        disconnectSocket(false);
        setStatus(i18nc("discord status", "Disabled"));
        return;
    }
    if (!isValidClientId(cid)) {
        m_reconnectTimer.stop();
        disconnectSocket(false);
        setStatus(i18nc("discord status", "Enter your Discord Application ID below"));
        return;
    }
    updateFocusState(m_state);
}

void DiscordPresence::setStatus(const QString &text)
{
    if (m_statusText != text) {
        m_statusText = text;
        Q_EMIT statusChanged();
    }
}

void DiscordPresence::connectNextSocket()
{
    if (QStandardPaths::isTestModeEnabled()) {
        return;
    }
    const QString cid = m_settings->discordClientId().trimmed();
    if (!isValidClientId(cid) || (!m_state.hasSession && !m_testPending)) {
        return;
    }
    if (m_pendingSockets.isEmpty()) {
        for (const QString &p : candidateSocketPaths()) {
            if (QFileInfo::exists(p)) {
                m_pendingSockets << p;
            }
        }
    }
    if (m_pendingSockets.isEmpty()) {
        setStatus(i18nc("discord status", "Discord desktop not detected (open Discord to sync)"));
        if (m_state.hasSession && m_settings->discordPresenceEnabled()) {
            m_reconnectTimer.start();
        }
        return;
    }

    if (!m_socket) {
        m_socket = new QLocalSocket(this);
        connect(m_socket, &QLocalSocket::connected, this, &DiscordPresence::onSocketConnected);
        connect(m_socket, &QLocalSocket::readyRead, this, &DiscordPresence::onSocketReadyRead);
        connect(m_socket, &QLocalSocket::disconnected, this, &DiscordPresence::onSocketDisconnected);
        connect(m_socket, &QLocalSocket::errorOccurred, this, &DiscordPresence::onSocketError);
    } else if (m_socket->state() != QLocalSocket::UnconnectedState) {
        m_socket->abort();
    }

    m_ready = false;
    m_handshakeSent = false;
    m_readBuffer.clear();
    m_connectedClientId = cid;
    setStatus(i18nc("discord status", "Connecting to Discord…"));
    m_socket->connectToServer(m_pendingSockets.takeFirst());
}

void DiscordPresence::onSocketConnected()
{
    m_pendingSockets.clear();
    sendHandshake();
}

void DiscordPresence::sendHandshake()
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState || m_handshakeSent) {
        return;
    }
    m_handshakeSent = true;
    const QJsonObject hello{
        {QStringLiteral("v"), 1},
        {QStringLiteral("client_id"), m_connectedClientId},
    };
    const QByteArray body = QJsonDocument(hello).toJson(QJsonDocument::Compact);
    m_socket->write(encodeFrame(kOpHandshake, body));
    m_socket->flush();
}

void DiscordPresence::onSocketReadyRead()
{
    if (!m_socket) {
        return;
    }
    m_readBuffer.append(m_socket->readAll());
    while (true) {
        quint32 opCode = 0;
        quint32 len = 0;
        if (!decodeFrameHeader(m_readBuffer, opCode, len)) {
            break;
        }
        if (len > 4 * 1024 * 1024) {
            disconnectSocket(false);
            setStatus(i18nc("discord status", "Invalid response from Discord IPC"));
            break;
        }
        if (static_cast<quint32>(m_readBuffer.size()) < 8u + len) {
            break;
        }
        const QByteArray payload = m_readBuffer.mid(8, static_cast<int>(len));
        m_readBuffer.remove(0, 8 + static_cast<int>(len));
        handleInboundFrame(opCode, payload);
    }
}

void DiscordPresence::handleInboundFrame(quint32 opCode, const QByteArray &payload)
{
    if (opCode == kOpPing && m_socket && m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write(encodeFrame(kOpPong, payload));
        m_socket->flush();
        return;
    }
    if (opCode == kOpClose) {
        disconnectSocket(false);
        return;
    }
    if (opCode != kOpFrame) {
        return;
    }
    const QJsonObject obj = QJsonDocument::fromJson(payload).object();
    const QString evt = obj.value(QStringLiteral("evt")).toString();
    if (evt == QLatin1String("READY")) {
        m_ready = true;
        setStatus(i18nc("discord status", "Connected to Discord"));
        Q_EMIT statusChanged();
        if (m_state.hasSession && m_settings->discordPresenceEnabled()) {
            sendCurrentActivity();
        } else if (m_testPending) {
            m_testPending = false;
            ActivityState demo;
            demo.hasSession = true;
            demo.running = true;
            demo.mode = FocusMode::Pomodoro;
            demo.cycleNumber = 1;
            demo.remainingSeconds = 25 * 60;
            const QString nonce = QString::number(++m_nonceSeq);
            sendJsonFrame(kOpFrame,
                          buildSetActivityPayload(QCoreApplication::applicationPid(),
                                                  demo,
                                                  false,
                                                  m_settings->discordLargeImage(),
                                                  QDateTime::currentSecsSinceEpoch(),
                                                  nonce));
            m_testClearTimer.start();
        }
    } else if (evt == QLatin1String("ERROR")) {
        const QString msg = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("message")).toString();
        setStatus(i18nc("discord status", "Discord error: %1", msg.isEmpty() ? i18nc("error", "Unknown error") : msg));
    }
}

void DiscordPresence::sendCurrentActivity()
{
    if (!m_ready || !m_state.hasSession) {
        return;
    }
    const QString nonce = QString::number(++m_nonceSeq);
    sendJsonFrame(kOpFrame,
                  buildSetActivityPayload(QCoreApplication::applicationPid(),
                                          m_state,
                                          m_settings->discordShowTaskTitle(),
                                          m_settings->discordLargeImage(),
                                          QDateTime::currentSecsSinceEpoch(),
                                          nonce));
}

void DiscordPresence::sendClearActivity()
{
    if (!m_ready) {
        return;
    }
    const QString nonce = QString::number(++m_nonceSeq);
    sendJsonFrame(kOpFrame, buildClearActivityPayload(QCoreApplication::applicationPid(), nonce));
}

void DiscordPresence::sendJsonFrame(quint32 opCode, const QJsonObject &obj)
{
    if (!m_socket || m_socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    m_socket->write(encodeFrame(opCode, QJsonDocument(obj).toJson(QJsonDocument::Compact)));
    m_socket->flush();
}

void DiscordPresence::onSocketDisconnected()
{
    const bool wasReady = m_ready;
    m_ready = false;
    m_handshakeSent = false;
    if (wasReady) {
        Q_EMIT statusChanged();
    }
    if (m_state.hasSession && m_settings->discordPresenceEnabled() && !m_testPending) {
        m_reconnectTimer.start();
    }
}

void DiscordPresence::onSocketError()
{
    m_ready = false;
    m_handshakeSent = false;
    if (!m_pendingSockets.isEmpty()) {
        connectNextSocket();
        return;
    }
    const QString err = m_socket ? m_socket->errorString() : QString();
    setStatus(i18nc("discord status", "Could not connect to Discord (%1)", err));
    if (m_state.hasSession && m_settings->discordPresenceEnabled()) {
        m_reconnectTimer.start();
    }
}

void DiscordPresence::disconnectSocket(bool resetStatus)
{
    const bool wasReady = m_ready;
    m_ready = false;
    m_handshakeSent = false;
    m_readBuffer.clear();
    if (m_socket) {
        m_socket->blockSignals(true);
        if (m_socket->state() != QLocalSocket::UnconnectedState) {
            m_socket->disconnectFromServer();
        }
        m_socket->blockSignals(false);
    }
    if (resetStatus) {
        setStatus(m_settings->discordPresenceEnabled() ? i18nc("discord status", "Ready (updates automatically during focus)")
                                                       : i18nc("discord status", "Disabled"));
    } else if (wasReady) {
        Q_EMIT statusChanged();
    }
}
