// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include <KLocalizedString>

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QCompleter>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

static constexpr int NumberRole = Qt::UserRole + 1;

MainWindow::MainWindow(QWidget *parent)
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

    m_number = new QLineEdit(this);
    m_number->setPlaceholderText(i18n("Number, SIP address or name"));
    m_number->setClearButtonEnabled(true);
    QFont f = m_number->font();
    f.setPointSizeF(f.pointSizeF() * 1.4);
    m_number->setFont(f);
    lay->addWidget(m_number);

    m_model = new QStandardItemModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_number->setCompleter(m_completer);
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this, [this](const QModelIndex &idx) {
        const QString n = idx.data(NumberRole).toString();
        QTimer::singleShot(0, this, [this, n] { setNumber(n); });
    });

    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(250);
    connect(m_searchTimer, &QTimer::timeout, this, [this] { Q_EMIT searchRequested(m_number->text(), ++m_searchId); });
    connect(m_number, &QLineEdit::textEdited, this, [this] {
        if (!m_settingNumber)
            m_searchTimer->start();
    });
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
        b->setFont(kf);
        connect(b, &QPushButton::clicked, this, [this, k = keys[i]] { keypad(k); });
        grid->addWidget(b, i / 3, i % 3);
    }
    lay->addLayout(grid);

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
    m_call = new QPushButton(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Call"), this);
    m_hangup = new QPushButton(QIcon::fromTheme(QStringLiteral("call-stop")), i18n("Hang up"), this);
    m_call->setMinimumHeight(38);
    m_hangup->setMinimumHeight(38);
    connect(m_call, &QPushButton::clicked, this, &MainWindow::callClicked);
    connect(m_hangup, &QPushButton::clicked, this, &MainWindow::hangupRequested);
    row->addWidget(m_mute);
    row->addWidget(m_hold);
    row->addWidget(m_call, 1);
    row->addWidget(m_hangup, 1);
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
    if (idle) {
        const QSignalBlocker b1(m_mute), b2(m_hold);
        m_mute->setChecked(false);
        m_hold->setChecked(false);
    }
}

void MainWindow::setNumber(const QString &number)
{
    m_settingNumber = true;
    m_number->setText(number);
    m_settingNumber = false;
}

void MainWindow::setSearchResults(int requestId, const QList<PhonebookEntry> &entries)
{
    if (requestId != m_searchId)
        return; // outdated
    m_model->clear();
    for (const auto &e : entries) {
        const QString kind = e.kind == QLatin1String("local") ? i18n("address book")
            : e.kind == QLatin1String("mobile")                ? i18n("mobile")
            : e.kind == QLatin1String("homePhone")              ? i18n("home")
                                                                : i18n("work");
        auto *it = new QStandardItem(QStringLiteral("%1 – %2 (%3)").arg(e.name, e.number, kind));
        it->setData(e.number, NumberRole);
        m_model->appendRow(it);
    }
    if (!entries.isEmpty() && m_number->hasFocus())
        m_completer->complete();
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
    if (m_completer->popup()->isVisible())
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
