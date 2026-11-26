// SPDX-License-Identifier: MIT
#include "mainwindow.h"
#include "searchfield.h"
#include "settingsdialog.h"

#include <QActionGroup>
#include <QApplication>
#include <QCollator>
#include <QMenu>
#include <QSettings>
#include <QStyledItemDelegate>

#include <algorithm>

#include <KLocalizedString>

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCompleter>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QPainter>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
constexpr int ExtNumberRole = Qt::UserRole;
constexpr int ExtNameRole = Qt::UserRole + 1;

// extension list: status dot and name on the left, number on the right
class ExtDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QString number = index.data(ExtNumberRole).toString();
        const QString name = index.data(ExtNameRole).toString();
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const int numW = name.isEmpty() ? 0 : opt.fontMetrics.horizontalAdvance(number) + 12;
        // the style draws background, dot and name in the left part ...
        opt.text = name.isEmpty() ? number : name;
        QStyleOptionViewItem left(opt);
        left.rect.setRight(opt.rect.right() - numW);
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, w); // full-width hover
        left.backgroundBrush = Qt::NoBrush;
        left.state &= ~QStyle::State_MouseOver;
        style->drawControl(QStyle::CE_ItemViewItem, &left, painter, w);
        // ... the number goes right-aligned in the rest
        if (numW > 0) {
            painter->save();
            painter->setFont(opt.font);
            painter->setPen(opt.palette.color(QPalette::PlaceholderText));
            painter->drawText(opt.rect.adjusted(0, 0, -6, 0), Qt::AlignRight | Qt::AlignVCenter, number);
            painter->restore();
        }
    }
};

// "Franka Mustermann" -> first "Franka", last "Mustermann"; a single word is both
QPair<QString, QString> nameParts(const QString &name)
{
    const QStringList w = name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (w.size() < 2)
        return {name, name};
    return {w.mid(0, w.size() - 1).join(QLatin1Char(' ')), w.last()};
}
}

MainWindow::MainWindow(Phonebook *phonebook, QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(i18n("KSipPhone"));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("call-start")));
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 12, 16, 16);
    lay->setSpacing(12);

    auto *top = new QHBoxLayout;
    m_reg = new QLabel(this);
    auto *settings = new QToolButton(this);
    settings->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
    settings->setToolTip(i18n("Settings"));
    settings->setAutoRaise(true);
    connect(settings, &QToolButton::clicked, this, &MainWindow::settingsRequested);
    m_vm = new QToolButton(this);
    m_vm->setIcon(QIcon::fromTheme(QStringLiteral("mail-message")));
    m_vm->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_vm->setAutoRaise(true);
    m_vm->setToolTip(i18n("Call voicemail"));
    connect(m_vm, &QToolButton::clicked, this, &MainWindow::voicemailRequested);
    top->addWidget(m_reg, 1);
    top->addWidget(m_vm);
    top->addWidget(settings);
    lay->addLayout(top);

    m_number = new SearchField(phonebook, this);
    QFont f = m_number->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_number->setFont(f);
    lay->addWidget(m_number);
    connect(m_number, &QLineEdit::returnPressed, this, &MainWindow::callClicked);

    m_pbError = new QLabel(this);
    m_pbError->setWordWrap(true);
    m_pbError->setStyleSheet(QStringLiteral("color:#c0392b"));
    m_pbError->hide();
    lay->addWidget(m_pbError);

    // peer area (only visible during a call): photo, name, state + duration + codec
    m_peerBox = new QWidget(this);
    auto *pb = new QVBoxLayout(m_peerBox);
    pb->setContentsMargins(0, 8, 0, 8);
    pb->setSpacing(6);
    m_photo = new QLabel(m_peerBox);
    m_photo->setAlignment(Qt::AlignCenter);
    m_photo->hide();
    pb->addWidget(m_photo);
    m_peerName = new QLabel(m_peerBox);
    m_peerName->setAlignment(Qt::AlignCenter);
    m_peerName->setWordWrap(true);
    m_peerName->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont pf = m_peerName->font();
    pf.setPointSizeF(pf.pointSizeF() * 1.35);
    pf.setBold(true);
    m_peerName->setFont(pf);
    pb->addWidget(m_peerName);
    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(10);
    m_status = new QLabel(m_peerBox);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    m_duration = new QLabel(m_peerBox);
    m_duration->setForegroundRole(QPalette::PlaceholderText);
    m_duration->hide();
    m_codec = new QLabel(m_peerBox);
    m_codec->hide();
    statusRow->addStretch();
    statusRow->addWidget(m_status);
    statusRow->addWidget(m_duration);
    statusRow->addWidget(m_codec);
    statusRow->addStretch();
    pb->addLayout(statusRow);
    m_peerBox->hide();
    lay->addWidget(m_peerBox);

    m_callTimer = new QTimer(this);
    m_callTimer->setInterval(1000);
    connect(m_callTimer, &QTimer::timeout, this, [this] {
        const qint64 s = (QDateTime::currentMSecsSinceEpoch() - m_callStartMs) / 1000;
        m_duration->setText(s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                                      : QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')));
    });

    auto *grid = new QGridLayout;
    grid->setSpacing(8);
    QFont kf = font();
    kf.setPointSizeF(kf.pointSizeF() * 1.25);
    const QStringList keys = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
                              QStringLiteral("5"), QStringLiteral("6"), QStringLiteral("7"), QStringLiteral("8"),
                              QStringLiteral("9"), QStringLiteral("*"), QStringLiteral("0"), QStringLiteral("#")};
    for (int i = 0; i < keys.size(); ++i) {
        auto *b = new QPushButton(keys[i], this);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(44);
        b->setMinimumWidth(80);
        b->setFont(kf);
        connect(b, &QPushButton::clicked, this, [this, k = keys[i]] { keypad(k); });
        grid->addWidget(b, i / 3, i % 3);
    }
    // internal extensions with presence status, next to the keypad: filter field + options, list
    m_extPanel = new QWidget(this);
    auto *extLay = new QVBoxLayout(m_extPanel);
    extLay->setContentsMargins(0, 0, 0, 0);
    extLay->setSpacing(6);
    auto *extTop = new QHBoxLayout;
    extTop->setSpacing(4);
    m_extFilter = new QLineEdit(m_extPanel);
    m_extFilter->setPlaceholderText(i18n("Filter extensions"));
    m_extFilter->setClearButtonEnabled(true);
    connect(m_extFilter, &QLineEdit::textChanged, this, &MainWindow::refreshExtList);
    auto *extOptions = new QToolButton(m_extPanel);
    extOptions->setIcon(QIcon::fromTheme(QStringLiteral("view-sort")));
    extOptions->setToolTip(i18n("Sort and filter"));
    extOptions->setAutoRaise(true);
    extOptions->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(extOptions);
    auto *sortGroup = new QActionGroup(menu);
    QSettings &cfg = appSettings();
    m_extSort = cfg.value(QStringLiteral("Presence/Sort"), 0).toInt();
    // 0 all, 1 online (free or busy), 2 only free; "OnlyFree" was the older on/off setting
    m_extShow = cfg.value(QStringLiteral("Presence/Show"), cfg.value(QStringLiteral("Presence/OnlyFree"), false).toBool() ? 2 : 0).toInt();
    const QStringList sorts = {i18n("Sort by number"), i18n("Sort by first name"), i18n("Sort by last name"), i18n("Sort by status")};
    for (int i = 0; i < sorts.size(); ++i) {
        QAction *a = menu->addAction(sorts[i]);
        a->setCheckable(true);
        a->setChecked(i == m_extSort);
        sortGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            m_extSort = i;
            appSettings().setValue(QStringLiteral("Presence/Sort"), i);
            refreshExtList();
        });
    }
    menu->addSeparator();
    auto *showGroup = new QActionGroup(menu);
    const QStringList shows = {i18n("Show all extensions"), i18n("Only online (free or busy)"), i18n("Only free extensions")};
    for (int i = 0; i < shows.size(); ++i) {
        QAction *a = menu->addAction(shows[i]);
        a->setCheckable(true);
        a->setChecked(i == m_extShow);
        showGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, i] {
            m_extShow = i;
            appSettings().setValue(QStringLiteral("Presence/Show"), i);
            appSettings().remove(QStringLiteral("Presence/OnlyFree"));
            refreshExtList();
        });
    }
    extOptions->setMenu(menu);
    extTop->addWidget(m_extFilter, 1);
    extTop->addWidget(extOptions);
    extLay->addLayout(extTop);
    m_ext = new QListWidget(m_extPanel);
    m_ext->setFocusPolicy(Qt::NoFocus);
    m_ext->setSelectionMode(QAbstractItemView::NoSelection);
    m_ext->setMouseTracking(true);
    m_ext->setItemDelegate(new ExtDelegate(m_ext));
    m_ext->setToolTip(i18n("Click to call; during a call click to transfer"));
    extLay->addWidget(m_ext, 1);
    m_extPanel->setMinimumWidth(250); // takes the extra width when the window is widened
    m_extPanel->hide();
    connect(m_ext, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString ext = it->data(ExtNumberRole).toString();
        if (m_state == Engine::CallState::Idle)
            Q_EMIT dialRequested(ext);
        else if (m_state == Engine::CallState::Established && m_consult == Engine::CallState::Idle)
            Q_EMIT transferRequested(ext);
    });
    auto *keyRow = new QHBoxLayout;
    keyRow->setSpacing(12);
    keyRow->addLayout(grid, 0);
    keyRow->addWidget(m_extPanel, 1);
    lay->addLayout(keyRow);

    auto *row = new QHBoxLayout;
    row->setSpacing(8);
    m_mute = new QToolButton(this);
    m_mute->setText(i18n("Mute"));
    m_mute->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_mute->setMinimumHeight(38);
    m_mute->setIcon(QIcon::fromTheme(QStringLiteral("microphone-sensitivity-muted")));
    m_mute->setToolTip(i18n("Mute microphone"));
    m_mute->setCheckable(true);
    connect(m_mute, &QToolButton::toggled, this, &MainWindow::muteToggled);
    m_hold = new QToolButton(this);
    m_hold->setText(i18n("Hold"));
    m_hold->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_hold->setMinimumHeight(38);
    m_hold->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-pause")));
    m_hold->setToolTip(i18n("Hold"));
    m_hold->setCheckable(true);
    connect(m_hold, &QToolButton::toggled, this, &MainWindow::holdToggled);
    m_transfer = new QToolButton(this);
    m_transfer->setText(i18n("Transfer"));
    m_transfer->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_transfer->setMinimumHeight(38);
    m_transfer->setIcon(QIcon::fromTheme(QStringLiteral("mail-forward")));
    m_transfer->setToolTip(i18n("Transfer the call"));
    connect(m_transfer, &QToolButton::clicked, this, [this] {
        if (m_consult != Engine::CallState::Idle)
            Q_EMIT consultCompleteRequested();
        else
            Q_EMIT transferRequested(QString());
    });
    m_call = new QPushButton(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Call"), this);
    m_hangup = new QPushButton(QIcon::fromTheme(QStringLiteral("call-stop")), i18n("Hang up"), this);
    m_call->setMinimumHeight(38);
    m_hangup->setMinimumHeight(38);
    connect(m_call, &QPushButton::clicked, this, &MainWindow::callClicked);
    connect(m_hangup, &QPushButton::clicked, this, [this] {
        if (m_consult != Engine::CallState::Idle)
            Q_EMIT consultAbortRequested();
        else
            Q_EMIT hangupRequested();
    });
    // all buttons share extra width evenly
    for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_mute, m_hold, m_transfer, m_call, m_hangup}) {
        b->setSizePolicy(QSizePolicy::Expanding, b->sizePolicy().verticalPolicy());
        row->addWidget(b, 1);
    }
    lay->addLayout(row);

    setRegistration(i18n("Not registered"), false);
    setCall(Engine::CallState::Idle, {});
    setMinimumWidth(360);
    resize(380, sizeHint().height());
}

void MainWindow::setRegistration(const QString &text, bool ok)
{
    m_reg->setText(QStringLiteral("<span style='color:%1'>●</span> %2").arg(ok ? QStringLiteral("#2e9d4c") : QStringLiteral("#c0392b"), text.toHtmlEscaped()));
}

void MainWindow::setCall(Engine::CallState state, const QString &peerText)
{
    m_state = state;
    const bool idle = state == Engine::CallState::Idle;
    const bool inCall = state == Engine::CallState::Established;
    switch (state) {
    case Engine::CallState::Idle: m_status->clear(); break;
    case Engine::CallState::Incoming: m_status->setText(i18n("Incoming call")); break;
    case Engine::CallState::Outgoing: m_status->setText(i18n("Calling …")); break;
    case Engine::CallState::Ringing: m_status->setText(i18n("Ringing …")); break;
    case Engine::CallState::Established: m_status->setText(i18n("Connected")); break;
    }
    m_peerName->setText(peerText);
    m_peerBox->setVisible(!idle);
    if (inCall && !m_callTimer->isActive()) {
        m_callStartMs = QDateTime::currentMSecsSinceEpoch();
        m_duration->setText(QStringLiteral("00:00"));
        m_duration->show();
        m_callTimer->start();
    } else if (!inCall) {
        m_callTimer->stop();
        m_duration->hide();
    }
    adjustSize();
    m_call->setEnabled(idle || state == Engine::CallState::Incoming);
    m_call->setText(state == Engine::CallState::Incoming ? i18n("Accept") : i18n("Call"));
    m_hangup->setEnabled(!idle);
    m_hangup->setText(state == Engine::CallState::Incoming ? i18n("Decline") : i18n("Hang up"));
    m_mute->setEnabled(inCall);
    m_hold->setEnabled(inCall);
    if (idle)
        m_consult = Engine::CallState::Idle;
    updateTransferButtons();
    if (idle) {
        const QSignalBlocker b1(m_mute), b2(m_hold);
        m_mute->setChecked(false);
        m_hold->setChecked(false);
    }
}

void MainWindow::setNumber(const QString &number)
{
    m_number->setNumber(number);
}

void MainWindow::setExtensions(const QList<QPair<QString, QString>> &extensions)
{
    m_extensions = extensions;
    m_extPanel->setVisible(!extensions.isEmpty());
    refreshExtList();
    adjustSize();
}

void MainWindow::setPresence(const QHash<QString, Engine::Presence> &states)
{
    m_presence = states;
    if (m_extSort == 3 || m_extShow != 0)
        refreshExtList(); // order or visibility depends on the status
    else
        updatePresenceIcons();
}

void MainWindow::refreshExtList()
{
    const QString filter = m_extFilter->text().trimmed();
    QList<QPair<QString, QString>> list;
    for (const auto &e : std::as_const(m_extensions)) {
        if (!filter.isEmpty() && !e.first.contains(filter, Qt::CaseInsensitive) && !e.second.contains(filter, Qt::CaseInsensitive))
            continue;
        const Engine::Presence st = m_presence.value(e.first, Engine::Presence::Unknown);
        if (m_extShow == 2 && st != Engine::Presence::Online)
            continue;
        if (m_extShow == 1 && st != Engine::Presence::Online && st != Engine::Presence::Busy)
            continue;
        list.append(e);
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    auto rank = [this](const QString &ext) {
        switch (m_presence.value(ext, Engine::Presence::Unknown)) {
        case Engine::Presence::Online: return 0;
        case Engine::Presence::Busy: return 1;
        case Engine::Presence::Unknown: return 2;
        case Engine::Presence::Offline: return 3;
        }
        return 3;
    };
    std::stable_sort(list.begin(), list.end(), [&](const QPair<QString, QString> &a, const QPair<QString, QString> &b) {
        const QString na = a.second.isEmpty() ? a.first : a.second, nb = b.second.isEmpty() ? b.first : b.second;
        int c = 0;
        switch (m_extSort) {
        case 1: c = collator.compare(nameParts(na).first, nameParts(nb).first); break;
        case 2: c = collator.compare(nameParts(na).second, nameParts(nb).second); break;
        case 3: c = rank(a.first) - rank(b.first); break;
        default: break;
        }
        if (c == 0)
            c = m_extSort == 0 || m_extSort == 3 ? collator.compare(a.first, b.first) : collator.compare(na, nb);
        return c < 0;
    });
    m_ext->clear();
    for (const auto &[ext, name] : std::as_const(list)) {
        auto *it = new QListWidgetItem(m_ext);
        it->setData(ExtNumberRole, ext);
        it->setData(ExtNameRole, name == ext ? QString() : name);
    }
    updatePresenceIcons();
}

void MainWindow::updatePresenceIcons()
{
    const int sz = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    const qreal dpr = devicePixelRatioF();
    auto dot = [&](const QColor &fill, bool hollow) {
        QPixmap pm(QSize(sz, sz) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r(sz * 0.22, sz * 0.22, sz * 0.56, sz * 0.56);
        if (hollow) {
            p.setPen(QPen(fill, 1.5));
            p.setBrush(Qt::NoBrush);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
        }
        p.drawEllipse(r);
        return QIcon(pm);
    };
    const QColor grey = palette().color(QPalette::PlaceholderText);
    for (int i = 0; i < m_ext->count(); ++i) {
        QListWidgetItem *it = m_ext->item(i);
        const Engine::Presence st = m_presence.value(it->data(ExtNumberRole).toString(), Engine::Presence::Unknown);
        switch (st) {
        case Engine::Presence::Online:
            it->setIcon(dot(QColor(0x2e, 0x9d, 0x4c), false));
            it->setToolTip(i18n("free"));
            break;
        case Engine::Presence::Busy:
            it->setIcon(dot(QColor(0xc0, 0x39, 0x2b), false));
            it->setToolTip(i18n("busy or ringing"));
            break;
        case Engine::Presence::Offline:
            it->setIcon(dot(grey, true));
            it->setToolTip(i18n("offline"));
            break;
        case Engine::Presence::Unknown:
            it->setIcon(dot(grey, true));
            it->setToolTip(i18n("status unknown"));
            break;
        }
    }
}

void MainWindow::setConsult(Engine::CallState state, const QString &peerText)
{
    m_consult = state;
    switch (state) {
    case Engine::CallState::Idle:
        if (m_state == Engine::CallState::Established)
            m_status->setText(i18n("Connected"));
        break;
    case Engine::CallState::Outgoing:
    case Engine::CallState::Incoming:
        m_status->setText(i18n("On hold – calling %1 …", peerText));
        break;
    case Engine::CallState::Ringing:
        m_status->setText(i18n("On hold – %1 is ringing", peerText));
        break;
    case Engine::CallState::Established:
        m_status->setText(i18n("On hold – talking to %1", peerText));
        break;
    }
    updateTransferButtons();
}

void MainWindow::updateTransferButtons()
{
    const bool consulting = m_consult != Engine::CallState::Idle;
    const bool inCall = m_state == Engine::CallState::Established;
    m_transfer->setEnabled(inCall && (!consulting || m_consult == Engine::CallState::Ringing || m_consult == Engine::CallState::Established));
    m_transfer->setText(consulting ? i18n("Connect") : i18n("Transfer"));
    m_transfer->setToolTip(consulting ? i18n("Connect the waiting caller with the consulted party") : i18n("Transfer the call"));
    m_hold->setEnabled(inCall && !consulting);
    if (m_state != Engine::CallState::Incoming)
        m_hangup->setText(consulting ? i18n("Back") : i18n("Hang up"));
    m_hangup->setToolTip(consulting ? i18n("End the consultation and return to the waiting caller") : QString());
}

void MainWindow::setPhonebookError(const QString &message)
{
    m_pbError->setText(message.isEmpty() ? QString() : i18n("Phonebook: %1", message));
    m_pbError->setVisible(!message.isEmpty());
}

void MainWindow::setPeerPhoto(const QPixmap &photo)
{
    if (photo.isNull()) {
        m_photo->clear();
        m_photo->hide();
        return;
    }
    m_photo->setPixmap(photo.scaled(96 * devicePixelRatioF(), 96 * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_photo->show();
}

void MainWindow::setVoicemail(int newMessages, bool configured)
{
    m_vm->setVisible(configured || newMessages > 0);
    m_vm->setIcon(QIcon::fromTheme(newMessages > 0 ? QStringLiteral("mail-unread-new") : QStringLiteral("mail-message")));
    m_vm->setText(newMessages > 0 ? QString::number(newMessages) : QString());
    m_vm->setToolTip(newMessages > 0 ? i18np("1 new voice message - click to listen", "%1 new voice messages - click to listen", newMessages)
                                     : i18n("Call voicemail"));
    QFont f = m_vm->font();
    f.setBold(newMessages > 0);
    m_vm->setFont(f);
}

void MainWindow::setCodec(const QString &codec)
{
    const QString u = codec.toUpper();
    if (u.isEmpty()) {
        m_codec->hide();
        return;
    }
    const bool hd = u == QLatin1String("G722") || u == QLatin1String("OPUS") || u == QLatin1String("AMR-WB") || u == QLatin1String("L16");
    const QString name = u == QLatin1String("PCMA") || u == QLatin1String("PCMU") ? QStringLiteral("G.711")
        : u == QLatin1String("G722")                                                ? QStringLiteral("G.722")
        : u == QLatin1String("OPUS")                                                ? QStringLiteral("Opus")
                                                                                    : codec;
    if (hd) {
        m_codec->setText(QStringLiteral("HD"));
        m_codec->setStyleSheet(QStringLiteral("QLabel{background:#2e9d4c;color:white;border-radius:4px;padding:1px 5px;font-weight:bold}"));
    } else {
        m_codec->setText(name);
        m_codec->setStyleSheet(QStringLiteral("QLabel{border:1px solid palette(mid);border-radius:4px;padding:1px 5px}"));
    }
    m_codec->setToolTip(i18n("Codec: %1", name));
    m_codec->show();
}

void MainWindow::keypad(const QString &digit)
{
    if (m_state == Engine::CallState::Established) {
        Q_EMIT dtmf(digit);
        return;
    }
    m_number->insert(digit);
    m_number->setFocus();
}

void MainWindow::callClicked()
{
    if (m_state == Engine::CallState::Incoming) {
        Q_EMIT acceptRequested();
        return;
    }
    if (m_state != Engine::CallState::Idle)
        return;
    if (m_number->popupVisible())
        return; // Return selects the completion first
    const QString t = m_number->text().trimmed();
    if (!t.isEmpty())
        Q_EMIT dialRequested(t);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_state != Engine::CallState::Idle) {
        Q_EMIT hangupRequested();
        return;
    }
    if (m_state == Engine::CallState::Established && !event->text().isEmpty()
        && QStringLiteral("0123456789*#").contains(event->text())) {
        Q_EMIT dtmf(event->text());
        return;
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // The window only hides; the phone keeps running in the system tray.
    hide();
    event->ignore();
}
