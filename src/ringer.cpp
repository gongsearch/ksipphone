// SPDX-License-Identifier: MIT
#include "ringer.h"

#include <QAudioDevice>
#include <QAudioOutput>
#include <QCoreApplication>
#include <QFile>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QUrl>

QString audioDeviceKey(const QAudioDevice &device)
{
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    return QString::fromUtf8(device.id());
#else
    return device.description();
#endif
}

QList<AudioNode> audioDevices(bool inputs)
{
    QList<AudioNode> list;
    const QList<QAudioDevice> devs = inputs ? QMediaDevices::audioInputs() : QMediaDevices::audioOutputs();
    for (const QAudioDevice &d : devs) {
        if (QString::fromUtf8(d.id()).endsWith(QLatin1String(".monitor")))
            continue;
        list.append({audioDeviceKey(d), d.description()});
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
                          QStringLiteral("/usr/share/baresip/ring.wav"),
                          QString(QCoreApplication::applicationDirPath() + QStringLiteral("/share/baresip/ring.wav"))}) {
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
            if (audioDeviceKey(d) == deviceNode) {
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
