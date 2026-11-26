// SPDX-License-Identifier: MIT
#include "settingsdialog.h"
#include "ringer.h"
#include "codecs.h"
#include "localcontacts.h"
#include "mediacontrol.h"
#include "globalshortcuts.h"
#include "mutecontrol.h"

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
#include <QLabel>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QRegularExpression>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QVBoxLayout>

// Same file and keys as KConfig used in 0.1 (~/.config/ksipphonerc on Linux).
QSettings &appSettings()
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
    QSettings &c = appSettings();
    c.sync();
    Settings s;
    c.beginGroup(QStringLiteral("Account"));
    s.user = str(c, QStringLiteral("User"));
    s.server = str(c, QStringLiteral("Server"));
    s.transport = str(c, QStringLiteral("Transport"), QStringLiteral("udp"));
    s.displayName = str(c, QStringLiteral("DisplayName"));
    s.codecs = str(c, QStringLiteral("Codecs"), QStringLiteral("G722,PCMA,PCMU"));
    s.voicemail = str(c, QStringLiteral("Voicemail"));
    s.watched = c.value(QStringLiteral("WatchedExtensions")).toStringList();
    s.dtmfMode = str(c, QStringLiteral("DtmfMode"), QStringLiteral("rtpevent"));
    s.preferIpv6 = c.value(QStringLiteral("PreferIPv6"), false).toBool();
    s.keepAlive = c.value(QStringLiteral("KeepAlive"), true).toBool();
    c.endGroup();
    c.beginGroup(QStringLiteral("Audio"));
    s.ringLoop = c.value(QStringLiteral("RingContinuously"), false).toBool();
    s.dndReject = c.value(QStringLiteral("DndReject"), false).toBool();
    s.dndFollowSystem = c.value(QStringLiteral("DndFollowSystem"), true).toBool();
    s.pauseMedia = c.value(QStringLiteral("PauseMedia"), true).toBool();
    s.mediaKeys = c.value(QStringLiteral("MediaKeys"), true).toBool();
    s.alwaysOnTop = c.value(QStringLiteral("AlwaysOnTop"), false).toBool();
    s.muteFollowsSystem = c.value(QStringLiteral("MuteFollowsSystem"), true).toBool();
    s.pushToTalk = c.value(QStringLiteral("PushToTalk"), false).toBool();
    s.echoCancel = c.value(QStringLiteral("EchoCancellation"), false).toBool();
    s.micDevice = str(c, QStringLiteral("Microphone"));
    s.speakerDevice = str(c, QStringLiteral("Speaker"));
    s.ringDevice = str(c, QStringLiteral("RingDevice"));
    s.ringFile = str(c, QStringLiteral("RingTone"));
    c.endGroup();
    c.beginGroup(QStringLiteral("Phonebook"));
    s.ldapUri = str(c, QStringLiteral("Uri"));
    s.ldapBase = str(c, QStringLiteral("BaseDn"));
    s.useLocalContacts = c.value(QStringLiteral("UseLocalContacts"), true).toBool();
    s.nameFormat = c.value(QStringLiteral("NameFormat"), 0).toInt();
    c.endGroup();
    return s;
}

void Settings::save() const
{
    QSettings &c = appSettings();
    c.beginGroup(QStringLiteral("Account"));
    c.setValue(QStringLiteral("User"), user);
    c.setValue(QStringLiteral("Server"), server);
    c.setValue(QStringLiteral("Transport"), transport);
    c.setValue(QStringLiteral("DisplayName"), displayName);
    c.setValue(QStringLiteral("Codecs"), codecs);
    c.setValue(QStringLiteral("Voicemail"), voicemail);
    c.setValue(QStringLiteral("WatchedExtensions"), watched);
    c.setValue(QStringLiteral("DtmfMode"), dtmfMode);
    c.setValue(QStringLiteral("PreferIPv6"), preferIpv6);
    c.setValue(QStringLiteral("KeepAlive"), keepAlive);
    c.endGroup();
    c.beginGroup(QStringLiteral("Audio"));
    c.setValue(QStringLiteral("RingContinuously"), ringLoop);
    c.setValue(QStringLiteral("DndReject"), dndReject);
    c.setValue(QStringLiteral("DndFollowSystem"), dndFollowSystem);
    c.setValue(QStringLiteral("PauseMedia"), pauseMedia);
    c.setValue(QStringLiteral("MediaKeys"), mediaKeys);
    c.setValue(QStringLiteral("AlwaysOnTop"), alwaysOnTop);
    c.setValue(QStringLiteral("MuteFollowsSystem"), muteFollowsSystem);
    c.setValue(QStringLiteral("PushToTalk"), pushToTalk);
    c.setValue(QStringLiteral("EchoCancellation"), echoCancel);
    c.setValue(QStringLiteral("Microphone"), micDevice);
    c.setValue(QStringLiteral("Speaker"), speakerDevice);
    c.setValue(QStringLiteral("RingDevice"), ringDevice);
    c.setValue(QStringLiteral("RingTone"), ringFile);
    c.endGroup();
    c.beginGroup(QStringLiteral("Phonebook"));
    c.setValue(QStringLiteral("Uri"), ldapUri);
    c.setValue(QStringLiteral("BaseDn"), ldapBase);
    c.setValue(QStringLiteral("UseLocalContacts"), useLocalContacts);
    c.setValue(QStringLiteral("NameFormat"), nameFormat);
    c.endGroup();
    c.sync();
}

namespace
{
// Checkable list entries with a clear gap between check box and text. Styles place the item view check
// box differently (Windows 11 even draws it where it reports none), so the delegate draws the check box
// itself (like a normal QCheckBox), the text after it, and handles clicks/space on its own check box.
class CheckListDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        const QString text = opt.text;
        const bool checked = opt.checkState == Qt::Checked;
        // background, selection and focus only
        opt.text.clear();
        opt.features &= ~QStyleOptionViewItem::HasCheckIndicator;
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);

        QStyleOptionButton box;
        box.state = (opt.state & (QStyle::State_Enabled | QStyle::State_Active)) | (checked ? QStyle::State_On : QStyle::State_Off);
        box.palette = opt.palette;
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        QRect visible;
        const QImage img = renderBox(style, w, box, dpr, &visible);
        const QRect target = checkRect(opt, style, w, &visible);
        painter->drawImage(target.topLeft() - visible.topLeft(), img);

        QRect textRect = opt.rect.adjusted(0, 0, -4, 0);
        textRect.setLeft(target.right() + 1 + gap(style, w));
        const QPalette::ColorGroup group = !(opt.state & QStyle::State_Enabled) ? QPalette::Disabled
            : (opt.state & QStyle::State_Active)                                 ? QPalette::Active
                                                                                 : QPalette::Inactive;
        painter->save();
        painter->setFont(opt.font);
        painter->setPen(opt.palette.color(group, (opt.state & QStyle::State_Selected) ? QPalette::HighlightedText : QPalette::Text));
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine,
                          opt.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        const int boxW = style->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, w);
        const int boxH = style->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, w);
        QSize sz = QStyledItemDelegate::sizeHint(option, index);
        sz.setWidth(margin(style, w) + boxW + gap(style, w) + opt.fontMetrics.horizontalAdvance(opt.text) + 8);
        sz.setHeight(qMax(sz.height(), boxH + 6));
        return sz;
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        const Qt::ItemFlags flags = model->flags(index);
        if (!(flags & Qt::ItemIsUserCheckable) || !(flags & Qt::ItemIsEnabled))
            return false;
        const QWidget *w = option.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease: {
            auto *me = static_cast<QMouseEvent *>(event);
            // the check box plus the gap is the click target; clicks on the text select/drag as usual
            QRect hit = checkRect(option, style, w);
            hit.setLeft(option.rect.left());
            hit.setRight(hit.right() + gap(style, w) / 2);
            hit.setTop(option.rect.top());
            hit.setBottom(option.rect.bottom());
            if (me->button() != Qt::LeftButton || !hit.contains(me->position().toPoint()))
                return false;
            if (event->type() != QEvent::MouseButtonRelease)
                return true; // swallow, toggle on release
            break;
        }
        case QEvent::KeyPress: {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key != Qt::Key_Space && key != Qt::Key_Select)
                return false;
            break;
        }
        default:
            return false;
        }
        const bool checked = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        return model->setData(index, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    }

private:
    static int gap(const QStyle *style, const QWidget *w)
    {
        return qMax(10, 2 * style->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, w));
    }
    static int margin(const QStyle *style, const QWidget *w)
    {
        return style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, w) + 3;
    }
    // Draws the check box into a transparent image and finds where it actually appears: styles offset or
    // resize it inside the given rect (Windows 11), so its position is measured instead of trusted.
    static QImage renderBox(QStyle *style, const QWidget *w, QStyleOptionButton box, qreal dpr, QRect *visible)
    {
        const int bw = style->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, w);
        const int bh = style->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, w);
        const int side = 3 * qMax(bw, bh);
        QImage img(QSize(side, side) * dpr, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);
        img.fill(Qt::transparent);
        box.rect = QRect(side / 3, side / 3, bw, bh);
        {
            QPainter p(&img);
            style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &box, &p, w);
        }
        int x0 = img.width(), y0 = img.height(), x1 = -1, y1 = -1;
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) {
                if (qAlpha(line[x]) > 24) {
                    x0 = qMin(x0, x), x1 = qMax(x1, x), y0 = qMin(y0, y), y1 = qMax(y1, y);
                }
            }
        }
        *visible = x1 < 0 ? box.rect
                          : QRect(QPoint(qFloor(x0 / dpr), qFloor(y0 / dpr)), QPoint(qCeil((x1 + 1) / dpr) - 1, qCeil((y1 + 1) / dpr) - 1));
        return img;
    }
    // where the visible check box goes in the row: left margin, vertically centred
    static QRect checkRect(const QStyleOptionViewItem &opt, const QStyle *style, const QWidget *w, const QRect *visible = nullptr)
    {
        const QSize sz = visible ? visible->size()
                                 : QSize(style->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, w),
                                         style->pixelMetric(QStyle::PM_IndicatorHeight, nullptr, w));
        return QRect(QPoint(opt.rect.left() + margin(style, w), opt.rect.top() + (opt.rect.height() - sz.height()) / 2), sz);
    }
};
}

SettingsDialog::SettingsDialog(const Settings &s, const QString &password, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Settings"));
    auto *lay = new QVBoxLayout(this);
    // three tabs: SIP account, audio, phonebook
    auto *tabs = new QTabWidget(this);
    lay->addWidget(tabs);

    auto *acc = new QWidget(tabs);
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
    m_codecs->setItemDelegate(new CheckListDelegate(m_codecs));
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
    m_keepAlive = new QCheckBox(i18n("Keep the connection open for firewalls/NAT"), acc);
    m_keepAlive->setChecked(s.keepAlive);
    m_keepAlive->setToolTip(i18n("Renews the registration every 90 seconds. Firewalls and NAT routers forget idle UDP "
                                 "connections after about two minutes; without this, incoming calls and status updates "
                                 "can be dropped."));
    f->addRow(QString(), m_keepAlive);
    f->addRow(i18n("Display name:"), m_display);
    f->addRow(i18n("Password:"), m_password);
    f->addRow(i18n("Codecs:"), codecRow);
    m_voicemail = new QLineEdit(s.voicemail, acc);
    m_voicemail->setPlaceholderText(i18n("number of the mailbox, e.g. 9000"));
    f->addRow(i18n("Voicemail:"), m_voicemail);
    m_watched = new QLineEdit(s.watched.join(QStringLiteral(", ")), acc);
    m_watched->setPlaceholderText(i18n("e.g. 100, 1000, 2000"));
    m_watched->setToolTip(i18n("Internal extensions shown in a list with their status (free, busy, offline).\n"
                               "The server must offer presence for them (Asterisk: a hint per extension)."));
    f->addRow(i18n("Extensions:"), m_watched);
    m_dtmf = new QComboBox(acc);
    m_dtmf->addItem(i18n("RFC 4733 / RTP events (recommended)"), QStringLiteral("rtpevent"));
    m_dtmf->addItem(i18n("SIP INFO"), QStringLiteral("info"));
    m_dtmf->addItem(i18n("Automatic"), QStringLiteral("auto"));
    m_dtmf->setCurrentIndex(qMax(0, m_dtmf->findData(s.dtmfMode)));
    m_dtmf->setToolTip(i18n("Must match the server: e.g. Asterisk dtmf_mode=rfc4733 needs RFC 4733, dtmf_mode=info needs SIP INFO."));
    f->addRow(i18n("DTMF:"), m_dtmf);
    tabs->addTab(acc, QIcon::fromTheme(QStringLiteral("call-start")), i18n("Account"));

    auto *gen = new QWidget(tabs);
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
    m_dnd = new QComboBox(gen);
    m_dnd->addItem(i18n("Show calls silently"));
    m_dnd->addItem(i18n("Reject calls (busy)"));
    m_dnd->setCurrentIndex(s.dndReject ? 1 : 0);
    m_dnd->setToolTip(i18n("What happens with incoming calls while \"Do not disturb\" is on (tray menu or window)"));
    gf->addRow(i18n("Do not disturb:"), m_dnd);
#ifdef KSIPPHONE_HAVE_DBUS
    m_dndSystem = new QCheckBox(i18n("Also when the desktop's \"Do not disturb\" is on"), gen);
    m_dndSystem->setChecked(s.dndFollowSystem);
    gf->addRow(QString(), m_dndSystem);
#endif
    if (MediaControl::isAvailable()) {
        m_pauseMedia = new QCheckBox(i18n("Pause music and videos during calls"), gen);
        m_pauseMedia->setChecked(s.pauseMedia);
        m_pauseMedia->setToolTip(i18n("Media players (MPRIS) are paused when a call starts and resumed afterwards"));
        gf->addRow(QString(), m_pauseMedia);
        m_mediaKeys = new QCheckBox(i18n("Answer and hang up with media keys and headset buttons"), gen);
        m_mediaKeys->setChecked(s.mediaKeys);
        m_mediaKeys->setToolTip(i18n("Play/pause answers a ringing call and ends a running one"));
        gf->addRow(QString(), m_mediaKeys);
    }
    tabs->addTab(gen, QIcon::fromTheme(QStringLiteral("audio-volume-high")), i18n("Audio"));

    auto *pb = new QWidget(tabs);
    auto *g = new QFormLayout(pb);
    m_ldapUri = new QLineEdit(s.ldapUri, pb);
    m_ldapUri->setPlaceholderText(QStringLiteral("ldap://server"));
    m_ldapBase = new QLineEdit(s.ldapBase, pb);
    m_ldapBase->setPlaceholderText(QStringLiteral("ou=Phonebook,dc=example,dc=org"));
    g->addRow(i18n("URI:"), m_ldapUri);
    g->addRow(i18n("Base DN:"), m_ldapBase);
    auto *pbHint = new QLabel(i18n("Optional: an LDAP directory (read anonymously) for searching contacts and showing "
                                   "caller names and photos."), pb);
    pbHint->setWordWrap(true);
    g->insertRow(0, pbHint);
    if (const QStringList sources = LocalContacts::titles(); !sources.isEmpty()) {
        m_local = new QCheckBox(i18n("Also use the address book of this computer"), pb);
        m_local->setChecked(s.useLocalContacts);
        m_local->setToolTip(i18n("Asked after LDAP: for names and photos of callers and for the search in the dialer.\nSources: %1",
                                 sources.join(QStringLiteral(", "))));
        g->addRow(QString(), m_local);
    }
    m_nameFormat = new QComboBox(pb);
    m_nameFormat->addItem(i18n("First name Last name"), 0);
    m_nameFormat->addItem(i18n("Last name, First name"), 1);
    m_nameFormat->setCurrentIndex(qMax(0, m_nameFormat->findData(s.nameFormat)));
    m_nameFormat->setToolTip(i18n("How names are shown and sorted in the search results"));
    g->addRow(i18n("Names:"), m_nameFormat);
    tabs->addTab(pb, QIcon::fromTheme(QStringLiteral("x-office-address-book")), i18n("Phonebook"));

    // controls: window, microphone, shortcuts
    auto *ctl = new QWidget(tabs);
    // a grid instead of QFormLayout: row labels must sit level with the first line of multi-line texts
    auto *cg = new QGridLayout(ctl);
    cg->setColumnStretch(1, 1);
    int cRow = 0;
    auto addRow = [ctl, cg, &cRow](const QString &label, QWidget *field, bool multiLine = false) {
        if (!label.isEmpty()) {
            auto *l = new QLabel(label, ctl);
            cg->addWidget(l, cRow, 0, Qt::AlignRight | (multiLine ? Qt::AlignTop : Qt::AlignVCenter));
        }
        cg->addWidget(field, cRow, 1, multiLine ? Qt::AlignTop : Qt::AlignVCenter);
        ++cRow;
    };
    m_onTop = new QCheckBox(i18n("Keep above other windows"), ctl);
    m_onTop->setChecked(s.alwaysOnTop);
    addRow(i18n("Window:"), m_onTop);
    // short check box texts; the explanations wrap underneath
    auto hint = [ctl](const QString &text) {
        auto *l = new QLabel(text, ctl);
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        l->setForegroundRole(QPalette::PlaceholderText);
        // wrapped labels in a tab page don't get their height from the layout: reserve it
        l->setMinimumWidth(340);
        l->setMinimumHeight(l->heightForWidth(340));
        return l;
    };
    if (MuteControl::systemMicAvailable()) {
        m_muteSystem = new QCheckBox(i18n("Follow the system microphone mute"), ctl);
        m_muteSystem->setChecked(s.muteFollowsSystem);
        addRow(i18n("Microphone:"), m_muteSystem);
        addRow(QString(), hint(i18n("The mute button shows a microphone muted elsewhere (mic-mute key, desktop, headset) "
                                        "and mutes system-wide. Restored after the call.")));
    }
    if (GlobalShortcuts::isAvailable()) {
        m_ptt = new QCheckBox(i18n("Push to talk"), ctl);
        m_ptt->setChecked(s.pushToTalk);
        addRow(m_muteSystem ? QString() : i18n("Microphone:"), m_ptt);
        addRow(QString(), hint(i18n("During calls the microphone is only open while the push-to-talk key is held.")));
        addRow(i18n("Global:"), hint(i18n("Keys for accept, hang up, mute, push to talk, show dialer, do not disturb and "
                                              "calling the number in the clipboard: System Settings > Keyboard > Shortcuts > KSipPhone.")), true);
    }
    auto *keys = new QLabel(i18n("<table>"
                                 "<tr><td>Ctrl+Enter</td><td>Call / accept</td></tr>"
                                 "<tr><td>Esc</td><td>Hang up / decline</td></tr>"
                                 "<tr><td>Ctrl+M</td><td>Mute</td></tr>"
                                 "<tr><td>Ctrl+P</td><td>Hold</td></tr>"
                                 "<tr><td>Ctrl+T</td><td>Transfer / connect / swap</td></tr>"
                                 "<tr><td>Ctrl+F</td><td>Search</td></tr>"
                                 "<tr><td>Ctrl+E</td><td>Filter extensions</td></tr>"
                                 "<tr><td>Ctrl+L</td><td>Show / hide extension list</td></tr>"
                                 "<tr><td>Ctrl+H</td><td>Call history</td></tr>"
                                 "<tr><td>Ctrl+D</td><td>Do not disturb</td></tr>"
                                 "<tr><td>Ctrl+Shift+M</td><td>Call voicemail</td></tr>"
                                 "<tr><td>Ctrl+Shift+T</td><td>Keep window on top</td></tr>"
                                 "<tr><td>Ctrl+,</td><td>Settings</td></tr>"
                                 "<tr><td>Ctrl+W</td><td>Hide window</td></tr>"
                                 "<tr><td>Ctrl+Q</td><td>Quit</td></tr>"
                                 "</table>"),
                            ctl);
    keys->setTextFormat(Qt::RichText);
    keys->setText(keys->text()
                      .replace(QLatin1String("<table>"), QLatin1String("<table cellspacing=\"0\" cellpadding=\"0\">"))
                      .replace(QLatin1String("</td><td>"), QLatin1String("</td><td style=\"padding-left:16px\">")));
    addRow(i18n("In the window:"), keys, true);
    cg->setRowStretch(cRow, 1);
    tabs->addTab(ctl, QIcon::fromTheme(QStringLiteral("configure-shortcuts")), i18n("Controls"));

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
    resize(qMax(520, sizeHint().width()), sizeHint().height());
}

Settings SettingsDialog::settings() const
{
    Settings s;
    s.user = m_user->text().trimmed();
    s.server = m_server->text().trimmed();
    s.transport = m_transport->currentText();
    s.preferIpv6 = m_ipv6->isChecked();
    s.keepAlive = m_keepAlive->isChecked();
    s.ringLoop = m_ring->currentIndex() == 1;
    s.dndReject = m_dnd->currentIndex() == 1;
    s.dndFollowSystem = m_dndSystem ? m_dndSystem->isChecked() : s.dndFollowSystem;
    s.pauseMedia = m_pauseMedia ? m_pauseMedia->isChecked() : s.pauseMedia;
    s.mediaKeys = m_mediaKeys ? m_mediaKeys->isChecked() : s.mediaKeys;
    s.alwaysOnTop = m_onTop->isChecked();
    s.muteFollowsSystem = m_muteSystem ? m_muteSystem->isChecked() : s.muteFollowsSystem;
    s.pushToTalk = m_ptt ? m_ptt->isChecked() : s.pushToTalk;
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
    s.watched.clear();
    for (const QString &e : m_watched->text().split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        if (!s.watched.contains(e))
            s.watched << e;
    }
    s.dtmfMode = m_dtmf->currentData().toString();
    s.ldapUri = m_ldapUri->text().trimmed();
    s.ldapBase = m_ldapBase->text().trimmed();
    s.useLocalContacts = m_local ? m_local->isChecked() : true;
    s.nameFormat = m_nameFormat->currentData().toInt();
    return s;
}

QString SettingsDialog::password() const
{
    return m_password->text();
}
