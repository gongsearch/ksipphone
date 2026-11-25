// SPDX-License-Identifier: MIT
#include "settingsdialog.h"
#include "ringer.h"

#include <KConfigGroup>
#include <KLocalizedString>
#include <KSharedConfig>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
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
    s.voicemail = a.readEntry("Voicemail", QString());
    s.dtmfMode = a.readEntry("DtmfMode", QStringLiteral("rtpevent"));
    s.preferIpv6 = a.readEntry("PreferIPv6", false);
    const KConfigGroup au(cfg, QStringLiteral("Audio"));
    s.ringLoop = au.readEntry("RingContinuously", false);
    s.echoCancel = au.readEntry("EchoCancellation", false);
    s.micDevice = au.readEntry("Microphone", QString());
    s.speakerDevice = au.readEntry("Speaker", QString());
    s.ringDevice = au.readEntry("RingDevice", QString());
    s.ringFile = au.readEntry("RingTone", QString());
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
    a.writeEntry("Voicemail", voicemail);
    a.writeEntry("DtmfMode", dtmfMode);
    a.writeEntry("PreferIPv6", preferIpv6);
    KConfigGroup au(cfg, QStringLiteral("Audio"));
    au.writeEntry("RingContinuously", ringLoop);
    au.writeEntry("EchoCancellation", echoCancel);
    au.writeEntry("Microphone", micDevice);
    au.writeEntry("Speaker", speakerDevice);
    au.writeEntry("RingDevice", ringDevice);
    au.writeEntry("RingTone", ringFile);
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
    m_voicemail = new QLineEdit(s.voicemail, acc);
    m_voicemail->setPlaceholderText(i18n("number of the mailbox, e.g. 9000"));
    f->addRow(i18n("Voicemail:"), m_voicemail);
    m_dtmf = new QComboBox(acc);
    m_dtmf->addItem(i18n("RFC 4733 / RTP events (recommended)"), QStringLiteral("rtpevent"));
    m_dtmf->addItem(i18n("SIP INFO"), QStringLiteral("info"));
    m_dtmf->addItem(i18n("Automatic"), QStringLiteral("auto"));
    m_dtmf->setCurrentIndex(qMax(0, m_dtmf->findData(s.dtmfMode)));
    m_dtmf->setToolTip(i18n("Must match the server: e.g. Asterisk dtmf_mode=rfc4733 needs RFC 4733, dtmf_mode=info needs SIP INFO."));
    f->addRow(i18n("DTMF:"), m_dtmf);
    lay->addWidget(acc);

    auto *gen = new QGroupBox(i18n("Audio"), this);
    auto *gf = new QFormLayout(gen);
    auto fill = [](QComboBox *c, const QList<AudioNode> &nodes, const QString &cur) {
        c->addItem(i18n("System default"), QString());
        for (const AudioNode &n : nodes)
            c->addItem(n.description, n.name);
        int i = c->findData(cur);
        if (i < 0 && !cur.isEmpty()) { // configured device currently not present
            c->addItem(i18n("%1 (not connected)", cur), cur);
            i = c->count() - 1;
        }
        c->setCurrentIndex(qMax(0, i));
    };
    const auto sinks = pipewireNodes(false);
    m_mic = new QComboBox(gen);
    fill(m_mic, pipewireNodes(true), s.micDevice);
    m_speaker = new QComboBox(gen);
    fill(m_speaker, sinks, s.speakerDevice);
    m_ringDev = new QComboBox(gen);
    fill(m_ringDev, sinks, s.ringDevice);
    m_ringFile = new QLineEdit(s.ringFile, gen);
    m_ringFile->setPlaceholderText(Ringer::defaultFile());
    auto *browse = new QPushButton(QIcon::fromTheme(QStringLiteral("document-open")), QString(), gen);
    browse->setToolTip(i18n("Choose ring tone"));
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString start = m_ringFile->text().isEmpty() ? QStringLiteral("/usr/share/sounds") : m_ringFile->text();
        const QString f = QFileDialog::getOpenFileName(this, i18n("Ring tone"), start, i18n("Sounds (*.wav *.ogg *.oga *.mp3 *.flac *.opus)"));
        if (!f.isEmpty())
            m_ringFile->setText(f);
    });
    m_preview = new Ringer(this);
    auto *test = new QPushButton(QIcon::fromTheme(QStringLiteral("media-playback-start")), i18n("Test"), gen);
    connect(test, &QPushButton::clicked, this, [this] {
        m_preview->preview(m_ringFile->text().trimmed(), m_ringDev->currentData().toString());
    });
    auto *fileRow = new QHBoxLayout;
    fileRow->addWidget(m_ringFile, 1);
    fileRow->addWidget(browse);
    fileRow->addWidget(test);
    m_ring = new QComboBox(gen);
    m_ring->addItem(i18n("Once"));
    m_ring->addItem(i18n("Continuously until answered"));
    m_ring->setCurrentIndex(s.ringLoop ? 1 : 0);
    gf->addRow(i18n("Microphone:"), m_mic);
    gf->addRow(i18n("Speaker / headset:"), m_speaker);
    m_aec = new QCheckBox(i18n("Echo cancellation in KSipPhone (WebRTC)"), gen);
    m_aec->setChecked(s.echoCancel);
    m_aec->setToolTip(i18n("Leave off if the microphone already comes through PipeWire's echo cancellation "
                           "(module-echo-cancel / an \"Echo Cancellation Source\"), otherwise the echo is cancelled twice."));
    gf->addRow(QString(), m_aec);
    gf->addRow(i18n("Ring on:"), m_ringDev);
    gf->addRow(i18n("Ring tone:"), fileRow);
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
    s.echoCancel = m_aec->isChecked();
    s.micDevice = m_mic->currentData().toString();
    s.speakerDevice = m_speaker->currentData().toString();
    s.ringDevice = m_ringDev->currentData().toString();
    s.ringFile = m_ringFile->text().trimmed();
    s.displayName = m_display->text().trimmed();
    s.codecs = m_codecs->text().remove(QLatin1Char(' '));
    s.voicemail = m_voicemail->text().trimmed();
    s.dtmfMode = m_dtmf->currentData().toString();
    s.ldapUri = m_ldapUri->text().trimmed();
    s.ldapBase = m_ldapBase->text().trimmed();
    return s;
}

QString SettingsDialog::password() const
{
    return m_password->text();
}
