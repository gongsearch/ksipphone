// SPDX-License-Identifier: MIT
// Ties engine, phonebook, dialer window, tray icon and notifications together.
#pragma once

#include "calllog.h"
#include "engine.h"
#include "phonebook.h"

#include <QObject>
#include <QPixmap>
#include <QPointer>

namespace Desktop
{
class Tray;
class Notification;
}
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

private Q_SLOTS:
    void systemNotificationProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

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
    void showWindow(bool onTop = false);
    void transferCall(const QString &target);
    void setDnd(bool on);
    bool dndActive() const { return m_dnd || (m_dndFollowSystem && m_systemDnd); }
    void updateDnd();
    void notifyWaiting(const QString &text);
    void showHistory();
    void logCall(CallLog::Direction dir, const QString &number, const QString &name, int seconds, bool elsewhere = false);
    void updateExtensions();

    Engine *m_engine;
    Phonebook *m_phonebook;
    class LocalContacts *m_local;
    MainWindow *m_window;
    Desktop::Tray *m_tray;
    Ringer *m_ringer;
    QPointer<Desktop::Notification> m_ring;
    QString m_peerNumber, m_peerName;
    QPixmap m_peerPhoto;
    Engine::CallState m_prevState = Engine::CallState::Idle;
    bool m_wasEstablished = false;
    int m_lookupId = 0;
    int m_missed = 0;
    int m_voicemails = 0;
    QPointer<Desktop::Notification> m_vmNotification;
    // watched extensions: number -> name from the phonebook
    QStringList m_watched;
    QHash<QString, QString> m_extNames;
    QHash<int, QString> m_extLookups; // lookup id -> extension
    int m_nextExtLookup = 1000000;
    QString m_consultName, m_consultNumber;
    QPixmap m_consultPhoto;
    QPointer<Desktop::Notification> m_waitNote;
    CallLog *m_callLog;
    class MediaControl *m_media;
    class MuteControl *m_muteCtl;
    class GlobalShortcuts *m_global;
    class WindowPlacer *m_placer;
    bool m_alwaysOnTop = false;
    QPointer<class CallLogDialog> m_historyDialog;
    bool m_callIncoming = false;
    QDateTime m_callStart;
    qint64 m_establishedMs = 0;
    bool m_dnd = false, m_systemDnd = false, m_dndReject = false, m_dndFollowSystem = true;
    class QAction *m_dndAction = nullptr;
    int m_consultLookup = -1;
};
