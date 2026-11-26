// SPDX-License-Identifier: MIT
// Baresip engine: runs baresip as a child process and controls it through its ctrl_tcp module
// (netstring-framed JSON on a loopback port). Works on Linux, Windows and macOS.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <functional>

class QTcpSocket;
class QTimer;

struct Account {
    QString user;
    QString server;
    QString transport = QStringLiteral("udp");
    QString displayName;
    QString password;
    bool preferIpv6 = false;
    QString dtmfMode = QStringLiteral("rtpevent"); // rtpevent (RFC 4733) | info (SIP INFO) | auto
    QStringList codecs = {QStringLiteral("G722"), QStringLiteral("PCMA"), QStringLiteral("PCMU")};
    bool isValid() const { return !user.isEmpty() && !server.isEmpty(); }
};

class Engine : public QObject
{
    Q_OBJECT
public:
    enum class RegState { Offline, Registering, Registered, Failed };
    Q_ENUM(RegState)
    enum class CallState { Idle, Incoming, Outgoing, Ringing, Established };
    Q_ENUM(CallState)

    explicit Engine(QObject *parent = nullptr);
    ~Engine() override;

    void start();
    void stop();
    void setAccount(const Account &account); // (re)creates the user agent
    // Audio device keys as used by baresip's audio module ("" = default, see audioDeviceKey());
    // restarts the engine if they change while idle.
    void setAudioDevices(const QString &player, const QString &source, bool echoCancel = false);

    void dial(const QString &target);
    void accept();
    void hangup();
    void setMuted(bool muted);
    void setHold(bool hold);
    void sendDtmf(const QString &digit);

    RegState regState() const { return m_regState; }
    CallState callState() const { return m_callState; }
    QString peer() const { return m_peer; }
    QString peerName() const { return m_peerName; }
    QString lastError() const { return m_lastError; }
    QString closeReason() const { return m_closeReason; } // of the last CALL_CLOSED

Q_SIGNALS:
    void regStateChanged(Engine::RegState state, const QString &info);
    void callStateChanged(Engine::CallState state);
    void incomingCall(const QString &peerUri, const QString &peerName);
    void callEnded(const QString &reason);
    void codecChanged(const QString &codec); // e.g. "G722", "PCMA"; empty when idle
    void voicemailChanged(int newMessages, int oldMessages, bool waiting);
    void engineError(const QString &message);
    void ready();

private:
    using Reply = std::function<void(bool ok, const QString &data)>;
    void send(const QString &commandLine, Reply reply = {});
    void onReadyRead();
    void handleMessage(const QJsonObject &msg);
    void onEvent(const QJsonObject &ev);
    bool writeConfig(QString *dir);
    void tryConnect();
    void setRegState(RegState s, const QString &info = {});
    void setCallState(CallState s);
    void queryCodec(int attempt = 0);
    QString targetUri(const QString &target) const;

    QProcess m_proc;
    QTcpSocket *m_sock = nullptr;
    QTimer *m_connectTimer = nullptr;
    QByteArray m_buf;
    QHash<QString, Reply> m_pending;
    quint64 m_token = 0;
    quint16 m_port = 0;
    int m_connectTries = 0;
    bool m_connected = false;
    Account m_account;
    bool m_uaCreated = false;
    RegState m_regState = RegState::Offline;
    CallState m_callState = CallState::Idle;
    QString m_peer, m_peerName, m_lastError, m_closeReason;
    int m_retries = 0;
    QString m_player, m_source;
    QStringList m_registeredCodecs;
    bool m_aec = false;
};
