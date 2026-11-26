// SPDX-License-Identifier: MIT
// Microphone mute during calls: the mute button, optionally the system-wide microphone mute
// (Linux, via pactl: mic-mute key, Plasma, headset) and push-to-talk.
#pragma once

#include <QObject>
#include <QString>

class Engine;
class QProcess;
class QTimer;

class MuteControl : public QObject
{
    Q_OBJECT
public:
    explicit MuteControl(Engine *engine, QObject *parent = nullptr);
    ~MuteControl() override;

    static bool systemMicAvailable(); // pactl found (Linux)
    // source: PipeWire/Pulse source name of the microphone, empty = default
    void configure(bool followSystem, bool pushToTalk, const QString &source);

    void setInCall(bool inCall);    // an established call
    void setUserMute(bool muted);   // the mute button
    void setPushToTalk(bool down);  // push-to-talk key held
    bool muted() const { return effective(); }

Q_SIGNALS:
    void mutedChanged(bool muted); // for the button

private:
    bool effective() const { return m_user || (m_ptt && !m_pttDown); }
    void apply();
    void watchSystem(bool on);
    void querySystem();
    void setSystemMuted(bool muted);
    void systemMuteChanged(bool muted);
    QString source() const;

    Engine *m_engine;
    bool m_follow = false, m_ptt = false;
    QString m_source;
    bool m_inCall = false, m_user = false, m_pttDown = false;
    // system microphone
    bool m_sysKnown = false, m_sysMuted = false, m_sysBefore = false, m_sysChangedByUs = false;
    int m_expect = -1; // state we just set ourselves (-1: none)
    QProcess *m_subscribe = nullptr;
    QTimer *m_queryTimer = nullptr;
};
