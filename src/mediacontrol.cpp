// SPDX-License-Identifier: MIT
#include "mediacontrol.h"

#include <KLocalizedString>

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(MEDIA, "ksipphone.media")

#ifdef KSIPPHONE_HAVE_DBUS
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>

static const QString kService = QStringLiteral("org.mpris.MediaPlayer2.ksipphone");
static const QString kPath = QStringLiteral("/org/mpris/MediaPlayer2");
static const QString kPlayerIface = QStringLiteral("org.mpris.MediaPlayer2.Player");

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ no)
    Q_PROPERTY(bool CanRaise READ no)
    Q_PROPERTY(bool HasTrackList READ no)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ none)
    Q_PROPERTY(QStringList SupportedMimeTypes READ none)
public:
    explicit MprisRootAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }
    bool no() const { return false; }
    QString identity() const { return QStringLiteral("KSipPhone"); }
    QString desktopEntry() const { return QStringLiteral("io.github.gongsearch.KSipPhone"); }
    QStringList none() const { return {}; }
public Q_SLOTS:
    void Raise() { }
    void Quit() { }
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(bool CanPlay READ yes)
    Q_PROPERTY(bool CanPause READ yes)
    Q_PROPERTY(bool CanControl READ yes)
    Q_PROPERTY(bool CanGoNext READ no)
    Q_PROPERTY(bool CanGoPrevious READ no)
    Q_PROPERTY(bool CanSeek READ no)
    Q_PROPERTY(double Rate READ one)
    Q_PROPERTY(double MinimumRate READ one)
    Q_PROPERTY(double MaximumRate READ one)
    Q_PROPERTY(double Volume READ one)
    Q_PROPERTY(qlonglong Position READ zero)
public:
    MprisPlayerAdaptor(QObject *parent, MediaControl *control)
        : QDBusAbstractAdaptor(parent)
        , m_control(control)
    {
    }
    QString playbackStatus() const { return m_control->m_state == MediaControl::State::Idle ? QStringLiteral("Stopped") : QStringLiteral("Playing"); }
    QVariantMap metadata() const
    {
        const bool ringing = m_control->m_state == MediaControl::State::Ringing;
        return {{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/io/github/gongsearch/KSipPhone/call")))},
                {QStringLiteral("xesam:title"), ringing ? i18n("Incoming call") : i18n("Phone call")},
                {QStringLiteral("xesam:artist"), QStringList{m_control->m_peer}}};
    }
    bool yes() const { return true; }
    bool no() const { return false; }
    double one() const { return 1.0; }
    qlonglong zero() const { return 0; }
public Q_SLOTS:
    void Play() { m_control->playPause(); }
    void PlayPause() { m_control->playPause(); }
    void Pause() { m_control->stop(); }
    void Stop() { m_control->stop(); }
    void Next() { }
    void Previous() { }
    void Seek(qlonglong) { }
    void SetPosition(const QDBusObjectPath &, qlonglong) { }
    void OpenUri(const QString &) { }

private:
    MediaControl *m_control;
};
#endif

MediaControl::MediaControl(QObject *parent)
    : QObject(parent)
{
#ifdef KSIPPHONE_HAVE_DBUS
    auto *obj = new QObject(this);
    new MprisRootAdaptor(obj);
    new MprisPlayerAdaptor(obj, this);
    QDBusConnection::sessionBus().registerObject(kPath, obj, QDBusConnection::ExportAdaptors);
#endif
}

MediaControl::~MediaControl()
{
    registerPlayer(false);
}

bool MediaControl::isAvailable()
{
#ifdef KSIPPHONE_HAVE_DBUS
    return true;
#else
    return false;
#endif
}

void MediaControl::configure(bool pauseMedia, bool mediaKeys)
{
    m_pauseMedia = pauseMedia;
    m_mediaKeys = mediaKeys;
    if (!m_mediaKeys)
        registerPlayer(false);
}

void MediaControl::setCallState(State state, const QString &peer)
{
    const State old = m_state;
    m_state = state;
    m_peer = peer;
    if (old == State::Idle && state != State::Idle) {
        if (m_pauseMedia)
            pauseOthers();
        if (m_mediaKeys)
            registerPlayer(true);
    } else if (state == State::Idle && old != State::Idle) {
        registerPlayer(false);
        resumePaused();
        return;
    }
    emitPlayerChanged();
}

void MediaControl::playPause()
{
    if (m_state == State::Ringing)
        Q_EMIT acceptRequested();
    else if (m_state == State::Active)
        Q_EMIT hangupRequested();
}

void MediaControl::stop()
{
    if (m_state == State::Active)
        Q_EMIT hangupRequested();
}

void MediaControl::registerPlayer(bool on)
{
#ifdef KSIPPHONE_HAVE_DBUS
    if (on == m_registered)
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_registered = on ? bus.registerService(kService) : !bus.unregisterService(kService);
    qCDebug(MEDIA) << "MPRIS player" << (m_registered ? "registered" : "unregistered");
#else
    Q_UNUSED(on)
#endif
}

void MediaControl::emitPlayerChanged()
{
#ifdef KSIPPHONE_HAVE_DBUS
    if (!m_registered)
        return;
    QDBusMessage sig = QDBusMessage::createSignal(kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    const bool ringing = m_state == State::Ringing;
    const QVariantMap meta{{QStringLiteral("mpris:trackid"), QVariant::fromValue(QDBusObjectPath(QStringLiteral("/io/github/gongsearch/KSipPhone/call")))},
                           {QStringLiteral("xesam:title"), ringing ? i18n("Incoming call") : i18n("Phone call")},
                           {QStringLiteral("xesam:artist"), QStringList{m_peer}}};
    sig << kPlayerIface
        << QVariantMap{{QStringLiteral("PlaybackStatus"), m_state == State::Idle ? QStringLiteral("Stopped") : QStringLiteral("Playing")},
                       {QStringLiteral("Metadata"), meta}}
        << QStringList();
    QDBusConnection::sessionBus().send(sig);
#endif
}

#ifdef KSIPPHONE_HAVE_DBUS
static QString playbackStatus(const QString &service)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, kPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    m << kPlayerIface << QStringLiteral("PlaybackStatus");
    const QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 500);
    if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty())
        return {};
    return r.arguments().first().value<QDBusVariant>().variant().toString();
}

static void callPlayer(const QString &service, const QString &method)
{
    QDBusMessage m = QDBusMessage::createMethodCall(service, kPath, kPlayerIface, method);
    QDBusConnection::sessionBus().call(m, QDBus::Block, 500);
}
#endif

void MediaControl::pauseOthers()
{
#ifdef KSIPPHONE_HAVE_DBUS
    m_paused.clear();
    const QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return;
    for (const QString &s : bus->registeredServiceNames().value()) {
        if (!s.startsWith(QLatin1String("org.mpris.MediaPlayer2.")) || s == kService)
            continue;
        if (playbackStatus(s) == QLatin1String("Playing")) {
            callPlayer(s, QStringLiteral("Pause"));
            m_paused << s;
        }
    }
    qCDebug(MEDIA) << "paused" << m_paused;
#endif
}

void MediaControl::resumePaused()
{
#ifdef KSIPPHONE_HAVE_DBUS
    // only players that are still paused (several services may control the same player)
    for (const QString &s : std::as_const(m_paused)) {
        if (playbackStatus(s) == QLatin1String("Paused"))
            callPlayer(s, QStringLiteral("Play"));
    }
    qCDebug(MEDIA) << "resumed" << m_paused;
    m_paused.clear();
#endif
}

#ifdef KSIPPHONE_HAVE_DBUS
#include "mediacontrol.moc"
#endif
