// SPDX-License-Identifier: MIT
#include "phoneapp.h"

#include <KLocalizedString>
#ifdef KSIPPHONE_WITH_KDE
#include <KAboutData>
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QFileOpenEvent>
#include <QUrl>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    KLocalizedString::setApplicationDomain("ksipphone");

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
    // Bundled Breeze subset: fallback for icons the platform theme lacks (Windows, macOS)
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));
    if (QIcon::themeName().isEmpty() || QIcon::fromTheme(QStringLiteral("call-start")).isNull())
        QIcon::setThemeName(QStringLiteral("breeze"));
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
