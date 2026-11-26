// SPDX-License-Identifier: MIT
#include "keepabove.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTemporaryFile>
#include <QWidget>

#ifdef KSIPPHONE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#endif

Q_LOGGING_CATEGORY(KEEPABOVE, "ksipphone.keepabove")

#ifdef KSIPPHONE_HAVE_DBUS
// KWin (Plasma 6): run a script that sets keepAbove on our window (matched by process id and caption)
static bool kwinKeepAbove(const QWidget *window, bool on)
{
    QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus || !bus->isServiceRegistered(QStringLiteral("org.kde.KWin")).value())
        return false;
    QTemporaryFile script(QStringLiteral("ksipphone-keepabove-XXXXXX.js"));
    script.setAutoRemove(true);
    if (!script.open())
        return false;
    QString caption = window->windowTitle();
    caption.replace(QLatin1Char('\\'), QLatin1String("\\\\")).replace(QLatin1Char('"'), QLatin1String("\\\""));
    const QString js = QStringLiteral(
                           "const list = workspace.windowList ? workspace.windowList() : workspace.clientList();\n"
                           "for (const w of list) {\n"
                           "    if (w.pid === %1 && w.caption === \"%2\")\n"
                           "        w.keepAbove = %3;\n"
                           "}\n")
                           .arg(QCoreApplication::applicationPid())
                           .arg(caption, on ? QStringLiteral("true") : QStringLiteral("false"));
    script.write(js.toUtf8());
    script.flush();
    const QString name = QStringLiteral("ksipphone-keepabove");
    QDBusInterface scripting(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting"), QStringLiteral("org.kde.kwin.Scripting"));
    scripting.call(QStringLiteral("unloadScript"), name);
    const QDBusReply<int> id = scripting.call(QStringLiteral("loadScript"), script.fileName(), name);
    if (!id.isValid() || id.value() < 0) {
        qCWarning(KEEPABOVE) << "KWin did not load the script" << id.error().message();
        return false;
    }
    QDBusInterface run(QStringLiteral("org.kde.KWin"), QStringLiteral("/Scripting/Script%1").arg(id.value()), QStringLiteral("org.kde.kwin.Script"));
    run.call(QStringLiteral("run"));
    scripting.call(QStringLiteral("unloadScript"), name);
    return true;
}
#endif

void KeepAbove::apply(QWidget *window, bool on)
{
    if (!window)
        return;
#ifdef KSIPPHONE_HAVE_DBUS
    if (QGuiApplication::platformName() == QLatin1String("wayland")) {
        if (window->isVisible())
            kwinKeepAbove(window, on);
        return;
    }
#endif
    if (bool(window->windowFlags() & Qt::WindowStaysOnTopHint) == on)
        return;
    const bool visible = window->isVisible();
    window->setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (visible)
        window->show(); // changing flags hides the window
}
