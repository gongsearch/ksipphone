// SPDX-License-Identifier: MIT
// Compact dialer window.
#pragma once

#include "engine.h"
#include "phonebook.h"

#include <QHash>
#include <QPair>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class SearchField;
class QPushButton;
class QToolButton;
class QTimer;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(Phonebook *phonebook, QWidget *parent = nullptr);

    void setRegistration(const QString &text, bool ok);
    void setCall(Engine::CallState state, const QString &peerText);
    void setNumber(const QString &number);
    void setExtensions(const QList<QPair<QString, QString>> &extensions); // number, name
    void setPresence(const QHash<QString, Engine::Presence> &states);
    void setConsult(Engine::CallState state, const QString &peerText, bool waitingCall = false);
    void setWaiting(const QString &peerText); // empty: no call waiting
    void setDnd(bool on, bool bySystem);
    void setMuted(bool muted);        // without emitting muteToggled
    void setAlwaysOnTop(bool on);
    void setPhonebookError(const QString &message);
    void setCodec(const QString &codec);
    void setPeerPhoto(const QPixmap &photo); // null pixmap hides it
    void setVoicemail(int newMessages, bool configured);

Q_SIGNALS:
    void dialRequested(const QString &target);
    void hangupRequested();
    void acceptRequested();
    void muteToggled(bool on);
    void holdToggled(bool on);
    void dtmf(const QString &digit);
    void transferRequested(const QString &target); // empty: ask for the target
    void consultCompleteRequested();
    void consultAbortRequested();
    void acceptWaitingRequested();
    void dndToggled(bool on);
    void historyRequested();
    void alwaysOnTopToggleRequested();
    void quitRequested();
    void rejectWaitingRequested();
    void settingsRequested();
    void voicemailRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void keypad(const QString &digit);
    void callClicked();
    void updatePresenceIcons();
    void refreshExtList();
    void setupShortcuts();
    void applyExtVisibility();
    void saveGeometryNow();
    void updateTransferButtons();

    QLabel *m_reg, *m_status, *m_pbError, *m_codec, *m_photo, *m_peerName, *m_duration;
    QWidget *m_peerBox;
    QTimer *m_callTimer;
    qint64 m_callStartMs = 0;
    SearchField *m_number;
    QListWidget *m_ext;
    QWidget *m_extPanel;
    QLineEdit *m_extFilter;
    QList<QPair<QString, QString>> m_extensions; // number, name
    int m_extSort = 0;                           // 0 number, 1 first name, 2 last name, 3 status
    QToolButton *m_extToggle;
    QToolButton *m_dnd;
    QToolButton *m_settings = nullptr, *m_history = nullptr;
    bool m_alwaysOnTop = false;
    class QFrame *m_waitBox;
    QLabel *m_waitText;
    bool m_secondIsWaiting = false;
    QTimer *m_saveTimer = nullptr;
    QSize m_wideSize;
    class QGridLayout *m_body = nullptr;
    class QVBoxLayout *m_left = nullptr;
    QList<QPushButton *> m_keys;
    int m_heightBeforeCall = -1;
    int m_extShow = 0; // 0 all, 1 online (free or busy), 2 only free
    QHash<QString, Engine::Presence> m_presence;
    Engine::CallState m_consult = Engine::CallState::Idle;
    QPushButton *m_call, *m_hangup;
    QToolButton *m_mute, *m_hold, *m_vm, *m_transfer;
    Engine::CallState m_state = Engine::CallState::Idle;
};
