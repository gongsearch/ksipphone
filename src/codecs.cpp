// SPDX-License-Identifier: MIT
#include "codecs.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <algorithm>

#ifndef KSIPPHONE_BARESIP_MODULE_PATH
#define KSIPPHONE_BARESIP_MODULE_PATH "/usr/lib/baresip/modules"
#endif
// Set for builds that ship a static baresip: the modules compiled into it ("," separated).
#ifndef KSIPPHONE_STATIC_BARESIP_MODULES
#define KSIPPHONE_STATIC_BARESIP_MODULES ""
#endif

QList<CodecInfo> Codecs::catalogue()
{
    return {
        {QStringLiteral("G722"), QStringLiteral("G722/16000/1"), QStringLiteral("g722"), i18n("G.722 (HD)"), i18n("G.722 – HD voice (16 kHz)")},
        {QStringLiteral("opus"), QStringLiteral("opus/48000/2"), QStringLiteral("opus"), i18n("Opus (HD)"), i18n("Opus – HD voice, low bandwidth")},
        {QStringLiteral("AMR-WB"), QStringLiteral("AMR-WB/16000/1"), QStringLiteral("amr"), i18n("AMR-WB (HD)"), i18n("AMR-WB (G.722.2) – HD voice of mobile networks")},
        {QStringLiteral("PCMA"), QStringLiteral("PCMA"), QStringLiteral("g711"), i18n("G.711 A-law (PCMA)"), i18n("G.711 A-law (PCMA) – standard in Europe")},
        {QStringLiteral("PCMU"), QStringLiteral("PCMU"), QStringLiteral("g711"), i18n("G.711 µ-law (PCMU)"), i18n("G.711 µ-law (PCMU) – standard in North America")},
        {QStringLiteral("AMR"), QStringLiteral("AMR/8000/1"), QStringLiteral("amr"), i18n("AMR"), i18n("AMR – narrowband codec of mobile networks")},
        {QStringLiteral("L16"), QStringLiteral("L16/16000/1"), QStringLiteral("l16"), i18n("L16 (LAN only)"), i18n("L16 – uncompressed 16 kHz, for the local network only")},
    };
}

const CodecInfo *Codecs::find(const QString &id)
{
    static const QList<CodecInfo> all = catalogue();
    auto it = std::find_if(all.begin(), all.end(), [&](const CodecInfo &c) { return c.id.compare(id, Qt::CaseInsensitive) == 0; });
    return it == all.end() ? nullptr : &*it;
}

QString Codecs::moduleDir()
{
    const QString env = qEnvironmentVariable("KSIPPHONE_BARESIP_MODULES");
    QString configured = env.isEmpty() ? QStringLiteral(KSIPPHONE_BARESIP_MODULE_PATH) : env;
    if (!QDir::isAbsolutePath(configured) && configured.contains(QLatin1Char('/')))
        configured = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(configured);
    if (QDir(configured).exists() || !env.isEmpty())
        return configured;
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    // distributions install the modules in different places (Arch /usr/lib, Debian /usr/lib/<triplet>, Fedora /usr/lib64)
    QStringList candidates = {QStringLiteral("/usr/lib/baresip/modules"), QStringLiteral("/usr/lib64/baresip/modules"),
                              QStringLiteral("/usr/local/lib/baresip/modules"), QStringLiteral("/usr/local/lib64/baresip/modules")};
    const QStringList triplets = QDir(QStringLiteral("/usr/lib")).entryList({QStringLiteral("*-linux-gnu*")}, QDir::Dirs);
    for (const QString &t : triplets)
        candidates << QStringLiteral("/usr/lib/%1/baresip/modules").arg(t);
    for (const QString &c : std::as_const(candidates)) {
        if (QFile::exists(c + QStringLiteral("/ctrl_tcp.so")))
            return c;
    }
#endif
    return configured;
}

static QStringList s_registered;

void Codecs::setRegistered(const QStringList &names)
{
    s_registered = names;
}

bool Codecs::available(const CodecInfo &codec)
{
    if (!s_registered.isEmpty())
        return s_registered.contains(codec.spec.section(QLatin1Char('/'), 0, 0), Qt::CaseInsensitive);
    return moduleAvailable(codec.module);
}

bool Codecs::moduleAvailable(const QString &module)
{
    // builds with a bundled static baresip know what is compiled in
    static const QStringList compiledIn = QStringLiteral(KSIPPHONE_STATIC_BARESIP_MODULES).split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (!compiledIn.isEmpty())
        return compiledIn.contains(module);
#if defined(Q_OS_WIN)
    const QString ext = QStringLiteral(".dll");
#else
    const QString ext = QStringLiteral(".so");
#endif
    return QFile::exists(moduleDir() + QLatin1Char('/') + module + ext);
}
