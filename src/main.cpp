// SPDX-License-Identifier: GPL-3.0-or-later
#include "phoneapp.h"

#include <KAboutData>
#include <KDBusService>
#include <KLocalizedString>

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    KLocalizedString::setApplicationDomain("ksipphone");

    KAboutData about(QStringLiteral("ksipphone"), i18n("KSipPhone"), QStringLiteral(KSIPPHONE_VERSION),
                     i18n("A small SIP phone for KDE Plasma"), KAboutLicense::GPL_V3);
    about.setOrganizationDomain("ksipphone.org");
    about.setDesktopFileName(QStringLiteral("org.ksipphone.KSipPhone"));
    KAboutData::setApplicationData(about);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("call-start")));

    QCommandLineParser parser;
    parser.addOption({QStringLiteral("minimized"), i18n("Start hidden in the system tray")});
    parser.addPositionalArgument(QStringLiteral("uri"), i18n("tel: or sip: URI to call"), QStringLiteral("[uri]"));
    about.setupCommandLine(&parser);
    parser.process(app);
    about.processCommandLine(&parser);

    KDBusService service(KDBusService::Unique);

    PhoneApp phone;
    QObject::connect(&service, &KDBusService::activateRequested, &phone, [&phone](const QStringList &args, const QString &) {
        phone.handleArguments(args, true);
    });
    phone.start(parser.isSet(QStringLiteral("minimized")));
    phone.handleArguments(QCoreApplication::arguments(), false);
    return app.exec();
}
