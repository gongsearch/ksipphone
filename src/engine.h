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
    bool keepAlive = true; // re-register often enough that firewalls/NAT keep the way in open
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
    enum class Presence { Unknown, Online, Busy, Offline };
    Q_ENUM(Presence)

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
    void setMuted(bool muted); // baresip's own mute of the call's audio
    bool isMuted() const { return m_muted; }
    void setHold(bool hold);
    void sendDtmf(const QString &digit);

    // call transfer: blind (REFER), or attended: consult the target first, then connect or go back
    void transfer(const QString &target);
    void startConsult(const QString &target);
    void completeConsult();
    void abortConsult();
    CallState consultState() const { return m_consultState; }
    // second call: a consultation (attended transfer) or an accepted waiting call
    enum class SecondCall { Consult, Waiting };
    enum class WaitingEnd { Gone, Accepted, BecameMainCall };
    Q_ENUM(WaitingEnd)
    SecondCall secondCall() const { return m_secondKind; }

    // call waiting: a second incoming call during a call
    void acceptWaiting(); // holds the current call
    void rejectWaiting();
    void swapCalls();     // with an accepted waiting call: switch which call is active

    // presence of these extensions (SUBSCRIBE via baresip's presence module)
    void setWatchList(const QStringList &extensions);

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
    void consultStateChanged(Engine::CallState state, const QString &peerUri); // Idle = no consultation call
    void transferFailed(const QString &reason);
    void waitingCall(const QString &peerUri, const QString &peerName); // second incoming call
    void waitingEnded(Engine::WaitingEnd how);
    void callsSwapped();                                              // main and second call changed roles
    void presenceChanged(const QHash<QString, Engine::Presence> &states); // extension -> state
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
    void restartWhenIdle();
    void pollPresence();
    void setConsultState(CallState s);

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
    bool m_muted = false;
    // calls by baresip call id: the main call and an attended transfer's consultation call
    QString m_callId, m_consultId;
    bool m_consultPending = false;
    CallState m_consultState = CallState::Idle;
    QString m_consultPeer;
    SecondCall m_secondKind = SecondCall::Consult;
    QString m_waitingId, m_waitingPeer;
    // presence
    QStringList m_watch;
    QString m_contactsServer; // server the contacts file was written for
    bool m_restartPending = false;
    QHash<QString, Presence> m_presence;
    QTimer *m_presenceTimer = nullptr;
};
