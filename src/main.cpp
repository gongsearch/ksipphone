// SPDX-License-Identifier: MIT
#include "phoneapp.h"

#include <KLocalizedString>
#ifdef KSIPPHONE_WITH_KDE
#include <KAboutData>
#endif

#include <QApplication>
#include <QPalette>
#include <QStyleHints>

#include <functional>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QUrl>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocale>
#include <QLocalSocket>

// Single instance without D-Bus (works on all platforms): the first instance listens on a
// per-user local socket; later starts hand their arguments (e.g. a tel: link) to it and exit.
static QString instanceName()
{
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        user = qEnvironmentVariable("USERNAME");
    return QStringLiteral("ksipphone-") + user;
}

static bool forwardToRunningInstance(const QStringList &args)
{
    QLocalSocket sock;
    sock.connectToServer(instanceName());
    if (!sock.waitForConnected(500))
        return false;
    sock.write(QJsonDocument(QJsonArray::fromStringList(args)).toJson(QJsonDocument::Compact));
    sock.waitForBytesWritten(1000);
    sock.disconnectFromServer();
    return true;
}

#ifndef KSIPPHONE_WITH_KDE
// judged by the colours actually used for drawing, so it matches whatever the style renders
static bool isDarkScheme()
{
    const QPalette pal = QGuiApplication::palette();
    return qGray(pal.color(QPalette::WindowText).rgb()) > qGray(pal.color(QPalette::Window).rgb());
}

// re-applies the icon theme whenever the application palette changes (light/dark switch)
class PaletteWatcher : public QObject
{
public:
    PaletteWatcher(std::function<void()> apply, QObject *parent)
        : QObject(parent)
        , m_apply(std::move(apply))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
            m_apply();
        return false;
    }

private:
    std::function<void()> m_apply;
};
#endif

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    KLocalizedString::setApplicationDomain("ksipphone");
    // right-to-left UI (Arabic, Persian, Urdu ...) when such a translation is used; Qt only does
    // this by itself when its own translations are loaded, which KI18n does not do
    for (const QString &lang : KLocalizedString::languages()) {
        if (lang.startsWith(QLatin1String("en")) || KLocalizedString::isApplicationTranslatedInto(lang)) {
            if (QLocale(lang).textDirection() == Qt::RightToLeft)
                QGuiApplication::setLayoutDirection(Qt::RightToLeft);
            break;
        }
    }

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("minimized"), i18n("Start hidden in the system tray")});
    parser.addOption({QStringLiteral("settings"), i18n("Open the settings dialog")});
    parser.addPositionalArgument(QStringLiteral("uri"), i18n("tel: or sip: URI to call"), QStringLiteral("[uri]"));
#ifdef KSIPPHONE_WITH_KDE
    KAboutData about(QStringLiteral("ksipphone"), i18n("KSipPhone"), QStringLiteral(KSIPPHONE_VERSION),
                     i18n("A small SIP phone for KDE Plasma"), KAboutLicense::MIT);
    about.setOrganizationDomain("gongsearch.github.io");
    about.setHomepage(QStringLiteral("https://github.com/gongsearch/ksipphone"));
    about.setBugAddress("https://github.com/gongsearch/ksipphone/issues");
    about.setDesktopFileName(QStringLiteral("io.github.gongsearch.KSipPhone"));
    KAboutData::setApplicationData(about);
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);
#else
    QCoreApplication::setApplicationName(QStringLiteral("ksipphone"));
    QCoreApplication::setApplicationVersion(QStringLiteral(KSIPPHONE_VERSION));
    QCoreApplication::setOrganizationDomain(QStringLiteral("gongsearch.github.io"));
    QGuiApplication::setApplicationDisplayName(i18n("KSipPhone"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.gongsearch.KSipPhone"));
    parser.setApplicationDescription(i18n("A small SIP phone for KDE Plasma"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);
    // Bundled Breeze subset. On Windows/macOS it is the primary theme: Qt's own platform icon theme there
    // only knows standard names and returns empty icons for others (e.g. "configure"), so the fallback
    // would never be used. On Linux (Qt-only builds) the system theme stays primary.
    // Breeze for light, Breeze Dark for dark colour schemes (the SVGs are not recoloured outside KDE).
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const bool bundledPrimary = true;
#else
    const bool bundledPrimary = QIcon::themeName().isEmpty() || QIcon::fromTheme(QStringLiteral("call-start")).isNull();
#endif
    auto applyIconTheme = [bundledPrimary] {
        const QString name = isDarkScheme() ? QStringLiteral("breeze-dark") : QStringLiteral("breeze");
        QIcon::setFallbackThemeName(name);
        if (bundledPrimary)
            QIcon::setThemeName(name);
    };
    applyIconTheme();
    app.installEventFilter(new PaletteWatcher(applyIconTheme, &app));
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, applyIconTheme);
#endif
#endif
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("io.github.gongsearch.KSipPhone"), QIcon::fromTheme(QStringLiteral("call-start"))));

    if (forwardToRunningInstance(QCoreApplication::arguments()))
        return 0;

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    if (!server.listen(instanceName())) {
        QLocalServer::removeServer(instanceName()); // stale socket after a crash
        server.listen(instanceName());
    }

    PhoneApp phone;
    QObject::connect(&server, &QLocalServer::newConnection, &phone, [&server, &phone] {
        while (QLocalSocket *c = server.nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            QObject::connect(c, &QLocalSocket::readyRead, &phone, [c, &phone] {
                const QJsonArray a = QJsonDocument::fromJson(c->readAll()).array();
                QStringList args;
                for (const auto &v : a)
                    args << v.toString();
                phone.handleArguments(args, true);
            });
        }
    });
    // macOS hands URL scheme activations (tel:, sip:, callto:) to the app as FileOpen events
    struct UrlOpenFilter : QObject {
        PhoneApp *phone;
        explicit UrlOpenFilter(PhoneApp *p) : phone(p) {}
        bool eventFilter(QObject *o, QEvent *e) override
        {
            if (e->type() == QEvent::FileOpen) {
                const QUrl url = static_cast<QFileOpenEvent *>(e)->url();
                if (!url.isEmpty() && !url.isLocalFile()) {
                    phone->handleArguments({QString(), url.toString()}, true);
                    return true;
                }
            }
            return QObject::eventFilter(o, e);
        }
    } urlFilter(&phone);
    app.installEventFilter(&urlFilter);

    phone.start(parser.isSet(QStringLiteral("minimized")));
    phone.handleArguments(QCoreApplication::arguments(), false);
    return app.exec();
}
