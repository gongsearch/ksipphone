// SPDX-License-Identifier: MIT
#include "windowplacer.h"
#include "settingsdialog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPoint>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

#ifdef KSIPPHONE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#endif

Q_LOGGING_CATEGORY(PLACER, "ksipphone.placer")

static const QString kScriptName = QStringLiteral("ksipphone-placer");
static const QString kService = QStringLiteral("io.github.gongsearch.KSipPhone");
static const QString kPath = QStringLiteral("/Window");

WindowPlacer::WindowPlacer(QWidget *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
}

WindowPlacer::~WindowPlacer()
{
    stop();
}

void WindowPlacer::start()
{
#ifdef KSIPPHONE_HAVE_DBUS
    if (m_active || QGuiApplication::platformName() != QLatin1String("wayland"))
        return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.interface() || !bus.interface()->isServiceRegistered(QStringLiteral("org.kde.KWin")).value())
        return;
    // receive the position from the script
    bus.registerService(kService);
    bus.registerObject(kPath, this, QDBusConnection::ExportScriptableSlots);

    QString caption = m_window->windowTitle();
    caption.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    const QPoint saved = appSettings().value(QStringLiteral("Window/Position"), QPoint(-1000000, -1000000)).toPoint();
    const QString savedJs = saved.x() == -1000000 ? QStringLiteral("null") : QStringLiteral("{x: %1, y: %2}").arg(saved.x()).arg(saved.y());
    const QString js = QStringLiteral(
                           "const PID = %1, CAPTION = \"%2\";\n"
                           "let saved = %3;\n"
                           "function report(w) {\n"
                           "    const g = w.frameGeometry;\n"
                           "    saved = {x: g.x, y: g.y};\n"
                           "    callDBus(\"%4\", \"%5\", \"io.github.gongsearch.KSipPhone.Window\", \"storePosition\", Math.round(g.x), Math.round(g.y));\n"
                           "}\n"
                           "function track(w) {\n"
                           "    if (w.pid !== PID || w.caption !== CAPTION)\n"
                           "        return;\n"
                           "    if (saved) {\n"
                           "        const g = w.frameGeometry;\n"
                           "        w.frameGeometry = {x: saved.x, y: saved.y, width: g.width, height: g.height};\n"
                           "    }\n"
                           "    w.interactiveMoveResizeFinished.connect(() => report(w));\n"
                           "}\n"
                           "workspace.windowAdded.connect(track);\n"
                           "for (const w of workspace.windowList())\n"
                           "    track(w);\n")
                           .arg(QCoreApplication::applicationPid())
                           .arg(caption, savedJs, kService, kPath);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir().mkpath(dir);
    QFile f(dir + QStringLiteral("/placer.js"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(js.toUtf8());
    f.close();
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"));
    scripting.call(QStringLiteral("unloadScript"), kScriptName); // a leftover of an earlier run
    const QDBusReply<int> id = scripting.call(QStringLiteral("loadScript"), f.fileName(), kScriptName);
    if (!id.isValid() || id.value() < 0) {
        qCWarning(PLACER) << "KWin did not load the placement script" << id.error().message();
        return;
    }
    QDBusInterface run(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(id.value()), QStringLiteral("org.kde.kwin.Script"));
    run.call(QStringLiteral("run"));
    m_active = true;
#endif
}

void WindowPlacer::stop()
{
#ifdef KSIPPHONE_HAVE_DBUS
    if (!m_active)
        return;
    m_active = false;
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"));
    scripting.call(QStringLiteral("unloadScript"), kScriptName);
#endif
}

void WindowPlacer::storePosition(int x, int y)
{
    qCDebug(PLACER) << "window position" << x << y;
    appSettings().setValue(QStringLiteral("Window/Position"), QPoint(x, y));
}
