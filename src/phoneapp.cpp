// SPDX-License-Identifier: MIT
#include "phoneapp.h"
#include <QTabWidget>

#include "mainwindow.h"
#include "localcontacts.h"
#include "ringer.h"
#include "transferdialog.h"
#include "calllogdialog.h"

#include <QAction>
#include <QSettings>
#ifdef KSIPPHONE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusVariant>
#endif
#include "secrets.h"
#include "settingsdialog.h"

#include "desktop.h"

#include <KLocalizedString>

#include <QApplication>
#include <QLocale>
#include <QMenu>
#include <QTime>
#include <QTimer>
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
    , m_local(new LocalContacts(this))
    , m_window(new MainWindow(m_phonebook))
    , m_tray(new Desktop::Tray(m_window, this))
    , m_ringer(new Ringer(this))
    , m_callLog(new CallLog(this))
{
    m_tray->setIconName(QStringLiteral("call-start"));
    m_phonebook->setLocalContacts(m_local);
    Desktop::Notification::setTray(m_tray);

    auto *menu = new QMenu;
    menu->addAction(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Show dialer"), this, [this] {
        m_window->show();
        m_window->raise();
        m_window->activateWindow();
    });
    menu->addAction(QIcon::fromTheme(QStringLiteral("mail-message")), i18n("Call voicemail"), this, &PhoneApp::callVoicemail);
    menu->addAction(QIcon::fromTheme(QStringLiteral("view-history")), i18n("Call history"), this, &PhoneApp::showHistory);
    menu->addAction(QIcon::fromTheme(QStringLiteral("call-stop")), i18n("Hang up"), m_engine, &Engine::hangup);
    menu->addSeparator();
    m_dndAction = menu->addAction(QIcon::fromTheme(QStringLiteral("notifications-disabled")), i18n("Do not disturb"));
    m_dndAction->setCheckable(true);
    connect(m_dndAction, &QAction::toggled, this, &PhoneApp::setDnd);
    connect(m_window, &MainWindow::dndToggled, this, &PhoneApp::setDnd);
    connect(m_window, &MainWindow::historyRequested, this, &PhoneApp::showHistory);
    m_dnd = appSettings().value(QStringLiteral("General/DoNotDisturb"), false).toBool();
#ifdef KSIPPHONE_HAVE_DBUS
    // the desktop's do-not-disturb (Plasma, GNOME via org.freedesktop.Notifications "Inhibited")
    {
        QDBusInterface props(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                             QStringLiteral("org.freedesktop.DBus.Properties"));
        const QDBusReply<QDBusVariant> r = props.call(QStringLiteral("Get"), QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Inhibited"));
        if (r.isValid())
            m_systemDnd = r.value().variant().toBool();
        QDBusConnection::sessionBus().connect(QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
                                              QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                                              SLOT(systemNotificationProperties(QString, QVariantMap, QStringList)));
    }
#endif
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
    connect(m_window, &MainWindow::transferRequested, this, &PhoneApp::transferCall);
    connect(m_window, &MainWindow::consultCompleteRequested, m_engine, &Engine::completeConsult);
    connect(m_window, &MainWindow::consultAbortRequested, m_engine, &Engine::abortConsult);
    connect(m_engine, &Engine::consultStateChanged, this, [this](Engine::CallState s, const QString &uri) {
        m_window->setConsult(s, m_consultName.isEmpty() ? userPart(uri) : m_consultName, m_engine->secondCall() == Engine::SecondCall::Waiting);
    });
    connect(m_engine, &Engine::transferFailed, this, [](const QString &reason) {
        Desktop::Notification::error(i18n("Transfer failed"), reason.isEmpty() ? i18n("The call could not be transferred.") : reason);
    });
    connect(m_engine, &Engine::presenceChanged, m_window, &MainWindow::setPresence);
    // call waiting
    connect(m_window, &MainWindow::acceptWaitingRequested, m_engine, &Engine::acceptWaiting);
    connect(m_window, &MainWindow::rejectWaitingRequested, m_engine, &Engine::rejectWaiting);
    connect(m_engine, &Engine::waitingCall, this, [this](const QString &uri, const QString &name) {
        m_consultNumber = userPart(uri);
        m_consultName = name;
        m_consultPhoto = QPixmap();
        m_consultLookup = ++m_nextExtLookup;
        m_phonebook->lookupNumber(m_consultNumber, m_consultLookup);
        if (dndActive() && m_dndReject) {
            m_engine->rejectWaiting();
            return;
        }
        const QString text = name.isEmpty() || name == m_consultNumber ? m_consultNumber : QStringLiteral("%1 (%2)").arg(name, m_consultNumber);
        m_window->setWaiting(text);
        notifyWaiting(text);
    });
    connect(m_engine, &Engine::waitingEnded, this, [this](Engine::WaitingEnd how) {
        if (how == Engine::WaitingEnd::Gone)
            logCall(CallLog::Direction::Missed, m_consultNumber, m_consultName, 0);
        else if (how == Engine::WaitingEnd::Accepted)
            logCall(CallLog::Direction::Incoming, m_consultNumber, m_consultName, 0);
        m_window->setWaiting(QString());
        if (m_waitNote)
            m_waitNote->close();
    });
    connect(m_engine, &Engine::callsSwapped, this, [this] {
        std::swap(m_peerNumber, m_consultNumber);
        std::swap(m_peerName, m_consultName);
        std::swap(m_peerPhoto, m_consultPhoto);
        m_window->setCall(m_engine->callState(), peerText());
        m_window->setPeerPhoto(m_peerPhoto);
    });
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
        // user@host without a port, shorter in the status line
        QString host = st.server;
        if (!host.startsWith(QLatin1Char('[')) && host.count(QLatin1Char(':')) == 1)
            host = host.section(QLatin1Char(':'), 0, 0);
        const QString who = QStringLiteral("%1@%2").arg(st.user, host);
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
        if (dndActive() && m_dndReject) {
            m_engine->hangup(); // busy; it shows up as a missed call
            return;
        }
        notifyIncoming();
    });
    connect(m_phonebook, &Phonebook::lookupFinished, this, [this](int id, const QString &name, const QByteArray &photo) {
        if (const auto it = m_extLookups.constFind(id); it != m_extLookups.constEnd()) {
            if (!name.isEmpty())
                m_extNames.insert(*it, name);
            m_extLookups.erase(it);
            if (m_extLookups.isEmpty())
                updateExtensions();
            return;
        }
        if (id == m_consultLookup) {
            if (!photo.isEmpty())
                m_consultPhoto.loadFromData(photo);
            if (!name.isEmpty()) {
                m_consultName = name;
                if (m_engine->consultState() != Engine::CallState::Idle)
                    m_window->setConsult(m_engine->consultState(), name, m_engine->secondCall() == Engine::SecondCall::Waiting);
                else if (m_waitNote) {
                    const QString text = QStringLiteral("%1 (%2)").arg(name, m_consultNumber);
                    m_window->setWaiting(text);
                    m_waitNote->setText(text);
                }
            }
            return;
        }
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
        if (m_prevState == Engine::CallState::Idle && (s == Engine::CallState::Incoming || s == Engine::CallState::Outgoing)) {
            m_callIncoming = s == Engine::CallState::Incoming;
            m_callStart = QDateTime::currentDateTimeUtc();
            m_establishedMs = 0;
        }
        if (s == Engine::CallState::Established && m_establishedMs == 0)
            m_establishedMs = QDateTime::currentMSecsSinceEpoch();
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
            const int secs = m_establishedMs ? int((QDateTime::currentMSecsSinceEpoch() - m_establishedMs) / 1000) : 0;
            if (!m_callIncoming)
                logCall(CallLog::Direction::Outgoing, m_peerNumber, m_peerName, secs);
            else if (m_wasEstablished)
                logCall(CallLog::Direction::Incoming, m_peerNumber, m_peerName, secs);
            else
                logCall(elsewhere ? CallLog::Direction::Incoming : CallLog::Direction::Missed, m_peerNumber, m_peerName, 0, elsewhere);
            m_establishedMs = 0;
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
    // developer aid (see openSettings): KSIPPHONE_GRAB_DIR also saves an image of the main window
    if (const QString grabDir = qEnvironmentVariable("KSIPPHONE_GRAB_DIR"); !grabDir.isEmpty() && !minimized) {
        QTimer::singleShot(3000, m_window, [this, grabDir] { m_window->grab().save(grabDir + QStringLiteral("/main.png")); });
    }
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
    m_local->setEnabled(s.useLocalContacts && LocalContacts::isAvailable());
    m_phonebook->setNameFormat(static_cast<Phonebook::NameFormat>(s.nameFormat));
    m_engine->setAudioDevices(s.speakerDevice, s.micDevice, s.echoCancel);
    m_ringer->configure(s.ringFile, s.ringDevice, s.ringLoop);
    m_dndReject = s.dndReject;
    m_dndFollowSystem = s.dndFollowSystem;
    updateDnd();
    Account a;
    a.user = s.user;
    a.server = s.server;
    a.transport = s.transport;
    a.preferIpv6 = s.preferIpv6;
    a.keepAlive = s.keepAlive;
    a.dtmfMode = s.dtmfMode;
    a.displayName = s.displayName;
    a.codecs = s.codecs.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (a.isValid())
        a.password = readPassword(QStringLiteral("sip:%1@%2").arg(s.user, s.server));
    m_engine->setAccount(a);
    m_window->setVoicemail(m_voicemails, !s.voicemail.isEmpty());
    // extensions with presence; names from the phonebook (LDAP / address book)
    m_engine->setWatchList(s.watched);
    if (s.watched != m_watched) {
        m_watched = s.watched;
        m_extNames.clear();
        m_extLookups.clear();
        for (const QString &ext : std::as_const(m_watched)) {
            const int id = ++m_nextExtLookup;
            m_extLookups.insert(id, ext);
            m_phonebook->lookupNumber(ext, id);
        }
        updateExtensions();
    }
    if (!a.isValid())
        m_window->setRegistration(i18n("No SIP account configured"), false);
}

void PhoneApp::updateExtensions()
{
    QList<QPair<QString, QString>> list;
    for (const QString &ext : std::as_const(m_watched))
        list.append({ext, m_extNames.value(ext)});
    m_window->setExtensions(list);
}

void PhoneApp::transferCall(const QString &target)
{
    if (m_engine->callState() != Engine::CallState::Established)
        return;
    TransferDialog dlg(m_phonebook, target, m_window->isVisible() ? m_window : nullptr);
    if (dlg.exec() != QDialog::Accepted || m_engine->callState() != Engine::CallState::Established)
        return;
    const QString t = dlg.target();
    if (dlg.mode() == TransferDialog::Mode::Blind) {
        m_engine->transfer(t);
        return;
    }
    m_consultName.clear();
    m_consultLookup = ++m_nextExtLookup;
    m_phonebook->lookupNumber(t, m_consultLookup);
    m_engine->startConsult(t);
}

void PhoneApp::setDnd(bool on)
{
    m_dnd = on;
    appSettings().setValue(QStringLiteral("General/DoNotDisturb"), on);
    updateDnd();
}

void PhoneApp::updateDnd()
{
    if (m_dndAction) {
        const QSignalBlocker b(m_dndAction);
        m_dndAction->setChecked(m_dnd);
    }
    m_window->setDnd(m_dnd, m_dndFollowSystem && m_systemDnd && !m_dnd);
    if (dndActive())
        m_ringer->stop();
    updateTray();
}

void PhoneApp::systemNotificationProperties(const QString &interface, const QVariantMap &changed, const QStringList &)
{
    if (interface != QLatin1String("org.freedesktop.Notifications") || !changed.contains(QStringLiteral("Inhibited")))
        return;
    m_systemDnd = changed.value(QStringLiteral("Inhibited")).toBool();
    updateDnd();
}

void PhoneApp::logCall(CallLog::Direction dir, const QString &number, const QString &name, int seconds, bool elsewhere)
{
    CallLog::Entry e;
    e.time = dir == CallLog::Direction::Outgoing || m_callStart.isNull() ? QDateTime::currentDateTimeUtc() : m_callStart;
    if (dir == CallLog::Direction::Outgoing && m_callStart.isValid())
        e.time = m_callStart;
    e.direction = dir;
    e.number = number;
    e.name = name == number ? QString() : name;
    e.seconds = seconds;
    e.elsewhere = elsewhere;
    m_callLog->add(e);
}

void PhoneApp::showHistory()
{
    if (!m_historyDialog) {
        m_historyDialog = new CallLogDialog(m_callLog, nullptr);
        m_historyDialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(m_historyDialog, &CallLogDialog::callRequested, this, &PhoneApp::callBack);
    }
    m_missed = 0; // seen
    updateTray();
    m_historyDialog->show();
    m_historyDialog->raise();
    m_historyDialog->activateWindow();
}

void PhoneApp::notifyWaiting(const QString &text)
{
    if (m_waitNote)
        m_waitNote->close();
    m_waitNote = new Desktop::Notification(QStringLiteral("callWaiting"), Desktop::Notification::Kind::Persistent, this);
    m_waitNote->setTitle(i18n("Call waiting"));
    m_waitNote->setText(text);
    m_waitNote->setIconName(QStringLiteral("call-start"));
    const int acc = m_waitNote->addAction(i18n("Accept (hold current)"));
    const int dec = m_waitNote->addAction(i18n("Decline"));
    connect(m_waitNote, &Desktop::Notification::actionTriggered, this, [this, acc, dec](int i) {
        if (i == acc)
            m_engine->acceptWaiting();
        else if (i == dec)
            m_engine->rejectWaiting();
    });
    m_waitNote->setDefaultAction(i18n("Show dialer"));
    connect(m_waitNote, &Desktop::Notification::defaultTriggered, this, [this] { showWindow(); });
    m_waitNote->send();
}

void PhoneApp::openSettings()
{
    const Settings cur = Settings::load();
    const QString key = QStringLiteral("sip:%1@%2").arg(cur.user, cur.server);
    SettingsDialog dlg(cur, cur.user.isEmpty() ? QString() : readPassword(key), m_window->isVisible() ? m_window : nullptr);
    // developer aid: KSIPPHONE_GRAB_DIR=<dir> saves images of each settings tab (only this dialog, not the screen)
    if (const QString grabDir = qEnvironmentVariable("KSIPPHONE_GRAB_DIR"); !grabDir.isEmpty()) {
        QTimer::singleShot(1500, &dlg, [&dlg, grabDir] {
            auto *tabs = dlg.findChild<QTabWidget *>();
            for (int i = 0; tabs && i < tabs->count(); ++i) {
                tabs->setCurrentIndex(i);
                QCoreApplication::processEvents();
                dlg.grab().save(QStringLiteral("%1/settings-%2.png").arg(grabDir).arg(i));
            }
            if (tabs)
                tabs->setCurrentIndex(0);
        });
    }
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
    if (!dndActive())
        m_ringer->start(); // do not disturb: the notification only, no ringing
    // Without notification buttons the dialer window (with Accept / Decline) comes to the front.
    if (!Desktop::richNotifications() && !dndActive())
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
    if (dndActive())
        tip << (m_dndReject ? i18n("Do not disturb: calls are rejected") : i18n("Do not disturb: calls are shown silently"));
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
    if (args.contains(QLatin1String("--settings"))) {
        QTimer::singleShot(0, this, &PhoneApp::openSettings);
        return;
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
