// SPDX-License-Identifier: MIT
// Ring tone playback on a selectable PipeWire output, once or looped.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

class QMediaPlayer;
class QAudioOutput;

struct AudioNode {
    QString name;        // PipeWire node.name (used by baresip and to pick the Qt output)
    QString description; // human readable
};

// Lists PipeWire sinks or sources (monitors excluded) via pw-dump.
QList<AudioNode> pipewireNodes(bool sources);

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
