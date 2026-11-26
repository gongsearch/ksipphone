// SPDX-License-Identifier: MIT
#include "engine.h"
#include "codecs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

Q_LOGGING_CATEGORY(ENGINE, "ksipphone.engine")

#ifndef KSIPPHONE_BARESIP_MODULE_PATH
#define KSIPPHONE_BARESIP_MODULE_PATH "/usr/lib/baresip/modules"
#endif
#ifndef KSIPPHONE_BARESIP_SHARE_PATH
#define KSIPPHONE_BARESIP_SHARE_PATH "/usr/share/baresip"
#endif
#ifndef KSIPPHONE_BARESIP_EXECUTABLE
#define KSIPPHONE_BARESIP_EXECUTABLE "baresip"
#endif

namespace
{
// baresip's audio driver per platform
#if defined(Q_OS_WIN)
const char *const kAudioModule = "wasapi";
#elif defined(Q_OS_MACOS)
const char *const kAudioModule = "audiounit";
#else
const char *const kAudioModule = "pipewire";
#endif

// relative paths are taken relative to the directory of the ksipphone executable
QString resolve(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || !path.contains(QLatin1Char('/')))
        return path;
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(path);
}

QString envOr(const char *name, const QString &fallback)
{
    const QString v = qEnvironmentVariable(name);
    return resolve(v.isEmpty() ? fallback : v);
}

QString baresipExecutable()
{
#if defined(Q_OS_WIN)
    return envOr("KSIPPHONE_BARESIP", QStringLiteral("./baresip.exe"));
#elif defined(Q_OS_MACOS)
    return envOr("KSIPPHONE_BARESIP", QStringLiteral("./baresip")); // Contents/MacOS of the .app
#else
    return envOr("KSIPPHONE_BARESIP", QStringLiteral(KSIPPHONE_BARESIP_EXECUTABLE));
#endif
}

QString moduleExt()
{
#if defined(Q_OS_WIN)
    return QStringLiteral(".dll");
#else
    return QStringLiteral(".so");
#endif
}
} // namespace

Engine::Engine(QObject *parent)
    : QObject(parent)
{
    m_proc.setProcessChannelMode(QProcess::MergedChannels);
#ifdef Q_OS_LINUX
    // never leave an orphaned baresip behind if KSipPhone dies
    m_proc.setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif
    connect(&m_proc, &QProcess::readyRead, this, [this] {
        const auto out = QString::fromUtf8(m_proc.readAll()).trimmed();
        if (out.isEmpty())
            return;
        qCDebug(ENGINE).noquote() << out;
        // remember the codecs baresip registered, e.g. "aucodec: G722/16000/1"
        static const QRegularExpression re(QStringLiteral("aucodec: ([A-Za-z0-9.-]+)/"));
        bool found = false;
        for (auto it = re.globalMatch(out); it.hasNext();) {
            const QString name = it.next().captured(1);
            if (!m_registeredCodecs.contains(name, Qt::CaseInsensitive))
                m_registeredCodecs << name;
            found = true;
        }
        if (found)
            Codecs::setRegistered(m_registeredCodecs);
    });
    connect(&m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        qCWarning(ENGINE) << "baresip exited" << code << st;
        if (m_sock)
            m_sock->abort();
        m_connected = false;
        m_uaCreated = false;
        m_pending.clear();
        setCallState(CallState::Idle);
        setRegState(RegState::Offline, QStringLiteral("engine stopped"));
        if (st == QProcess::CrashExit && m_retries++ < 5)
            QTimer::singleShot(3000, this, &Engine::start);
    });

    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::readyRead, this, &Engine::onReadyRead);
    connect(m_sock, &QTcpSocket::connected, this, [this] {
        m_connected = true;
        m_connectTimer->stop();
        m_retries = 0;
        qCDebug(ENGINE) << "connected to baresip ctrl_tcp on port" << m_port;
        Q_EMIT ready();
        if (m_account.isValid())
            setAccount(m_account);
    });
    connect(m_sock, &QTcpSocket::disconnected, this, [this] { m_connected = false; });

    m_connectTimer = new QTimer(this);
    m_connectTimer->setInterval(250);
    connect(m_connectTimer, &QTimer::timeout, this, &Engine::tryConnect);
}

Engine::~Engine()
{
    stop();
}

bool Engine::writeConfig(QString *dir)
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/baresip");
    if (!QDir().mkpath(base))
        return false;

    // A free loopback port for ctrl_tcp. Note: ctrl_tcp has no authentication, so any local
    // process could talk to it while KSipPhone runs; it only listens on 127.0.0.1.
    {
        QTcpServer probe;
        if (!probe.listen(QHostAddress::LocalHost, 0))
            return false;
        m_port = probe.serverPort();
    }

    // Generated on every start; the account (with password) is never written to disk,
    // it is created at runtime via "uanew".
    QFile cfg(base + QStringLiteral("/config"));
    if (!cfg.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QByteArray mod = kAudioModule;
    const QByteArray player = mod + "," + (m_player.isEmpty() ? QByteArray("default") : m_player.toUtf8());
    const QByteArray source = mod + "," + (m_source.isEmpty() ? QByteArray("default") : m_source.toUtf8());
    const QByteArray ext = moduleExt().toUtf8();
    const QByteArray modulePath = QDir::fromNativeSeparators(Codecs::moduleDir()).toUtf8();
    QByteArray conf =
        "# generated by ksipphone - changes are overwritten\n"
        // no sip_listen: baresip binds all local IPv4 and IPv6 addresses
        "call_local_timeout\t120\n"
        "audio_player\t\t" + player + "\n"
        "audio_source\t\t" + source + "\n"
        "audio_alert\t\t" + player + "\n"
        "audio_path\t\t" + QDir::fromNativeSeparators(base).toUtf8() + "\n"
        "ausrc_srate\t\t48000\n"
        "auplay_srate\t\t48000\n"
        "module_path\t\t" + modulePath + "\n";
    // every codec module this baresip has (the account's audio_codecs then selects and orders them)
    QStringList codecModules;
    for (const CodecInfo &c : Codecs::catalogue()) {
        if (!codecModules.contains(c.module) && Codecs::moduleAvailable(c.module))
            codecModules << c.module;
    }
    for (const QString &m : std::as_const(codecModules))
        conf += "module\t\t\t" + m.toUtf8() + ext + "\n";
    for (const char *m : {kAudioModule, "auconv", "auresamp", "srtp"})
        conf += QByteArray("module\t\t\t") + m + ext + "\n";
    if (m_aec) // baresip's own echo canceller; off when e.g. PipeWire already cancels echo
        conf += "module\t\t\twebrtc_aec" + ext + "\n";
    for (const char *m : {"menu", "mwi", "ctrl_tcp"})
        conf += QByteArray("module_app\t\t") + m + ext + "\n";
    conf += "ctrl_tcp_listen\t\t127.0.0.1:" + QByteArray::number(m_port) + "\n"
            "ringback_disabled\tno\n";
    cfg.write(conf);
    cfg.close();

    // Sounds: baresip's own tones, except the ring tone: ring.wav is silence, KSipPhone rings itself
    // (configurable file, device, once/continuous - see Ringer).
    const QString share = envOr("KSIPPHONE_BARESIP_SHARE", QStringLiteral(KSIPPHONE_BARESIP_SHARE_PATH));
    for (const auto &f : {QStringLiteral("ringback.wav"), QStringLiteral("busy.wav"), QStringLiteral("error.wav"),
                          QStringLiteral("notfound.wav"), QStringLiteral("callwaiting.wav")}) {
        QFile::remove(base + QLatin1Char('/') + f);
        QFile::copy(share + QLatin1Char('/') + f, base + QLatin1Char('/') + f);
    }
    QFile::remove(base + QStringLiteral("/ring.wav"));
    {
        QFile ring(base + QStringLiteral("/ring.wav"));
        if (ring.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const quint32 rate = 8000, samples = 800; // 0.1 s, 16 bit mono silence
            QByteArray wav;
            auto u32 = [&wav](quint32 v) { for (int i = 0; i < 4; ++i) wav.append(char((v >> (8 * i)) & 0xff)); };
            auto u16 = [&wav](quint16 v) { wav.append(char(v & 0xff)); wav.append(char(v >> 8)); };
            wav.append("RIFF"); u32(36 + samples * 2); wav.append("WAVEfmt "); u32(16); u16(1); u16(1);
            u32(rate); u32(rate * 2); u16(2); u16(16); wav.append("data"); u32(samples * 2);
            wav.append(QByteArray(int(samples * 2), '\0'));
            ring.write(wav);
        }
    }
    for (const auto &f : {QStringLiteral("/accounts"), QStringLiteral("/contacts")}) {
        QFile e(base + f);
        if (e.open(QIODevice::WriteOnly | QIODevice::Truncate))
            e.close();
    }
    *dir = base;
    return true;
}

void Engine::start()
{
    if (m_proc.state() != QProcess::NotRunning)
        return;
    QString dir;
    if (!writeConfig(&dir)) {
        Q_EMIT engineError(QStringLiteral("Cannot write baresip configuration"));
        return;
    }
    m_buf.clear();
    m_pending.clear();
    m_registeredCodecs.clear();
    m_proc.start(baresipExecutable(), {QStringLiteral("-f"), QDir::toNativeSeparators(dir)});
    if (!m_proc.waitForStarted(5000)) {
        m_lastError = QStringLiteral("Cannot start baresip (%1): %2").arg(baresipExecutable(), m_proc.errorString());
        Q_EMIT engineError(m_lastError);
        return;
    }
    m_connectTries = 0;
    m_connectTimer->start();
}

void Engine::tryConnect()
{
    if (m_connected || m_proc.state() == QProcess::NotRunning) {
        m_connectTimer->stop();
        return;
    }
    if (++m_connectTries > 40) { // 10 s
        m_connectTimer->stop();
        m_lastError = QStringLiteral("baresip does not answer on its control port (ctrl_tcp module missing?)");
        Q_EMIT engineError(m_lastError);
        return;
    }
    if (m_sock->state() == QAbstractSocket::UnconnectedState)
        m_sock->connectToHost(QHostAddress::LocalHost, m_port);
}

void Engine::setAudioDevices(const QString &player, const QString &source, bool echoCancel)
{
    if (player == m_player && source == m_source && echoCancel == m_aec)
        return;
    m_player = player;
    m_source = source;
    m_aec = echoCancel;
    if (m_proc.state() != QProcess::NotRunning && m_callState == CallState::Idle) {
        stop();
        QTimer::singleShot(500, this, &Engine::start);
    }
}

void Engine::stop()
{
    if (m_proc.state() == QProcess::NotRunning)
        return;
    m_connectTimer->stop();
    if (m_connected) {
        send(QStringLiteral("quit"));
        m_sock->flush();
    }
    if (!m_proc.waitForFinished(3000)) {
        m_proc.terminate();
        if (!m_proc.waitForFinished(2000))
            m_proc.kill();
    }
}

void Engine::send(const QString &commandLine, Reply reply)
{
    if (!m_connected) {
        if (reply)
            reply(false, QStringLiteral("not connected"));
        return;
    }
    const QString cmd = commandLine.section(QLatin1Char(' '), 0, 0);
    const QString params = commandLine.section(QLatin1Char(' '), 1);
    const QString token = QString::number(++m_token);
    if (reply)
        m_pending.insert(token, reply);
    const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("command"), cmd},
                                                      {QStringLiteral("params"), params},
                                                      {QStringLiteral("token"), token}})
                                .toJson(QJsonDocument::Compact);
    m_sock->write(QByteArray::number(json.size()) + ':' + json + ',');
}

void Engine::onReadyRead()
{
    m_buf += m_sock->readAll();
    // netstrings: <length>:<payload>,
    for (;;) {
        const int colon = m_buf.indexOf(':');
        if (colon <= 0)
            break;
        bool ok = false;
        const int len = m_buf.left(colon).toInt(&ok);
        if (!ok || len < 0) { // garbage - resync by dropping the buffer
            qCWarning(ENGINE) << "invalid netstring from baresip";
            m_buf.clear();
            break;
        }
        if (m_buf.size() < colon + 1 + len + 1)
            break; // incomplete
        const QByteArray payload = m_buf.mid(colon + 1, len);
        m_buf.remove(0, colon + 1 + len + 1);
        const QJsonObject msg = QJsonDocument::fromJson(payload).object();
        if (!msg.isEmpty())
            handleMessage(msg);
    }
}

void Engine::handleMessage(const QJsonObject &msg)
{
    if (msg.value(QStringLiteral("response")).toBool()) {
        const QString token = msg.value(QStringLiteral("token")).toString();
        const Reply r = m_pending.take(token);
        if (r)
            r(msg.value(QStringLiteral("ok")).toBool(), msg.value(QStringLiteral("data")).toString());
        else if (!msg.value(QStringLiteral("ok")).toBool())
            qCWarning(ENGINE) << "command failed" << msg.value(QStringLiteral("data")).toString().trimmed();
        return;
    }
    if (msg.value(QStringLiteral("event")).toBool())
        onEvent(msg);
}

void Engine::setAccount(const Account &account)
{
    m_account = account;
    if (!m_connected)
        return;
    if (m_uaCreated) {
        send(QStringLiteral("uadelall"));
        m_uaCreated = false;
    }
    if (!account.isValid()) {
        setRegState(RegState::Offline, QStringLiteral("no account configured"));
        return;
    }
    QString aor = QStringLiteral("<sip:%1@%2;transport=%3>").arg(account.user, account.server, account.transport);
    if (!account.displayName.isEmpty())
        aor.prepend(QStringLiteral("\"%1\" ").arg(QString(account.displayName).remove(QLatin1Char('"'))));
    QString line = aor;
    if (!account.password.isEmpty())
        line += QStringLiteral(";auth_pass=\"%1\"").arg(account.password);
    if (account.preferIpv6) {
        // Route via the server's IPv6 address (if it has one); the AOR keeps the server name.
        QString host = account.server, port;
        if (!host.startsWith(QLatin1Char('[')) && host.count(QLatin1Char(':')) == 1) {
            port = host.section(QLatin1Char(':'), 1);
            host = host.section(QLatin1Char(':'), 0, 0);
        }
        QHostAddress literal(host);
        if (literal.isNull()) {
            const QHostInfo info = QHostInfo::fromName(host);
            for (const QHostAddress &a : info.addresses()) {
                if (a.protocol() == QAbstractSocket::IPv6Protocol && !a.isLinkLocal()) {
                    QString proxy = QStringLiteral("sip:[%1]").arg(a.toString());
                    if (!port.isEmpty())
                        proxy += QLatin1Char(':') + port;
                    line += QStringLiteral(";outbound=\"%1;transport=%2\"").arg(proxy, account.transport);
                    qCDebug(ENGINE) << "using IPv6 outbound proxy" << proxy;
                    break;
                }
            }
        }
    }
    line += QStringLiteral(";regint=300;answermode=manual");
    if (!account.dtmfMode.isEmpty())
        line += QStringLiteral(";dtmfmode=%1").arg(account.dtmfMode);
    if (!account.codecs.isEmpty()) {
        // baresip matches a bare codec name against 8 kHz; wideband codecs need their rate (spec).
        // Codecs whose module is missing are left out; unknown ids are passed through unchanged.
        QStringList list;
        for (const QString &c : account.codecs) {
            const CodecInfo *ci = Codecs::find(c.trimmed());
            if (!ci)
                list << c.trimmed();
            else if (Codecs::available(*ci))
                list << ci->spec;
        }
        line += QStringLiteral(";audio_codecs=%1").arg(list.join(QLatin1Char(',')));
    }
    send(QStringLiteral("uanew ") + line, [this](bool ok, const QString &data) {
        if (!ok) {
            setRegState(RegState::Failed, data.trimmed());
            return;
        }
    });
    m_uaCreated = true;
    setRegState(RegState::Registering);
}

QString Engine::targetUri(const QString &target) const
{
    QString t = target.trimmed();
    if (t.startsWith(QLatin1String("sip:")) || t.startsWith(QLatin1String("sips:")))
        return t;
    if (t.startsWith(QLatin1String("tel:")))
        t = t.mid(4);
    // keep digits, '+', '*', '#'; drop formatting characters
    QString n;
    for (const QChar c : std::as_const(t)) {
        if (c.isDigit() || c == QLatin1Char('+') || c == QLatin1Char('*') || c == QLatin1Char('#'))
            n += c;
    }
    if (n.isEmpty())
        n = t; // probably a SIP user name
    return QStringLiteral("sip:%1@%2").arg(n, m_account.server);
}

void Engine::dial(const QString &target)
{
    if (target.trimmed().isEmpty())
        return;
    m_peer = targetUri(target);
    m_peerName.clear();
    send(QStringLiteral("dial ") + m_peer);
}

void Engine::accept()
{
    send(QStringLiteral("accept"));
}

void Engine::hangup()
{
    send(QStringLiteral("hangup"));
}

void Engine::setMuted(bool muted)
{
    Q_UNUSED(muted) // baresip toggles
    send(QStringLiteral("mute"));
}

void Engine::setHold(bool hold)
{
    send(hold ? QStringLiteral("hold") : QStringLiteral("resume"));
}

void Engine::sendDtmf(const QString &digit)
{
    send(QStringLiteral("sndcode ") + digit);
}

void Engine::queryCodec(int attempt)
{
    if (m_callState != CallState::Established)
        return;
    // "callstat" prints the audio stream incl. the codec; pick the first known codec name.
    send(QStringLiteral("callstat"), [this, attempt](bool, const QString &out) {
        static const QStringList known = {QStringLiteral("G722"), QStringLiteral("opus"), QStringLiteral("PCMA"),
                                          QStringLiteral("PCMU"), QStringLiteral("L16"), QStringLiteral("G726"),
                                          QStringLiteral("GSM"), QStringLiteral("G729"), QStringLiteral("AMR-WB"),
                                          QStringLiteral("AMR")};
        int best = -1;
        QString codec;
        for (const QString &k : known) {
            const int i = out.indexOf(k, 0, Qt::CaseInsensitive);
            if (i >= 0 && (best < 0 || i < best)) {
                best = i;
                codec = k;
            }
        }
        if (codec.isEmpty() && attempt < 5) {
            QTimer::singleShot(1000, this, [this, attempt] { queryCodec(attempt + 1); });
            return;
        }
        qCDebug(ENGINE) << "codec" << codec;
        Q_EMIT codecChanged(codec);
    });
}

void Engine::setRegState(RegState s, const QString &info)
{
    if (s == m_regState && info.isEmpty())
        return;
    m_regState = s;
    Q_EMIT regStateChanged(s, info);
}

void Engine::setCallState(CallState s)
{
    if (s == m_callState)
        return;
    m_callState = s;
    Q_EMIT callStateChanged(s);
}

void Engine::onEvent(const QJsonObject &ev)
{
    const QString t = ev.value(QStringLiteral("type")).toString();
    const QString text = ev.value(QStringLiteral("param")).toString();
    qCDebug(ENGINE) << "event" << ev.value(QStringLiteral("class")).toString() << t << text;

    if (t == QLatin1String("MWI_NOTIFY")) {
        // body like "Messages-Waiting: yes\r\nVoice-Message: 2/5 (0/0)"
        bool waiting = false;
        int nNew = 0, nOld = 0;
        for (const QString &raw : text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts)) {
            const QString l = raw.trimmed();
            if (l.startsWith(QLatin1String("Messages-Waiting"), Qt::CaseInsensitive))
                waiting = l.section(QLatin1Char(':'), 1).trimmed().startsWith(QLatin1String("yes"), Qt::CaseInsensitive);
            else if (l.startsWith(QLatin1String("Voice-Message"), Qt::CaseInsensitive)) {
                const auto m = QRegularExpression(QStringLiteral("(\\d+)\\s*/\\s*(\\d+)")).match(l);
                if (m.hasMatch()) {
                    nNew = m.captured(1).toInt();
                    nOld = m.captured(2).toInt();
                }
            }
        }
        if (waiting && nNew == 0)
            nNew = 1; // "yes" without counts
        Q_EMIT voicemailChanged(nNew, nOld, waiting);
        return;
    }
    if (t == QLatin1String("REGISTERING")) {
        if (m_regState != RegState::Registered)
            setRegState(RegState::Registering);
    } else if (t == QLatin1String("REGISTER_OK")) {
        setRegState(RegState::Registered, text);
    } else if (t == QLatin1String("REGISTER_FAIL")) {
        setRegState(RegState::Failed, text);
    } else if (t == QLatin1String("UNREGISTERING")) {
        setRegState(RegState::Offline);
    } else if (t == QLatin1String("CALL_INCOMING")) {
        m_peer = ev.value(QStringLiteral("peeruri")).toString();
        m_peerName = ev.value(QStringLiteral("peerdisplayname")).toString();
        setCallState(CallState::Incoming);
        Q_EMIT incomingCall(m_peer, m_peerName);
    } else if (t == QLatin1String("CALL_OUTGOING")) {
        setCallState(CallState::Outgoing);
    } else if (t == QLatin1String("CALL_RINGING") || t == QLatin1String("CALL_PROGRESS")) {
        if (m_callState == CallState::Outgoing)
            setCallState(CallState::Ringing);
    } else if (t == QLatin1String("CALL_ESTABLISHED")) {
        setCallState(CallState::Established);
        QTimer::singleShot(300, this, [this] { queryCodec(); });
    } else if (t == QLatin1String("CALL_CLOSED")) {
        m_closeReason = text;
        setCallState(CallState::Idle);
        Q_EMIT codecChanged(QString());
        Q_EMIT callEnded(text);
    }
}
