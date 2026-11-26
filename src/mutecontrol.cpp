// SPDX-License-Identifier: MIT
#include "mutecontrol.h"
#include "engine.h"

#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

Q_LOGGING_CATEGORY(MUTE, "ksipphone.mute")

MuteControl::MuteControl(Engine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_queryTimer = new QTimer(this);
    m_queryTimer->setSingleShot(true);
    m_queryTimer->setInterval(80); // pactl reports several events per change
    connect(m_queryTimer, &QTimer::timeout, this, &MuteControl::querySystem);
}

MuteControl::~MuteControl()
{
    watchSystem(false);
}

bool MuteControl::systemMicAvailable()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    static const bool found = !QStandardPaths::findExecutable(QStringLiteral("pactl")).isEmpty();
    return found;
#else
    return false;
#endif
}

QString MuteControl::source() const
{
    return m_source.isEmpty() ? QStringLiteral("@DEFAULT_SOURCE@") : m_source;
}

void MuteControl::configure(bool followSystem, bool pushToTalk, const QString &source)
{
    m_follow = followSystem && systemMicAvailable();
    m_ptt = pushToTalk;
    m_source = source;
    if (m_inCall) {
        watchSystem(m_follow);
        apply();
    }
}

void MuteControl::setInCall(bool inCall)
{
    if (inCall == m_inCall)
        return;
    m_inCall = inCall;
    if (inCall) {
        m_user = false;
        m_pttDown = false;
        m_sysKnown = false;
        m_sysChangedByUs = false;
        m_expect = -1;
        if (m_follow)
            watchSystem(true); // the first query tells the state before the call; apply() follows it
        else
            apply();
        return;
    }
    // call over: give the system microphone back as it was
    if (m_follow && m_sysChangedByUs && m_sysKnown && m_sysMuted != m_sysBefore)
        setSystemMuted(m_sysBefore);
    watchSystem(false);
    m_user = false;
    m_pttDown = false;
    Q_EMIT mutedChanged(false);
}

void MuteControl::setUserMute(bool muted)
{
    m_user = muted;
    apply();
}

void MuteControl::setPushToTalk(bool down)
{
    m_pttDown = down;
    if (m_ptt)
        apply();
}

void MuteControl::apply()
{
    const bool want = effective();
    if (m_inCall) {
        if (m_follow) {
            m_engine->setMuted(false); // the system microphone does the muting
            if (!m_sysKnown || want != m_sysMuted)
                setSystemMuted(want);
        } else {
            m_engine->setMuted(want);
        }
    }
    Q_EMIT mutedChanged(want);
}

void MuteControl::watchSystem(bool on)
{
    if (!on) {
        if (m_subscribe) {
            m_subscribe->kill();
            m_subscribe->waitForFinished(500);
            delete m_subscribe;
            m_subscribe = nullptr;
        }
        return;
    }
    if (m_subscribe)
        return;
    m_subscribe = new QProcess(this);
    connect(m_subscribe, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray out = m_subscribe->readAllStandardOutput();
        if (out.contains("on source") || out.contains("on server"))
            m_queryTimer->start();
    });
    m_subscribe->start(QStringLiteral("pactl"), {QStringLiteral("subscribe")});
    querySystem();
}

void MuteControl::querySystem()
{
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [this, p] {
        const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed(); // "Mute: yes"
        p->deleteLater();
        if (!out.startsWith(QLatin1String("Mute:")))
            return;
        systemMuteChanged(out.endsWith(QLatin1String("yes")));
    });
    p->start(QStringLiteral("pactl"), {QStringLiteral("get-source-mute"), source()});
}

void MuteControl::systemMuteChanged(bool muted)
{
    const bool first = !m_sysKnown;
    m_sysKnown = true;
    m_sysMuted = muted;
    if (first) {
        m_sysBefore = muted;
        m_user = muted; // muted before the call: the button shows it
        if (m_inCall)
            apply();
        return;
    }
    if (m_expect == int(muted)) {
        m_expect = -1; // our own change
        return;
    }
    // changed outside (mic-mute key, Plasma, headset): follow it
    qCDebug(MUTE) << "system microphone" << (muted ? "muted" : "unmuted") << "outside KSipPhone";
    m_user = muted;
    if (m_ptt && !muted)
        m_pttDown = true; // unmuted by hand: treat like a held push-to-talk key
    Q_EMIT mutedChanged(effective());
}

void MuteControl::setSystemMuted(bool muted)
{
    m_expect = int(muted);
    m_sysChangedByUs = true;
    m_sysMuted = muted;
    QProcess::startDetached(QStringLiteral("pactl"), {QStringLiteral("set-source-mute"), source(), muted ? QStringLiteral("1") : QStringLiteral("0")});
}
