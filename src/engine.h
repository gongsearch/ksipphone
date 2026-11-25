// SPDX-License-Identifier: GPL-3.0-or-later
// Baresip engine: runs baresip as a child process and talks to it via its ctrl_dbus module.
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

class QDBusInterface;

struct Account {
    QString user;
    QString server;
    QString transport = QStringLiteral("udp");
    QString displayName;
    QString password;
    bool preferIpv6 = false;
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
    // PipeWire node names ("" = default); restarts the engine if they change while idle
    void setAudioDevices(const QString &player, const QString &source);

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
    void engineError(const QString &message);
    void ready();

private Q_SLOTS:
    void onEvent(const QString &cls, const QString &type, const QString &param);

private:
    QString invoke(const QString &command);
    bool writeConfig(QString *dir);
    void connectBus();
    void setRegState(RegState s, const QString &info = {});
    void setCallState(CallState s);
    void queryCodec(int attempt = 0);
    QString targetUri(const QString &target) const;

    QProcess m_proc;
    QDBusInterface *m_iface = nullptr;
    QString m_busName;
    Account m_account;
    bool m_uaCreated = false;
    RegState m_regState = RegState::Offline;
    CallState m_callState = CallState::Idle;
    QString m_peer, m_peerName, m_lastError, m_closeReason;
    int m_retries = 0;
    QString m_player, m_source;
};
