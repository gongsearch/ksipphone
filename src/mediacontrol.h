// SPDX-License-Identifier: MIT
// Desktop media integration (Linux, MPRIS over D-Bus):
// - pauses playing media players while a call rings or runs and resumes them afterwards
// - during a call KSipPhone appears as an MPRIS player, so media keys and headset buttons
//   answer (play/pause while ringing) and hang up (play/pause, pause or stop during the call)
#pragma once

#include <QObject>
#include <QStringList>

class MediaControl : public QObject
{
    Q_OBJECT
public:
    explicit MediaControl(QObject *parent = nullptr);
    ~MediaControl() override;

    static bool isAvailable();
    void configure(bool pauseMedia, bool mediaKeys);

    enum class State { Idle, Ringing, Active };
    void setCallState(State state, const QString &peer);

Q_SIGNALS:
    void acceptRequested();
    void hangupRequested();

private:
    friend class MprisPlayerAdaptor;
    friend class MprisRootAdaptor;
    void pauseOthers();
    void resumePaused();
    void registerPlayer(bool on);
    void emitPlayerChanged();
    void playPause();
    void stop();

    bool m_pauseMedia = true, m_mediaKeys = true;
    bool m_registered = false;
    State m_state = State::Idle;
    QString m_peer;
    QStringList m_paused; // MPRIS services we paused
};
