// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "task.h"

#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class Settings;

/**
 * Lightweight Discord Rich Presence client talking directly to the local
 * Discord IPC UNIX socket ($XDG_RUNTIME_DIR/discord-ipc-*).
 * Privacy-first: task titles are omitted unless explicitly enabled.
 */
class DiscordPresence : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

public:
    struct ActivityState {
        bool hasSession = false;
        bool running = false;
        KTasks::FocusMode mode = KTasks::FocusMode::Pomodoro;
        int cycleNumber = 1;
        int elapsedSeconds = 0;
        int remainingSeconds = 0;
        QString taskTitle;
    };

    explicit DiscordPresence(Settings *settings, QObject *parent = nullptr);
    ~DiscordPresence() override;

    bool connected() const
    {
        return m_ready;
    }
    QString statusText() const
    {
        return m_statusText;
    }

    void updateFocusState(const ActivityState &state);
    Q_INVOKABLE void testConnection();

    /** Wire protocol helpers exposed for unit tests. */
    static QByteArray encodeFrame(quint32 opCode, const QByteArray &payload);
    static bool decodeFrameHeader(const QByteArray &bytes, quint32 &opCode, quint32 &payloadLength);
    static QJsonObject buildSetActivityPayload(qint64 pid,
                                               const ActivityState &state,
                                               bool showTaskTitle,
                                               const QString &largeImageKey,
                                               qint64 nowEpochSecs,
                                               const QString &nonce);
    static QJsonObject buildClearActivityPayload(qint64 pid, const QString &nonce);
    static bool isValidClientId(const QString &clientId);
    static QStringList candidateSocketPaths();

Q_SIGNALS:
    void statusChanged();

private:
    void onSettingsChanged();
    void onSocketConnected();
    void onSocketReadyRead();
    void onSocketDisconnected();
    void onSocketError();
    void connectNextSocket();
    void sendHandshake();
    void sendCurrentActivity();
    void sendClearActivity();
    void sendJsonFrame(quint32 opCode, const QJsonObject &obj);
    void disconnectSocket(bool resetStatus = false);
    void setStatus(const QString &text);
    void handleInboundFrame(quint32 opCode, const QByteArray &payload);

    Settings *m_settings;
    QLocalSocket *m_socket = nullptr;
    QTimer m_reconnectTimer;
    QTimer m_testClearTimer;
    QByteArray m_readBuffer;
    QStringList m_pendingSockets;
    ActivityState m_state;
    QString m_connectedClientId;
    QString m_statusText;
    quint64 m_nonceSeq = 0;
    bool m_handshakeSent = false;
    bool m_ready = false;
    bool m_testPending = false;
};
