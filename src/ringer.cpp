// SPDX-License-Identifier: GPL-3.0-or-later
#include "ringer.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QProcess>
#include <QUrl>

QList<AudioNode> pipewireNodes(bool sources)
{
    QList<AudioNode> list;
    QProcess p;
    p.start(QStringLiteral("pw-dump"), {});
    if (!p.waitForFinished(5000))
        return list;
    const QJsonArray all = QJsonDocument::fromJson(p.readAllStandardOutput()).array();
    const QString cls = sources ? QStringLiteral("Audio/Source") : QStringLiteral("Audio/Sink");
    for (const QJsonValue &v : all) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("PipeWire:Interface:Node"))
            continue;
        const QJsonObject props = o.value(QStringLiteral("info")).toObject().value(QStringLiteral("props")).toObject();
        const QString mc = props.value(QStringLiteral("media.class")).toString();
        if (mc != cls && !(sources && mc == QLatin1String("Audio/Source/Virtual")))
            continue;
        AudioNode n;
        n.name = props.value(QStringLiteral("node.name")).toString();
        n.description = props.value(QStringLiteral("node.description")).toString(n.name);
        if (!n.name.isEmpty())
            list.append(n);
    }
    std::sort(list.begin(), list.end(), [](const AudioNode &a, const AudioNode &b) {
        return a.description.localeAwareCompare(b.description) < 0;
    });
    return list;
}

Ringer::Ringer(QObject *parent)
    : QObject(parent)
{
}

QString Ringer::defaultFile()
{
    for (const auto &f : {QStringLiteral("/usr/share/sounds/ocean/stereo/phone-incoming-call.oga"),
                          QStringLiteral("/usr/share/sounds/freedesktop/stereo/phone-incoming-call.oga"),
                          QStringLiteral("/usr/share/baresip/ring.wav")}) {
        if (QFile::exists(f))
            return f;
    }
    return {};
}

void Ringer::configure(const QString &file, const QString &deviceNode, bool loop)
{
    m_file = file.isEmpty() ? defaultFile() : file;
    m_device = deviceNode;
    m_loop = loop;
}

void Ringer::start()
{
    play(m_file, m_device, m_loop);
}

void Ringer::preview(const QString &file, const QString &deviceNode)
{
    play(file.isEmpty() ? defaultFile() : file, deviceNode, false);
}

void Ringer::play(const QString &file, const QString &deviceNode, bool loop)
{
    stop();
    if (file.isEmpty())
        return;
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_output = new QAudioOutput(this);
        m_player->setAudioOutput(m_output);
    }
    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (!deviceNode.isEmpty()) {
        for (const QAudioDevice &d : QMediaDevices::audioOutputs()) {
            if (QString::fromUtf8(d.id()) == deviceNode) {
                dev = d;
                break;
            }
        }
    }
    m_output->setDevice(dev);
    m_player->setSource(QUrl::fromLocalFile(file));
    m_player->setLoops(loop ? QMediaPlayer::Infinite : 1);
    m_player->play();
}

void Ringer::stop()
{
    if (m_player)
        m_player->stop();
}
