// SPDX-License-Identifier: MIT
#include "phoneapp.h"

#include "mainwindow.h"
#include "ringer.h"
#include "secrets.h"
#include "settingsdialog.h"

#include "desktop.h"

#include <KLocalizedString>

#include <QApplication>
#include <QLocale>
#include <QMenu>
#include <QTime>
#include <QUrl>
#include <QWindow>

static QString userPart(const QString &uri)
{
    QString s = uri;
    const int lt = s.indexOf(QLatin1Char('<'));
    if (lt >= 0)
        s = s.mid(lt + 1);
    if (s.startsWith(QLatin1String("sip:")))
        s = s.mid(4);
    else if (s.startsWith(QLatin1String("sips:")))
        s = s.mid(5);
    return s.section(QLatin1Char('@'), 0, 0).section(QLatin1Char(';'), 0, 0);
}

PhoneApp::PhoneApp(QObject *parent)
    : QObject(parent)
    , m_engine(new Engine(this))
    , m_phonebook(new Phonebook(this))
    , m_window(new MainWindow)
    , m_tray(new Desktop::Tray(m_window, this))
    , m_ringer(new Ringer(this))
{
    m_tray->setIconName(QStringLiteral("call-start"));
    Desktop::Notification::setTray(m_tray);

    auto *menu = new QMenu;
    menu->addAction(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Show dialer"), this, [this] {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("mail-message")), i18n("Call voicemail"), this, &PhoneApp::callVoicemail);
    menu->addAction(QIcon::fromTheme(QStringLiteral("call-stop")), i18n("Hang up"), m_engine, &Engine::hangup);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Settings…"), this, &PhoneApp::openSettings);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18n("Quit"), this, &PhoneApp::quit);
    m_tray->setMenu(menu);
    connect(m_tray, &Desktop::Tray::activated, this, [this] {
        if (m_missed) {
            m_missed = 0;
            updateTray();
        }
    });

    // window -> engine
    connect(m_window, &MainWindow::dialRequested, m_engine, &Engine::dial);
    connect(m_window, &MainWindow::hangupRequested, m_engine, &Engine::hangup);
    connect(m_window, &MainWindow::acceptRequested, m_engine, &Engine::accept);
    connect(m_window, &MainWindow::muteToggled, m_engine, &Engine::setMuted);
    connect(m_window, &MainWindow::holdToggled, m_engine, &Engine::setHold);
    connect(m_window, &MainWindow::dtmf, m_engine, &Engine::sendDtmf);
    connect(m_window, &MainWindow::settingsRequested, this, &PhoneApp::openSettings);
    connect(m_window, &MainWindow::voicemailRequested, this, &PhoneApp::callVoicemail);
    connect(m_window, &MainWindow::searchRequested, m_phonebook, &Phonebook::search);
    connect(m_phonebook, &Phonebook::searchFinished, m_window, &MainWindow::setSearchResults);
    connect(m_phonebook, &Phonebook::errorOccurred, m_window, &MainWindow::setPhonebookError);
    connect(m_engine, &Engine::codecChanged, m_window, &MainWindow::setCodec);
    connect(m_engine, &Engine::voicemailChanged, this, [this](int nNew, int, bool) {
        const int before = m_voicemails;
        m_voicemails = nNew;
        m_window->setVoicemail(nNew, !Settings::load().voicemail.isEmpty());
        updateTray();
        if (nNew > before) {
            if (m_vmNotification)
                m_vmNotification->close();
            m_vmNotification = new Desktop::Notification(QStringLiteral("newVoicemail"), Desktop::Notification::Kind::Transient, this);
            m_vmNotification->setTitle(i18n("New voice message"));
            m_vmNotification->setText(i18np("You have 1 new voice message.", "You have %1 new voice messages.", nNew));
            m_vmNotification->setIconName(QStringLiteral("mail-unread-new"));
            if (!Settings::load().voicemail.isEmpty()) {
                m_vmNotification->addAction(i18n("Listen"));
                connect(m_vmNotification, &Desktop::Notification::actionTriggered, this, &PhoneApp::callVoicemail);
                connect(m_vmNotification, &Desktop::Notification::defaultTriggered, this, [this] { showWindow(); });
            }
            m_vmNotification->send();
        } else if (nNew == 0 && m_vmNotification) {
            m_vmNotification->close();
        }
    });

    // engine -> ui
    connect(m_engine, &Engine::regStateChanged, this, [this](Engine::RegState s, const QString &info) {
        const Settings st = Settings::load();
        const QString who = QStringLiteral("%1@%2").arg(st.user, st.server);
        switch (s) {
        case Engine::RegState::Registered: m_window->setRegistration(i18n("Registered as %1", who), true); break;
        case Engine::RegState::Registering: m_window->setRegistration(i18n("Registering %1 …", who), false); break;
        case Engine::RegState::Failed: m_window->setRegistration(i18n("Registration failed: %1", info), false); break;
        case Engine::RegState::Offline: m_window->setRegistration(info.isEmpty() ? i18n("Not registered") : info, false); break;
        }
        updateTray();
    });
    connect(m_engine, &Engine::engineError, this, [this](const QString &msg) {
        m_window->setRegistration(msg, false);
        Desktop::Notification::error(i18n("KSipPhone"), msg);
    });
    connect(m_engine, &Engine::incomingCall, this, [this](const QString &uri, const QString &name) {
        m_peerNumber = userPart(uri);
        m_peerName = name;
        m_peerPhoto = QPixmap();
        m_window->setPeerPhoto({});
        const int id = ++m_lookupId;
        m_phonebook->lookupNumber(m_peerNumber, id);
        notifyIncoming();
    });
    connect(m_phonebook, &Phonebook::lookupFinished, this, [this](int id, const QString &name, const QByteArray &photo) {
        if (id != m_lookupId)
            return;
        if (!photo.isEmpty() && m_peerPhoto.loadFromData(photo)) {
            m_window->setPeerPhoto(m_peerPhoto);
            if (m_ring)
                m_ring->setPixmap(m_peerPhoto);
        }
        if (name.isEmpty())
            return;
        m_peerName = name;
        m_window->setCall(m_engine->callState(), peerText());
        if (m_ring)
            m_ring->setText(peerText()); // updates the visible notification (no second sound)
    });
    connect(m_engine, &Engine::callStateChanged, this, [this](Engine::CallState s) {
        if (s == Engine::CallState::Outgoing) {
            m_peerNumber = userPart(m_engine->peer());
            m_peerName.clear();
            m_peerPhoto = QPixmap();
            m_window->setPeerPhoto({});
            const int id = ++m_lookupId;
            m_phonebook->lookupNumber(m_peerNumber, id);
        }
        if (s == Engine::CallState::Established)
            m_wasEstablished = true;
        if (s != Engine::CallState::Incoming) {
            closeRingNotification();
            if (m_window->windowFlags() & Qt::WindowStaysOnTopHint) {
                const bool vis = m_window->isVisible();
                m_window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
                if (vis)
                    m_window->show(); // changing flags hides the window
            }
        }
        if (s == Engine::CallState::Idle) {
            const QString reason = m_engine->closeReason();
            const bool elsewhere = reason.contains(QLatin1String("elsewhere"), Qt::CaseInsensitive)
                || reason.contains(QLatin1String("cause=200"));
            if (m_prevState == Engine::CallState::Incoming && !m_wasEstablished && !elsewhere) {
                ++m_missed;
                notifyMissed();
            }
            m_wasEstablished = false;
            m_window->setPeerPhoto({});
        }
        m_prevState = s;
        m_window->setCall(s, peerText());
        updateTray();
    });
    connect(qApp, &QApplication::aboutToQuit, m_engine, &Engine::stop);
}

PhoneApp::~PhoneApp()
{
    delete m_window;
}

void PhoneApp::start(bool minimized)
{
    applySettings();
    m_engine->start();
    if (!minimized)
        m_window->show();
    updateTray();
}

QString PhoneApp::peerText() const
{
    if (!m_peerName.isEmpty() && m_peerName != m_peerNumber)
        return QStringLiteral("%1 (%2)").arg(m_peerName, m_peerNumber);
    return m_peerNumber.isEmpty() ? i18n("unknown") : m_peerNumber;
}

QString PhoneApp::readPassword(const QString &key)
{
    return Secrets::readPassword(key);
}

void PhoneApp::writePassword(const QString &key, const QString &password)
{
    const QString err = Secrets::writePassword(key, password);
    if (!err.isEmpty())
        Desktop::Notification::error(i18n("KSipPhone"), i18n("The password could not be stored: %1", err));
}

void PhoneApp::applySettings()
{
    const Settings s = Settings::load();
    m_phonebook->configure(s.ldapUri, s.ldapBase);
    m_engine->setAudioDevices(s.speakerDevice, s.micDevice, s.echoCancel);
    m_ringer->configure(s.ringFile, s.ringDevice, s.ringLoop);
    Account a;
    a.user = s.user;
    a.server = s.server;
    a.transport = s.transport;
    a.preferIpv6 = s.preferIpv6;
    a.dtmfMode = s.dtmfMode;
    a.displayName = s.displayName;
    a.codecs = s.codecs.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (a.isValid())
        a.password = readPassword(QStringLiteral("sip:%1@%2").arg(s.user, s.server));
    m_engine->setAccount(a);
    m_window->setVoicemail(m_voicemails, !s.voicemail.isEmpty());
    if (!a.isValid())
        m_window->setRegistration(i18n("No SIP account configured"), false);
}

void PhoneApp::openSettings()
{
    const Settings cur = Settings::load();
    const QString key = QStringLiteral("sip:%1@%2").arg(cur.user, cur.server);
    SettingsDialog dlg(cur, cur.user.isEmpty() ? QString() : readPassword(key), m_window->isVisible() ? m_window : nullptr);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const Settings s = dlg.settings();
    s.save();
    if (dlg.passwordChanged())
        writePassword(QStringLiteral("sip:%1@%2").arg(s.user, s.server), dlg.password());
    applySettings();
}

void PhoneApp::notifyIncoming()
{
    closeRingNotification();
    m_ring = new Desktop::Notification(QStringLiteral("incomingCall"), Desktop::Notification::Kind::Persistent, this);
    m_ring->setTitle(i18n("Incoming call"));
    m_ring->setText(peerText());
    m_ring->setIconName(QStringLiteral("call-start"));
    m_ring->setUrgent(true);
    const int acc = m_ring->addAction(i18n("Accept"));
    const int dec = m_ring->addAction(i18n("Decline"));
    connect(m_ring, &Desktop::Notification::actionTriggered, this, [this, acc, dec](int i) {
        if (i == acc)
            m_engine->accept();
        else if (i == dec)
            m_engine->hangup();
    });
    m_ring->setDefaultAction(i18n("Show dialer"));
    connect(m_ring, &Desktop::Notification::defaultTriggered, this, [this] { showWindow(); });
    m_ring->send();
    m_ringer->start();
    // Without notification buttons the dialer window (with Accept / Decline) comes to the front.
    if (!Desktop::richNotifications())
        showWindow(true);
}

void PhoneApp::notifyMissed()
{
    // Times out normally; Plasma then keeps it in the notification history, where "Call back"
    // keeps working while KSipPhone runs.
    auto *n = new Desktop::Notification(QStringLiteral("missedCall"), Desktop::Notification::Kind::Transient, this);
    n->setTitle(i18n("Missed call"));
    n->setText(i18n("%1 at %2", peerText(), QLocale().toString(QTime::currentTime(), QLocale::ShortFormat)));
    n->setIconName(QStringLiteral("call-missed"));
    if (!m_peerPhoto.isNull())
        n->setPixmap(m_peerPhoto);
    const QString number = m_peerNumber;
    if (!number.isEmpty() && number != QLatin1String("anonymous")) {
        n->addAction(i18n("Call back"));
        connect(n, &Desktop::Notification::actionTriggered, this, [this, number] { callBack(number); });
    }
    n->setDefaultAction(i18n("Show dialer"));
    connect(n, &Desktop::Notification::defaultTriggered, this, [this, number] {
        m_missed = 0;
        updateTray();
        m_window->setNumber(number);
        showWindow();
    });
    n->send();
}

void PhoneApp::callBack(const QString &number)
{
    m_missed = qMax(0, m_missed - 1);
    updateTray();
    m_window->setNumber(number);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    if (m_engine->callState() == Engine::CallState::Idle)
        m_engine->dial(number);
}

void PhoneApp::callVoicemail()
{
    const QString vm = Settings::load().voicemail;
    if (vm.isEmpty()) {
        openSettings();
        return;
    }
    m_window->setNumber(vm);
    m_window->show();
    m_window->raise();
    m_window->activateWindow();
    if (m_engine->callState() == Engine::CallState::Idle)
        m_engine->dial(vm);
}

void PhoneApp::closeRingNotification()
{
    m_ringer->stop();
    if (m_ring) {
        m_ring->close();
        m_ring = nullptr;
    }
}

void PhoneApp::updateTray()
{
    // Icon: always the normal phone icon, only a waiting voice message changes it.
    // Missed calls are left to Plasma's notification history; state details go to the tooltip.
    const auto cs = m_engine->callState();
    const auto rs = m_engine->regState();
    const bool vm = m_voicemails > 0;
    const QString icon = vm ? QStringLiteral("mail-unread-new") : QStringLiteral("call-start");
    QStringList tip;
    if (cs == Engine::CallState::Incoming)
        tip << i18n("Incoming call from %1", peerText());
    else if (cs != Engine::CallState::Idle)
        tip << i18n("In call with %1", peerText());
    tip << (rs == Engine::RegState::Registered ? i18n("Registered") : i18n("Not registered"));
    if (vm)
        tip << i18np("1 new voice message", "%1 new voice messages", m_voicemails);
    m_tray->setIconName(icon);
    m_tray->setAttention(vm);
    m_tray->setToolTip(i18n("KSipPhone"), tip.join(QStringLiteral("<br>")));
}

void PhoneApp::handleArguments(const QStringList &args, bool fromRemote)
{
    for (const QString &a : args.mid(1)) {
        if (a.startsWith(QLatin1String("tel:")) || a.startsWith(QLatin1String("sip:")) || a.startsWith(QLatin1String("sips:")) || a.startsWith(QLatin1String("callto:"))) {
            QString target = QUrl::fromPercentEncoding(a.toUtf8());
            if (target.startsWith(QLatin1String("callto:")))
                target = target.mid(7);
            if (target.startsWith(QLatin1String("tel:")))
                target = target.mid(4).section(QLatin1Char(';'), 0, 0);
            m_window->setNumber(target);
            m_window->show();
            m_window->raise();
            m_window->activateWindow();
            if (m_engine->callState() == Engine::CallState::Idle)
                m_engine->dial(target);
            return;
        }
    }
    if (fromRemote && !args.contains(QLatin1String("--minimized"))) {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    }
}

void PhoneApp::showWindow(bool onTop)
{
    if (onTop) {
        // bring the dialer in front of other windows while it rings (no notification buttons)
        m_window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    }
    m_window->show();
    m_window->showNormal();
    m_window->raise();
    m_window->activateWindow();
}

void PhoneApp::quit()
{
    if (m_engine->callState() != Engine::CallState::Idle)
        m_engine->hangup();
    qApp->quit();
}
