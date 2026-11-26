// SPDX-License-Identifier: MIT
// Audio codecs KSipPhone knows: settings list, baresip module and the codec spec for "audio_codecs".
#pragma once

#include <QList>
#include <QString>

struct CodecInfo {
    QString id;     // stored in the settings ("G722", "opus", ...)
    QString spec;   // baresip audio_codecs entry (wideband codecs need their rate)
    QString module; // baresip module providing it
    QString label;  // short name shown in the settings (translated)
    QString hint;   // longer explanation (tooltip)
};

namespace Codecs
{
// all codecs in default preference order
QList<CodecInfo> catalogue();
const CodecInfo *find(const QString &id);
// is the baresip module for this codec available (module file, or compiled into a static baresip)?
bool moduleAvailable(const QString &module);
// codecs baresip actually registered ("aucodec: NAME/rate/ch" at startup); empty until the engine ran
void setRegistered(const QStringList &names);
// registered if known, otherwise judged by the module
bool available(const CodecInfo &codec);
QString moduleDir();
}
