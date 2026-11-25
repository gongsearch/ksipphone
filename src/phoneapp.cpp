// SPDX-License-Identifier: GPL-3.0-or-later
#include "phoneapp.h"

#include "mainwindow.h"
#include "ringer.h"
#include "settingsdialog.h"

#include <KLocalizedString>
#include <KNotification>
#include <KStatusNotifierItem>
#include <KWallet>

#include <QApplication>
#include <QMenu>
#include <QUrl>
#include <QWindow>

using KWallet::Wallet;

static const QString kWalletFolder = QStringLiteral("KSipPhone");

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
    , m_tray(new KStatusNotifierItem(QStringLiteral("ksipphone"), this))
    , m_ringer(new Ringer(this))
{
    m_tray->setCategory(KStatusNotifierItem::Communications);
    m_tray->setTitle(i18n("KSipPhone"));
    m_tray->setIconByName(QStringLiteral("call-start"));
    m_tray->setStandardActionsEnabled(false);
    m_window->winId();
    m_tray->setAssociatedWindow(m_window->windowHandle());

    auto *menu = new QMenu;
    menu->addAction(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Show dialer"), this, [this] {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("call-stop")), i18n("Hang up"), m_engine, &Engine::hangup);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("configure")), i18n("Settings…"), this, &PhoneApp::openSettings);
    menu->addSeparator();
    menu->addAction(QIcon::fromTheme(QStringLiteral("application-exit")), i18n("Quit"), this, &PhoneApp::quit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &KStatusNotifierItem::activateRequested, this, [this](bool) {
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
    connect(m_window, &MainWindow::searchRequested, m_phonebook, &Phonebook::search);
    connect(m_phonebook, &Phonebook::searchFinished, m_window, &MainWindow::setSearchResults);
    connect(m_phonebook, &Phonebook::errorOccurred, m_window, &MainWindow::setPhonebookError);
    connect(m_engine, &Engine::codecChanged, m_window, &MainWindow::setCodec);

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
        KNotification::event(KNotification::Error, i18n("KSipPhone"), msg);
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
        if (s != Engine::CallState::Incoming)
            closeRingNotification();
        if (s == Engine::CallState::Idle) {
            if (m_prevState == Engine::CallState::Incoming && !m_wasEstablished) {
                ++m_missed;
                KNotification::event(QStringLiteral("missedCall"), i18n("Missed call"), peerText(),
                                     QStringLiteral("call-missed"), KNotification::Persistent);
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
    QString pw;
    std::unique_ptr<Wallet> w(Wallet::openWallet(Wallet::NetworkWallet(), 0, Wallet::Synchronous));
    if (w && w->hasFolder(kWalletFolder) && w->setFolder(kWalletFolder))
        w->readPassword(key, pw);
    return pw;
}

void PhoneApp::writePassword(const QString &key, const QString &password)
{
    std::unique_ptr<Wallet> w(Wallet::openWallet(Wallet::NetworkWallet(), 0, Wallet::Synchronous));
    if (!w) {
        KNotification::event(KNotification::Error, i18n("KSipPhone"), i18n("KWallet is not available; the password was not stored."));
        return;
    }
    if (!w->hasFolder(kWalletFolder))
        w->createFolder(kWalletFolder);
    w->setFolder(kWalletFolder);
    w->writePassword(key, password);
}

void PhoneApp::applySettings()
{
    const Settings s = Settings::load();
    m_phonebook->configure(s.ldapUri, s.ldapBase);
    m_engine->setAudioDevices(s.speakerDevice, s.micDevice);
    m_ringer->configure(s.ringFile, s.ringDevice, s.ringLoop);
    Account a;
    a.user = s.user;
    a.server = s.server;
    a.transport = s.transport;
    a.preferIpv6 = s.preferIpv6;
    a.displayName = s.displayName;
    a.codecs = s.codecs.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (a.isValid())
        a.password = readPassword(QStringLiteral("sip:%1@%2").arg(s.user, s.server));
    m_engine->setAccount(a);
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
    m_ring = new KNotification(QStringLiteral("incomingCall"), KNotification::Persistent);
    m_ring->setTitle(i18n("Incoming call"));
    m_ring->setText(peerText());
    m_ring->setIconName(QStringLiteral("call-start"));
    m_ring->setUrgency(KNotification::CriticalUrgency);
    auto *acc = m_ring->addAction(i18n("Accept"));
    connect(acc, &KNotificationAction::activated, m_engine, &Engine::accept);
    auto *dec = m_ring->addAction(i18n("Decline"));
    connect(dec, &KNotificationAction::activated, m_engine, &Engine::hangup);
    auto *def = m_ring->addDefaultAction(i18n("Show dialer"));
    connect(def, &KNotificationAction::activated, this, [this] {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    });
    m_ring->sendEvent();
    m_ringer->start();
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
    const auto cs = m_engine->callState();
    const auto rs = m_engine->regState();
    QString icon = QStringLiteral("call-start");
    QString tip;
    // Active (not Passive): Plasma moves passive items into the hidden part of the tray.
    KStatusNotifierItem::ItemStatus status = KStatusNotifierItem::Active;
    if (cs == Engine::CallState::Incoming) {
        status = KStatusNotifierItem::NeedsAttention;
        tip = i18n("Incoming call from %1", peerText());
    } else if (cs != Engine::CallState::Idle) {
        status = KStatusNotifierItem::Active;
        icon = QStringLiteral("call-start");
        tip = i18n("In call with %1", peerText());
    } else if (m_missed > 0) {
        status = KStatusNotifierItem::NeedsAttention;
        icon = QStringLiteral("call-missed");
        tip = i18np("1 missed call", "%1 missed calls", m_missed);
    } else if (rs == Engine::RegState::Registered) {
        tip = i18n("Registered");
    } else {
        status = KStatusNotifierItem::Active;
        icon = QStringLiteral("call-stop");
        tip = i18n("Not registered");
    }
    m_tray->setIconByName(icon);
    m_tray->setAttentionIconByName(icon);
    m_tray->setStatus(status);
    m_tray->setToolTip(icon, i18n("KSipPhone"), tip);
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

void PhoneApp::quit()
{
    if (m_engine->callState() != Engine::CallState::Idle)
        m_engine->hangup();
    qApp->quit();
}
