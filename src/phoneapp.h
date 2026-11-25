// SPDX-License-Identifier: MIT
// Ties engine, phonebook, dialer window, tray icon and notifications together.
#pragma once

#include "engine.h"
#include "phonebook.h"

#include <QObject>
#include <QPixmap>
#include <QPointer>

class KNotification;
class KStatusNotifierItem;
class MainWindow;
class Ringer;

class PhoneApp : public QObject
{
    Q_OBJECT
public:
    explicit PhoneApp(QObject *parent = nullptr);
    ~PhoneApp() override;

    void start(bool minimized);
    void handleArguments(const QStringList &args, bool fromRemote);

private:
    void applySettings();
    void openSettings();
    QString readPassword(const QString &key);
    void writePassword(const QString &key, const QString &password);
    QString peerText() const;
    void updateTray();
    void notifyIncoming();
    void closeRingNotification();
    void notifyMissed();
    void callBack(const QString &number);
    void callVoicemail();
    void quit();

    Engine *m_engine;
    Phonebook *m_phonebook;
    MainWindow *m_window;
    KStatusNotifierItem *m_tray;
    Ringer *m_ringer;
    QPointer<KNotification> m_ring;
    QString m_peerNumber, m_peerName;
    QPixmap m_peerPhoto;
    Engine::CallState m_prevState = Engine::CallState::Idle;
    bool m_wasEstablished = false;
    int m_lookupId = 0;
    int m_missed = 0;
    int m_voicemails = 0;
    QPointer<KNotification> m_vmNotification;
};
