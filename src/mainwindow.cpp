// SPDX-License-Identifier: MIT
#include "mainwindow.h"
#include "searchfield.h"
#include "settingsdialog.h"
#include "keepabove.h"

#include <QShortcut>
#include <QShowEvent>

#include <QActionGroup>
#include <QApplication>
#include <QCollator>
#include <QMenu>
#include <QSettings>
#include <QFrame>
#include <QMoveEvent>
#include <QResizeEvent>
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
    m_settings = settings;
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
    m_extToggle = new QToolButton(this);
    m_extToggle->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details")));
    m_extToggle->setToolTip(i18n("Show or hide the extension list"));
    m_extToggle->setAutoRaise(true);
    m_extToggle->setCheckable(true);
    m_extToggle->setChecked(appSettings().value(QStringLiteral("Window/ExtensionList"), true).toBool());
    m_extToggle->hide();
    connect(m_extToggle, &QToolButton::toggled, this, [this](bool on) {
        appSettings().setValue(QStringLiteral("Window/ExtensionList"), on);
        applyExtVisibility();
    });
    m_dnd = new QToolButton(this);
    m_dnd->setAutoRaise(true);
    m_dnd->setCheckable(true);
    connect(m_dnd, &QToolButton::toggled, this, &MainWindow::dndToggled);
    setDnd(false, false);
    auto *history = new QToolButton(this);
    m_history = history;
    history->setIcon(QIcon::fromTheme(QStringLiteral("view-history")));
    history->setToolTip(i18n("Call history"));
    history->setAutoRaise(true);
    connect(history, &QToolButton::clicked, this, &MainWindow::historyRequested);
    top->addWidget(m_reg, 1);
    top->addWidget(history);
    top->addWidget(m_dnd);
    top->addWidget(m_extToggle);
    top->addWidget(m_vm);
    top->addWidget(settings);
    lay->addLayout(top);

    m_number = new SearchField(phonebook, this);
    // picking a search result calls right away (like a click in the extension list); during a call it transfers
    connect(m_number, &SearchField::numberChosen, this, [this](const QString &n) {
        if (m_state == Engine::CallState::Idle)
            Q_EMIT dialRequested(n);
        else if (m_state == Engine::CallState::Established && m_consult == Engine::CallState::Idle)
            Q_EMIT transferRequested(n);
    });
    QFont f = m_number->font();
    f.setPointSizeF(f.pointSizeF() * 1.15);
    m_number->setFont(f);
    // body grid: row 0 search field | extension filter, row 1 call info + keypad + buttons | extension list
    auto *body = new QGridLayout;
    m_body = body;
    body->setHorizontalSpacing(12);
    body->setVerticalSpacing(12);
    auto *left = new QVBoxLayout;
    m_left = left;
    left->setSpacing(12);
    body->addWidget(m_number, 0, 0);
    body->addLayout(left, 1, 0);
    body->setColumnStretch(1, 1); // extra width goes to the extension list
    body->setRowStretch(1, 1);
    lay->addLayout(body, 1);
    connect(m_number, &SearchField::numberEntered, this, &MainWindow::callClicked); // Enter on a typed number calls

    m_pbError = new QLabel(this);
    m_pbError->setWordWrap(true);
    m_pbError->setStyleSheet(QStringLiteral("color:#c0392b"));
    m_pbError->hide();
    left->addWidget(m_pbError);

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
    left->addWidget(m_peerBox);

    // call waiting: a second caller during a call
    m_waitBox = new QFrame(this);
    m_waitBox->setFrameShape(QFrame::StyledPanel);
    auto *wb = new QVBoxLayout(m_waitBox);
    m_waitText = new QLabel(m_waitBox);
    m_waitText->setWordWrap(true);
    m_waitText->setAlignment(Qt::AlignCenter);
    wb->addWidget(m_waitText);
    auto *wbButtons = new QHBoxLayout;
    auto *waitAccept = new QPushButton(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Accept"), m_waitBox);
    waitAccept->setToolTip(i18n("Put the current call on hold and accept"));
    auto *waitReject = new QPushButton(QIcon::fromTheme(QStringLiteral("call-stop")), i18n("Decline"), m_waitBox);
    connect(waitAccept, &QPushButton::clicked, this, &MainWindow::acceptWaitingRequested);
    connect(waitReject, &QPushButton::clicked, this, &MainWindow::rejectWaitingRequested);
    wbButtons->addWidget(waitAccept);
    wbButtons->addWidget(waitReject);
    wb->addLayout(wbButtons);
    m_waitBox->hide();
    left->addWidget(m_waitBox);

    m_callTimer = new QTimer(this);
    m_callTimer->setInterval(1000);
    connect(m_callTimer, &QTimer::timeout, this, [this] {
        const qint64 s = (QDateTime::currentMSecsSinceEpoch() - m_callStartMs) / 1000;
        m_duration->setText(s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg((s / 60) % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                                      : QStringLiteral("%1:%2").arg(s / 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0')));
    });

    auto *grid = new QGridLayout;
    m_keyGrid = grid;
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
        m_keys.append(b);
        b->setFont(kf);
        connect(b, &QPushButton::clicked, this, [this, k = keys[i]] { keypad(k); });
        grid->addWidget(b, i / 3, (i % 3) * 2, 1, 2);
    }
    // internal extensions with presence status, next to the keypad: filter field + options, list
    m_extPanel = new QWidget(this); // filter field + options button
    auto *extTop = new QHBoxLayout(m_extPanel);
    extTop->setContentsMargins(0, 0, 0, 0);
    extTop->setSpacing(4);
    m_extFilter = new QLineEdit(m_extPanel);
    m_extFilter->setFont(m_number->font()); // same height as the search field
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
    m_ext = new QListWidget(this);
    m_ext->setFocusPolicy(Qt::NoFocus);
    m_ext->setSelectionMode(QAbstractItemView::NoSelection);
    m_ext->setMouseTracking(true);
    m_ext->setItemDelegate(new ExtDelegate(m_ext));
    m_ext->setToolTip(i18n("Click to call; during a call click to transfer"));
    m_ext->setMinimumWidth(250);
    m_extPanel->hide();
    m_ext->hide();
    connect(m_ext, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const QString ext = it->data(ExtNumberRole).toString();
        if (m_state == Engine::CallState::Idle)
            Q_EMIT dialRequested(ext);
        else if (m_state == Engine::CallState::Established && m_consult == Engine::CallState::Idle)
            Q_EMIT transferRequested(ext);
    });
    left->addLayout(grid);
    // while idle, reserve the room the call info (photo, name, state) takes during a call: the window
    // keeps its height when a call starts, only the keypad moves down
    m_callSpace = new QSpacerItem(0, callInfoHeight(), QSizePolicy::Minimum, QSizePolicy::Fixed);
    left->addItem(m_callSpace);
    left->addStretch(1); // the keypad stays together when the window gets taller (with the extension list)
    body->addWidget(m_extPanel, 0, 1);
    body->addWidget(m_ext, 1, 1);

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
    // buttons under the keypad: mute / hold / transfer in thirds, call / hang up in halves
    for (QAbstractButton *b : std::initializer_list<QAbstractButton *>{m_mute, m_hold, m_transfer, m_call, m_hangup})
        b->setSizePolicy(QSizePolicy::Expanding, b->sizePolicy().verticalPolicy());
    grid->setRowMinimumHeight(4, 4);
    grid->addWidget(m_mute, 5, 0, 1, 2);
    grid->addWidget(m_hold, 5, 2, 1, 2);
    grid->addWidget(m_transfer, 5, 4, 1, 2);
    grid->addWidget(m_call, 6, 0, 1, 3);
    grid->addWidget(m_hangup, 6, 3, 1, 3);
    equalizeKeypad();

    setRegistration(i18n("Not registered"), false);
    setCall(Engine::CallState::Idle, {});
    // no explicit minimum: the layout's minimum (keypad, extension list) applies to width and height
    resize(380, sizeHint().height());
    setupShortcuts();
    // saved window size and position (Wayland ignores the position)
    if (const QByteArray g = appSettings().value(QStringLiteral("Window/Geometry")).toByteArray(); !g.isEmpty())
        restoreGeometry(g);
    m_wideSize = size(); // the list (if configured) appears later and keeps this size
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(800);
    connect(m_saveTimer, &QTimer::timeout, this, &MainWindow::saveGeometryNow);
}

void MainWindow::saveGeometryNow()
{
    if (m_state != Engine::CallState::Idle)
        return; // the window may be enlarged for the call info
    appSettings().setValue(QStringLiteral("Window/Geometry"), saveGeometry());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (isVisible() && m_saveTimer)
        m_saveTimer->start();
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    if (isVisible() && m_saveTimer)
        m_saveTimer->start();
}

void MainWindow::equalizeKeypad()
{
    // every widget in the 6-column grid (keys span 2, mute/hold/transfer 2, call/hang up 3) needs
    // span * column + (span - 1) * spacing; the widest need sets one minimum for all columns
    const int sp = m_keyGrid->horizontalSpacing();
    int col = 0;
    for (int i = 0; i < m_keyGrid->count(); ++i) {
        QWidget *w = m_keyGrid->itemAt(i)->widget();
        if (!w)
            continue;
        int row, column, rowSpan, colSpan;
        m_keyGrid->getItemPosition(i, &row, &column, &rowSpan, &colSpan);
        const int need = std::max(w->sizeHint().width(), w->minimumWidth());
        col = std::max(col, (need - (colSpan - 1) * sp + colSpan - 1) / colSpan);
    }
    for (int c = 0; c < 6; ++c) {
        m_keyGrid->setColumnMinimumWidth(c, col);
        m_keyGrid->setColumnStretch(c, 1);
    }
}

void MainWindow::applyExtVisibility()
{
    const bool configured = !m_extensions.isEmpty();
    const bool show = configured && m_extToggle->isChecked();
    m_extToggle->setVisible(configured);
    const bool changed = show != m_ext->isVisible();
    if (changed && !show && m_ext->isVisible())
        m_wideSize = size(); // remember for showing it again
    m_extPanel->setVisible(show);
    m_ext->setVisible(show);
    // with the list: compact keypad, the list takes extra space; without: keypad and buttons fill the window
    m_body->setColumnStretch(0, show ? 0 : 1);
    m_body->setColumnStretch(1, show ? 1 : 0);
    m_left->setStretch(m_left->count() - 1, show ? 1 : 0);
    for (QPushButton *k : std::as_const(m_keys))
        k->setSizePolicy(k->sizePolicy().horizontalPolicy(), show ? QSizePolicy::Fixed : QSizePolicy::Expanding);
    if (!changed)
        return;
    // hiding: shrink to the keypad; showing: back to the earlier size
    QTimer::singleShot(0, this, [this, show] {
        const QSize minS = minimumSizeHint();
        resize(show ? m_wideSize.expandedTo(minS) : minS);
    });
}

void MainWindow::setRegistration(const QString &text, bool ok)
{
    m_reg->setText(QStringLiteral("<span style='color:%1'>●</span> %2").arg(ok ? QStringLiteral("#2e9d4c") : QStringLiteral("#c0392b"), text.toHtmlEscaped()));
}

void MainWindow::learnCallInfoHeight()
{
    // measured during a call: if the call info needed more room than reserved, reserve that from now on
    if (m_state == Engine::CallState::Idle || !m_peerBox->isVisible())
        return;
    int needed = m_peerBox->sizeHint().height() + 12;
    if (m_waitBox->isVisible())
        needed += m_waitBox->sizeHint().height() + 12;
    if (needed > callInfoHeight()) {
        appSettings().setValue(QStringLiteral("Window/CallInfoHeight"), needed);
        m_learnedCallInfo = needed;
    }
}

int MainWindow::callInfoHeight() const
{
    if (m_learnedCallInfo < 0)
        m_learnedCallInfo = appSettings().value(QStringLiteral("Window/CallInfoHeight"), 0).toInt();
    // peer box: margins 8+8, photo 96, name (1.35x bold, up to two lines), state row; plus the column spacing
    QFont nf = font();
    nf.setPointSizeF(nf.pointSizeF() * 1.35);
    nf.setBold(true);
    const int name = QFontMetrics(nf).lineSpacing() * 2;
    const int stateRow = qMax(fontMetrics().height() + 6, 22);
    return qMax(8 + 96 + 6 + name + 6 + stateRow + 8 + 12, m_learnedCallInfo);
}

void MainWindow::setCall(Engine::CallState state, const QString &peerText)
{
    const bool wasIdle = m_state == Engine::CallState::Idle;
    m_state = state;
    QTimer::singleShot(0, this, &MainWindow::equalizeKeypad); // button texts may change below
    // reserved room <-> call info
    m_callSpace->changeSize(0, state == Engine::CallState::Idle ? callInfoHeight() : 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_left->invalidate();
    if (state != Engine::CallState::Idle)
        QTimer::singleShot(0, this, &MainWindow::learnCallInfoHeight);
    // after a call the number / search text is not needed any more
    if (state == Engine::CallState::Idle && !wasIdle)
        m_number->clear();
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
    // grow if the call info needs room; afterwards back to the size the user had
    if (!idle && m_heightBeforeCall < 0)
        m_heightBeforeCall = height();
    QTimer::singleShot(0, this, [this, idle] {
        const int minH = minimumSizeHint().height();
        if (idle && m_heightBeforeCall >= 0) {
            resize(width(), qMax(minH, m_heightBeforeCall));
            m_heightBeforeCall = -1;
        } else if (height() < minH) {
            resize(width(), minH);
        }
    });
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
    applyExtVisibility();
    refreshExtList();
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

void MainWindow::setDnd(bool on, bool bySystem)
{
    const QSignalBlocker b(m_dnd);
    m_dnd->setChecked(on);
    m_dnd->setIcon(QIcon::fromTheme(on || bySystem ? QStringLiteral("notifications-disabled") : QStringLiteral("notifications")));
    m_dnd->setToolTip(on         ? i18n("Do not disturb is on – click to turn it off")
                          : bySystem ? i18n("The desktop's do not disturb is on – calls are not rung")
                                     : i18n("Do not disturb"));
}

void MainWindow::setWaiting(const QString &peerText)
{
    QTimer::singleShot(0, this, &MainWindow::learnCallInfoHeight);
    m_waitText->setText(i18n("<b>Call waiting:</b> %1", peerText.toHtmlEscaped()));
    m_waitBox->setVisible(!peerText.isEmpty());
}

void MainWindow::setConsult(Engine::CallState state, const QString &peerText, bool waitingCall)
{
    m_consult = state;
    m_secondIsWaiting = waitingCall;
    QTimer::singleShot(0, this, &MainWindow::equalizeKeypad);
    if (waitingCall && state != Engine::CallState::Idle) {
        m_status->setText(state == Engine::CallState::Established ? i18n("Talking to %1 – the other call is on hold", peerText)
                                                                  : i18n("Accepting %1 …", peerText));
        updateTransferButtons();
        return;
    }
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
    m_hold->setEnabled(inCall && !consulting);
    if (consulting && m_secondIsWaiting) {
        // two calls after accepting a waiting call
        m_transfer->setEnabled(inCall && m_consult == Engine::CallState::Established);
        m_transfer->setText(i18n("Swap"));
        m_transfer->setToolTip(i18n("Switch to the call on hold"));
        m_hangup->setText(i18n("Hang up"));
        m_hangup->setToolTip(i18n("End this call and return to the one on hold"));
        return;
    }
    m_transfer->setEnabled(inCall && (!consulting || m_consult == Engine::CallState::Ringing || m_consult == Engine::CallState::Established));
    m_transfer->setText(consulting ? i18n("Connect") : i18n("Transfer"));
    m_transfer->setToolTip(consulting ? i18n("Connect the waiting caller with the consulted party") : i18n("Transfer the call"));
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
    // 96 logical pixels at any display scale (without the ratio set it was shown 1.5x / 2x larger)
    const qreal dpr = devicePixelRatioF();
    QPixmap scaled = photo.scaled(qRound(96 * dpr), qRound(96 * dpr), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    m_photo->setPixmap(scaled);
    QTimer::singleShot(0, this, &MainWindow::learnCallInfoHeight);
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
    m_number->clearResults();
    const QString t = m_number->text().trimmed();
    if (!t.isEmpty())
        Q_EMIT dialRequested(t);
}

void MainWindow::setMuted(bool muted)
{
    const QSignalBlocker b(m_mute);
    m_mute->setChecked(muted);
}

void MainWindow::setAlwaysOnTop(bool on)
{
    m_alwaysOnTop = on;
    KeepAbove::apply(this, on);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_alwaysOnTop) // KWin needs the window mapped first
        QTimer::singleShot(150, this, [this] { KeepAbove::apply(this, m_alwaysOnTop); });
}

void MainWindow::setupShortcuts()
{
    auto add = [this](const QKeySequence &key, auto fn, QWidget *tipWidget = nullptr) {
        auto *sc = new QShortcut(key, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, fn);
        if (tipWidget) {
            const QString tip = tipWidget->toolTip().isEmpty() ? tipWidget->property("text").toString().remove(QLatin1Char('&')) : tipWidget->toolTip();
            tipWidget->setToolTip(QStringLiteral("%1 (%2)").arg(tip, key.toString(QKeySequence::NativeText)));
        }
    };
    add(QKeySequence(Qt::CTRL | Qt::Key_Return), [this] { callClicked(); }, m_call);
    add(QKeySequence(Qt::CTRL | Qt::Key_Enter), [this] { callClicked(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_M), [this] { if (m_mute->isEnabled()) m_mute->toggle(); }, m_mute);
    add(QKeySequence(Qt::CTRL | Qt::Key_P), [this] { if (m_hold->isEnabled()) m_hold->toggle(); }, m_hold);
    add(QKeySequence(Qt::CTRL | Qt::Key_T), [this] { if (m_transfer->isEnabled()) m_transfer->click(); }, m_transfer);
    add(QKeySequence(Qt::CTRL | Qt::Key_F), [this] { m_number->setFocus(); m_number->selectAll(); }, m_number);
    add(QKeySequence(Qt::CTRL | Qt::Key_E), [this] { if (m_extFilter->isVisible()) { m_extFilter->setFocus(); m_extFilter->selectAll(); } }, m_extFilter);
    add(QKeySequence(Qt::CTRL | Qt::Key_L), [this] { if (m_extToggle->isVisible()) m_extToggle->toggle(); }, m_extToggle);
    add(QKeySequence(Qt::CTRL | Qt::Key_H), [this] { Q_EMIT historyRequested(); }, m_history);
    add(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { m_dnd->toggle(); });
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), [this] { Q_EMIT voicemailRequested(); }, m_vm);
    add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T), [this] { Q_EMIT alwaysOnTopToggleRequested(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_Comma), [this] { Q_EMIT settingsRequested(); }, m_settings);
    add(QKeySequence(Qt::CTRL | Qt::Key_W), [this] { hide(); });
    add(QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { Q_EMIT quitRequested(); });
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
    saveGeometryNow();
    // The window only hides; the phone keeps running in the system tray.
    hide();
    event->ignore();
}
