// SPDX-License-Identifier: MIT
#include "settingsdialog.h"
#include "ringer.h"
#include "codecs.h"

#include <KLocalizedString>

#include <QCheckBox>
#include <algorithm>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QToolButton>
#include <QVBoxLayout>

// Same file and keys as KConfig used in 0.1 (~/.config/ksipphonerc on Linux).
static QSettings &store()
{
    static QSettings settings(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/ksipphonerc"),
                              QSettings::IniFormat);
    return settings;
}

// KConfig wrote comma separated values unquoted; QSettings reads those as a list.
static QString str(const QSettings &s, const QString &key, const QString &def = {})
{
    const QVariant v = s.value(key, def);
    if (v.metaType().id() == QMetaType::QStringList)
        return v.toStringList().join(QLatin1Char(','));
    return v.toString();
}

Settings Settings::load()
{
    QSettings &c = store();
    c.sync();
    Settings s;
    c.beginGroup(QStringLiteral("Account"));
    s.user = str(c, QStringLiteral("User"));
    s.server = str(c, QStringLiteral("Server"));
    s.transport = str(c, QStringLiteral("Transport"), QStringLiteral("udp"));
    s.displayName = str(c, QStringLiteral("DisplayName"));
    s.codecs = str(c, QStringLiteral("Codecs"), QStringLiteral("G722,PCMA,PCMU"));
    s.voicemail = str(c, QStringLiteral("Voicemail"));
    s.dtmfMode = str(c, QStringLiteral("DtmfMode"), QStringLiteral("rtpevent"));
    s.preferIpv6 = c.value(QStringLiteral("PreferIPv6"), false).toBool();
    c.endGroup();
    c.beginGroup(QStringLiteral("Audio"));
    s.ringLoop = c.value(QStringLiteral("RingContinuously"), false).toBool();
    s.echoCancel = c.value(QStringLiteral("EchoCancellation"), false).toBool();
    s.micDevice = str(c, QStringLiteral("Microphone"));
    s.speakerDevice = str(c, QStringLiteral("Speaker"));
    s.ringDevice = str(c, QStringLiteral("RingDevice"));
    s.ringFile = str(c, QStringLiteral("RingTone"));
    c.endGroup();
    c.beginGroup(QStringLiteral("Phonebook"));
    s.ldapUri = str(c, QStringLiteral("Uri"));
    s.ldapBase = str(c, QStringLiteral("BaseDn"));
    c.endGroup();
    return s;
}

void Settings::save() const
{
    QSettings &c = store();
    c.beginGroup(QStringLiteral("Account"));
    c.setValue(QStringLiteral("User"), user);
    c.setValue(QStringLiteral("Server"), server);
    c.setValue(QStringLiteral("Transport"), transport);
    c.setValue(QStringLiteral("DisplayName"), displayName);
    c.setValue(QStringLiteral("Codecs"), codecs);
    c.setValue(QStringLiteral("Voicemail"), voicemail);
    c.setValue(QStringLiteral("DtmfMode"), dtmfMode);
    c.setValue(QStringLiteral("PreferIPv6"), preferIpv6);
    c.endGroup();
    c.beginGroup(QStringLiteral("Audio"));
    c.setValue(QStringLiteral("RingContinuously"), ringLoop);
    c.setValue(QStringLiteral("EchoCancellation"), echoCancel);
    c.setValue(QStringLiteral("Microphone"), micDevice);
    c.setValue(QStringLiteral("Speaker"), speakerDevice);
    c.setValue(QStringLiteral("RingDevice"), ringDevice);
    c.setValue(QStringLiteral("RingTone"), ringFile);
    c.endGroup();
    c.beginGroup(QStringLiteral("Phonebook"));
    c.setValue(QStringLiteral("Uri"), ldapUri);
    c.setValue(QStringLiteral("BaseDn"), ldapBase);
    c.endGroup();
    c.sync();
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
    // Codecs: tick to offer, order = preference (drag & drop or the arrow buttons)
    const QList<CodecInfo> known = Codecs::catalogue();
    m_codecs = new QListWidget(acc);
    m_codecs->setDragDropMode(QAbstractItemView::InternalMove);
    m_codecs->setDefaultDropAction(Qt::MoveAction);
    m_codecs->setToolTip(i18n("Tick the codecs to offer. The order is the preference: drag the entries or use the arrows."));
    QStringList enabled = s.codecs.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &c : enabled)
        c = c.trimmed();
    auto addItem = [this](const QString &id, const QString &label, bool on, bool available, const QString &hint) {
        auto *it = new QListWidgetItem(label, m_codecs);
        it->setData(Qt::UserRole, id);
        it->setToolTip(hint);
        Qt::ItemFlags fl = Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled;
        if (available)
            fl |= Qt::ItemIsEnabled;
        else
            it->setToolTip(hint + QLatin1Char('\n') + i18n("Not available in this baresip build"));
        it->setFlags(fl);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    };
    // enabled ones first in their configured order (unknown ids are kept), then the remaining catalogue
    for (const QString &id : std::as_const(enabled)) {
        const CodecInfo *k = Codecs::find(id);
        addItem(k ? k->id : id, k ? k->label : id, true, !k || Codecs::available(*k), k ? k->hint : QString());
    }
    for (const CodecInfo &k : known) {
        const bool listed = std::any_of(enabled.begin(), enabled.end(), [&](const QString &e) { return e.compare(k.id, Qt::CaseInsensitive) == 0; });
        if (!listed)
            addItem(k.id, Codecs::available(k) ? k.label : i18n("%1 (not available)", k.label), false, Codecs::available(k), k.hint);
    }
    // show every entry without scrolling
    m_codecs->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_codecs->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    int h = 2 * m_codecs->frameWidth();
    for (int i = 0; i < m_codecs->count(); ++i)
        h += m_codecs->sizeHintForRow(i);
    m_codecs->setFixedHeight(h + 2);
    auto *up = new QToolButton(acc);
    up->setIcon(QIcon::fromTheme(QStringLiteral("go-up")));
    up->setToolTip(i18n("Prefer"));
    auto *down = new QToolButton(acc);
    down->setIcon(QIcon::fromTheme(QStringLiteral("go-down")));
    down->setToolTip(i18n("Less preferred"));
    auto move = [this](int delta) {
        const int r = m_codecs->currentRow();
        if (r < 0 || r + delta < 0 || r + delta >= m_codecs->count())
            return;
        QListWidgetItem *it = m_codecs->takeItem(r);
        m_codecs->insertItem(r + delta, it);
        m_codecs->setCurrentRow(r + delta);
    };
    connect(up, &QToolButton::clicked, this, [move] { move(-1); });
    connect(down, &QToolButton::clicked, this, [move] { move(1); });
    auto *codecRow = new QHBoxLayout;
    auto *arrows = new QVBoxLayout;
    arrows->addWidget(up);
    arrows->addWidget(down);
    arrows->addStretch();
    codecRow->addWidget(m_codecs, 1);
    codecRow->addLayout(arrows);
    f->addRow(i18n("User:"), m_user);
    f->addRow(i18n("Server:"), m_server);
    f->addRow(i18n("Transport:"), m_transport);
    f->addRow(QString(), m_ipv6);
    f->addRow(i18n("Display name:"), m_display);
    f->addRow(i18n("Password:"), m_password);
    f->addRow(i18n("Codecs:"), codecRow);
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
    const auto sinks = audioDevices(false);
    m_mic = new QComboBox(gen);
    fill(m_mic, audioDevices(true), s.micDevice);
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
    QStringList codecs;
    for (int i = 0; i < m_codecs->count(); ++i) {
        if (m_codecs->item(i)->checkState() == Qt::Checked)
            codecs << m_codecs->item(i)->data(Qt::UserRole).toString();
    }
    s.codecs = codecs.join(QLatin1Char(','));
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
