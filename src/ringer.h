// SPDX-License-Identifier: MIT
// Ring tone playback on a selectable PipeWire output, once or looped.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QMediaPlayer;
class QAudioOutput;

struct AudioNode {
    QString name;        // device key as understood by baresip's audio module (see audioDeviceKey)
    QString description; // human readable
};

class QAudioDevice;
// Key that identifies a device for baresip: the PipeWire/PulseAudio node name on Linux,
// the device name on Windows (wasapi) and macOS (audiounit).
QString audioDeviceKey(const QAudioDevice &device);
// Audio inputs or outputs as reported by Qt Multimedia.
QList<AudioNode> audioDevices(bool inputs);

class Ringer : public QObject
{
    Q_OBJECT
public:
    explicit Ringer(QObject *parent = nullptr);

    static QString defaultFile();
    void configure(const QString &file, const QString &deviceNode, bool loop);
    void start();
    void stop();
    // one-off preview with explicit parameters (settings dialog "Test" button)
    void preview(const QString &file, const QString &deviceNode);

private:
    void play(const QString &file, const QString &deviceNode, bool loop);

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_output = nullptr;
    QString m_file, m_device;
    bool m_loop = false;
};
