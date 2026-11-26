// SPDX-License-Identifier: MIT
#include "calllogdialog.h"
#include "calllog.h"
#include "settingsdialog.h"

#include <KLocalizedString>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

static constexpr int IndexRole = Qt::UserRole;

CallLogDialog::CallLogDialog(CallLog *log, QWidget *parent)
    : QDialog(parent)
    , m_log(log)
{
    setWindowTitle(i18n("Call history"));
    auto *lay = new QVBoxLayout(this);
    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(i18n("Filter by name or number"));
    m_filter->setClearButtonEnabled(true);
    m_kind = new QComboBox(this);
    m_kind->addItem(i18n("All calls"));
    m_kind->addItem(QIcon::fromTheme(QStringLiteral("call-missed")), i18n("Missed"));
    m_kind->addItem(QIcon::fromTheme(QStringLiteral("call-incoming")), i18n("Incoming"));
    m_kind->addItem(QIcon::fromTheme(QStringLiteral("call-outgoing")), i18n("Outgoing"));
    auto *clear = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-clear-history")), i18n("Clear list"), this);
    top->addWidget(m_filter, 1);
    top->addWidget(m_kind);
    top->addWidget(clear);
    lay->addLayout(top);

    m_list = new QTreeWidget(this);
    m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true);
    m_list->setHeaderLabels({i18n("Name"), i18n("Number"), i18n("Time"), i18n("Duration")});
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 4; ++c)
        m_list->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setToolTip(i18n("Double-click to call back"));
    lay->addWidget(m_list, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &CallLogDialog::refresh);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &CallLogDialog::refresh);
    connect(m_log, &CallLog::changed, this, &CallLogDialog::refresh);
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const int i = entryIndex(it);
        if (i >= 0)
            Q_EMIT callRequested(m_log->entries().at(i).number);
    });
    connect(m_list, &QWidget::customContextMenuRequested, this, &CallLogDialog::contextMenu);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, i18n("Clear list"), i18n("Delete the whole call history?")) == QMessageBox::Yes)
            m_log->clear();
    });

    if (const QByteArray g = appSettings().value(QStringLiteral("Window/CallHistory")).toByteArray(); !g.isEmpty())
        restoreGeometry(g);
    else
        resize(560, 420);
    connect(this, &QDialog::finished, this, [this] { appSettings().setValue(QStringLiteral("Window/CallHistory"), saveGeometry()); });
    refresh();
}

int CallLogDialog::entryIndex(const QTreeWidgetItem *item) const
{
    return item ? item->data(0, IndexRole).toInt() : -1;
}

void CallLogDialog::refresh()
{
    m_list->clear();
    const QString filter = m_filter->text().trimmed();
    const int kind = m_kind->currentIndex();
    const QList<CallLog::Entry> &entries = m_log->entries();
    const QLocale loc;
    for (int i = 0; i < entries.size(); ++i) {
        const CallLog::Entry &e = entries.at(i);
        if (kind == 1 && e.direction != CallLog::Direction::Missed)
            continue;
        if (kind == 2 && e.direction != CallLog::Direction::Incoming)
            continue;
        if (kind == 3 && e.direction != CallLog::Direction::Outgoing)
            continue;
        if (!filter.isEmpty() && !e.name.contains(filter, Qt::CaseInsensitive) && !e.number.contains(filter))
            continue;
        auto *it = new QTreeWidgetItem(m_list);
        it->setData(0, IndexRole, i);
        const QString icon = e.direction == CallLog::Direction::Missed ? QStringLiteral("call-missed")
            : e.direction == CallLog::Direction::Outgoing              ? QStringLiteral("call-outgoing")
                                                                        : QStringLiteral("call-incoming");
        it->setIcon(0, QIcon::fromTheme(icon));
        it->setText(0, e.name.isEmpty() ? e.number : e.name);
        it->setText(1, e.number);
        const QDateTime t = e.time.toLocalTime();
        it->setText(2, t.date() == QDate::currentDate() ? loc.toString(t.time(), QLocale::ShortFormat)
                                                          : loc.toString(t, QLocale::ShortFormat));
        if (e.elsewhere)
            it->setText(3, i18n("answered elsewhere"));
        else if (e.seconds > 0)
            it->setText(3, e.seconds >= 3600 ? QStringLiteral("%1:%2:%3").arg(e.seconds / 3600).arg((e.seconds / 60) % 60, 2, 10, QLatin1Char('0')).arg(e.seconds % 60, 2, 10, QLatin1Char('0'))
                                             : QStringLiteral("%1:%2").arg(e.seconds / 60).arg(e.seconds % 60, 2, 10, QLatin1Char('0')));
        it->setTextAlignment(3, Qt::AlignRight | Qt::AlignVCenter);
        if (e.direction == CallLog::Direction::Missed)
            it->setForeground(0, QColor(0xc0, 0x39, 0x2b));
    }
}

void CallLogDialog::contextMenu(const QPoint &pos)
{
    QTreeWidgetItem *it = m_list->itemAt(pos);
    const int i = entryIndex(it);
    if (i < 0)
        return;
    const QString number = m_log->entries().at(i).number;
    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("call-start")), i18n("Call"), this, [this, number] { Q_EMIT callRequested(number); });
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18n("Copy number"), this,
                   [number] { QApplication::clipboard()->setText(number); });
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18n("Delete entry"), this, [this, i] { m_log->remove(i); });
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}
