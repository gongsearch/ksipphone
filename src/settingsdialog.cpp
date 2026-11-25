// SPDX-License-Identifier: GPL-3.0-or-later
#include "settingsdialog.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QVBoxLayout>

Settings Settings::load()
{
    const auto cfg = KSharedConfig::openConfig();
    const KConfigGroup a(cfg, QStringLiteral("Account"));
    const KConfigGroup p(cfg, QStringLiteral("Phonebook"));
    Settings s;
    s.user = a.readEntry("User", QString());
    s.server = a.readEntry("Server", QString());
    s.transport = a.readEntry("Transport", QStringLiteral("udp"));
    s.displayName = a.readEntry("DisplayName", QString());
    s.codecs = a.readEntry("Codecs", QStringLiteral("G722,PCMA,PCMU"));
    s.preferIpv6 = a.readEntry("PreferIPv6", false);
    s.ringLoop = KConfigGroup(cfg, QStringLiteral("General")).readEntry("RingContinuously", false);
    s.ldapUri = p.readEntry("Uri", QString());
    s.ldapBase = p.readEntry("BaseDn", QString());
    return s;
}

void Settings::save() const
{
    auto cfg = KSharedConfig::openConfig();
    KConfigGroup a(cfg, QStringLiteral("Account"));
    a.writeEntry("User", user);
    a.writeEntry("Server", server);
    a.writeEntry("Transport", transport);
    a.writeEntry("DisplayName", displayName);
    a.writeEntry("Codecs", codecs);
    a.writeEntry("PreferIPv6", preferIpv6);
    KConfigGroup(cfg, QStringLiteral("General")).writeEntry("RingContinuously", ringLoop);
    KConfigGroup p(cfg, QStringLiteral("Phonebook"));
    p.writeEntry("Uri", ldapUri);
    p.writeEntry("BaseDn", ldapBase);
    cfg->sync();
}

SettingsDialog::SettingsDialog(const Settings &s, const QString &password, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Settings"));
    auto *lay = new QVBoxLayout(this);

    auto *acc = new QGroupBox(i18n("SIP account"), this);
    auto *f = new QFormLayout(acc);
    m_user = new QLineEdit(s.user, acc);
    m_server = new QLineEdit(s.server, acc);
    m_server->setPlaceholderText(i18n("host or host:port"));
    m_transport = new QComboBox(acc);
    m_transport->addItems({QStringLiteral("udp"), QStringLiteral("tcp"), QStringLiteral("tls")});
    m_transport->setCurrentText(s.transport);
    m_ipv6 = new QCheckBox(i18n("Prefer IPv6 if the server has an IPv6 address"), acc);
    m_ipv6->setChecked(s.preferIpv6);
    m_display = new QLineEdit(s.displayName, acc);
    m_password = new QLineEdit(acc);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(password.isEmpty() ? i18n("not set") : i18n("stored in KWallet - type to change"));
    connect(m_password, &QLineEdit::textEdited, this, [this] { m_pwChanged = true; });
    m_codecs = new QLineEdit(s.codecs, acc);
    m_codecs->setToolTip(i18n("Comma separated, in order of preference, e.g. G722,PCMA,PCMU,opus"));
    f->addRow(i18n("User:"), m_user);
    f->addRow(i18n("Server:"), m_server);
    f->addRow(i18n("Transport:"), m_transport);
    f->addRow(QString(), m_ipv6);
    f->addRow(i18n("Display name:"), m_display);
    f->addRow(i18n("Password:"), m_password);
    f->addRow(i18n("Codecs:"), m_codecs);
    lay->addWidget(acc);

    auto *gen = new QGroupBox(i18n("Incoming calls"), this);
    auto *gf = new QFormLayout(gen);
    m_ring = new QComboBox(gen);
    m_ring->addItem(i18n("Once (notification sound, set in System Settings → Notifications)"));
    m_ring->addItem(i18n("Continuously until answered"));
    m_ring->setCurrentIndex(s.ringLoop ? 1 : 0);
    gf->addRow(i18n("Ring:"), m_ring);
    lay->addWidget(gen);

    auto *pb = new QGroupBox(i18n("LDAP phonebook (optional, anonymous read)"), this);
    auto *g = new QFormLayout(pb);
    m_ldapUri = new QLineEdit(s.ldapUri, pb);
    m_ldapUri->setPlaceholderText(QStringLiteral("ldap://server"));
    m_ldapBase = new QLineEdit(s.ldapBase, pb);
    m_ldapBase->setPlaceholderText(QStringLiteral("ou=Phonebook,dc=example,dc=org"));
    g->addRow(i18n("URI:"), m_ldapUri);
    g->addRow(i18n("Base DN:"), m_ldapBase);
    lay->addWidget(pb);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    resize(460, sizeHint().height());
}

Settings SettingsDialog::settings() const
{
    Settings s;
    s.user = m_user->text().trimmed();
    s.server = m_server->text().trimmed();
    s.transport = m_transport->currentText();
    s.preferIpv6 = m_ipv6->isChecked();
    s.ringLoop = m_ring->currentIndex() == 1;
    s.displayName = m_display->text().trimmed();
    s.codecs = m_codecs->text().remove(QLatin1Char(' '));
    s.ldapUri = m_ldapUri->text().trimmed();
    s.ldapBase = m_ldapBase->text().trimmed();
    return s;
}

QString SettingsDialog::password() const
{
    return m_password->text();
}
