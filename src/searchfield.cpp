// SPDX-License-Identifier: MIT
#include "searchfield.h"

#include <KLocalizedString>

#include <QAbstractItemView>
#include <QCompleter>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>

static constexpr int NumberRole = Qt::UserRole + 1;

namespace
{
// request ids are shared by all search fields (Phonebook answers everyone)
int nextRequestId()
{
    static int id = 0;
    return ++id;
}

// results: icons (source, number type) at the right end of the row, after name and number
class ResultDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->decorationPosition = QStyleOptionViewItem::Right;
        option->decorationAlignment = Qt::AlignRight | Qt::AlignVCenter;
        // like QCompleter's own delegate: the current row looks focused
        if (const auto *view = qobject_cast<const QAbstractItemView *>(option->widget); view && view->currentIndex() == index)
            option->state |= QStyle::State_HasFocus;
    }
};
}

SearchField::SearchField(Phonebook *phonebook, QWidget *parent)
    : QLineEdit(parent)
    , m_phonebook(phonebook)
{
    setPlaceholderText(i18n("Number, SIP address or name"));
    setClearButtonEnabled(true);

    m_model = new QStandardItemModel(this);
    m_completer = new QCompleter(m_model, this);
    m_completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    setCompleter(m_completer);
    m_completer->popup()->setItemDelegate(new ResultDelegate(m_completer->popup()));
    connect(m_completer, QOverload<const QModelIndex &>::of(&QCompleter::activated), this, [this](const QModelIndex &idx) {
        const QString n = idx.data(NumberRole).toString();
        QTimer::singleShot(0, this, [this, n] {
            setNumber(n);
            Q_EMIT numberChosen(n);
        });
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_requestId = nextRequestId();
        m_phonebook->search(text(), m_requestId);
    });
    connect(this, &QLineEdit::textEdited, this, [this] {
        if (!m_setting)
            m_timer->start();
    });
    // field emptied (typing, clear button, programmatically): drop old results and close the drop-down
    connect(this, &QLineEdit::textChanged, this, [this](const QString &t) {
        if (t.trimmed().isEmpty())
            clearResults();
    });
    connect(m_phonebook, &Phonebook::searchFinished, this, &SearchField::showResults);
}

void SearchField::setNumber(const QString &number)
{
    m_setting = true;
    setText(number);
    m_setting = false;
}

bool SearchField::popupVisible() const
{
    return m_completer->popup()->isVisible();
}

void SearchField::clearResults()
{
    m_timer->stop();
    m_requestId = -1; // results still on their way are outdated
    m_completer->popup()->hide();
    m_model->clear();
}

void SearchField::showResults(int requestId, const QList<PhonebookEntry> &entries)
{
    if (requestId != m_requestId)
        return; // another field's or an outdated search
    m_model->clear();
    // icons: where the entry comes from, and the kind of number
    QAbstractItemView *popup = m_completer->popup();
    const int sz = popup->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, popup);
    const qreal dpr = devicePixelRatioF();
    popup->setIconSize(QSize(2 * sz + 4, sz));
    for (const auto &e : entries) {
        const bool ldap = e.source == QLatin1String("ldap");
        const QString kindIcon = e.kind == QLatin1String("mobile") ? QStringLiteral("smartphone")
            : e.kind == QLatin1String("home")                     ? QStringLiteral("go-home")
            : e.kind == QLatin1String("work")                     ? QStringLiteral("computer")
                                                                   : QString();
        const QString kindText = e.kind == QLatin1String("mobile") ? i18n("mobile")
            : e.kind == QLatin1String("home")                     ? i18n("home")
            : e.kind == QLatin1String("work")                     ? i18n("work")
                                                                   : QString();
        QPixmap pm(QSize(2 * sz + 4, sz) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        {
            QPainter p(&pm);
            QIcon::fromTheme(ldap ? QStringLiteral("server-database") : QStringLiteral("x-office-address-book")).paint(&p, QRect(0, 0, sz, sz));
            if (!kindIcon.isEmpty())
                QIcon::fromTheme(kindIcon).paint(&p, QRect(sz + 4, 0, sz, sz));
        }
        auto *it = new QStandardItem(QIcon(pm), QStringLiteral("%1 – %2").arg(e.display.isEmpty() ? e.name : e.display, e.number));
        it->setData(e.number, NumberRole);
        const QString from = ldap ? i18n("LDAP phonebook") : i18n("address book of this computer");
        it->setToolTip(kindText.isEmpty() ? from : QStringLiteral("%1 · %2").arg(from, kindText));
        m_model->appendRow(it);
    }
    if (entries.isEmpty())
        popup->hide();
    else if (hasFocus())
        m_completer->complete();
}
