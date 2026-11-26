// SPDX-License-Identifier: MIT
#include "transferdialog.h"
#include "searchfield.h"

#include <KLocalizedString>

#include <QDialogButtonBox>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

TransferDialog::TransferDialog(Phonebook *phonebook, const QString &initialTarget, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(i18n("Transfer call"));
    auto *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(i18n("Transfer the call to:"), this));
    m_target = new SearchField(phonebook, this);
    m_target->setNumber(initialTarget);
    lay->addWidget(m_target);
    auto *hint = new QLabel(i18n("<b>Consult first</b>: you talk to the target while the caller waits, then connect them.<br>"
                                 "<b>Connect now</b>: the call is handed over immediately."),
                            this);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    lay->addWidget(hint);

    auto *bb = new QDialogButtonBox(this);
    auto *consult = bb->addButton(i18n("Consult first"), QDialogButtonBox::AcceptRole);
    consult->setIcon(QIcon::fromTheme(QStringLiteral("call-start")));
    auto *blind = bb->addButton(i18n("Connect now"), QDialogButtonBox::AcceptRole);
    blind->setIcon(QIcon::fromTheme(QStringLiteral("go-next")));
    bb->addButton(QDialogButtonBox::Cancel);
    consult->setDefault(true);
    // decide by the clicked button (QDialogButtonBox emits accepted() before other clicked() handlers)
    connect(bb, &QDialogButtonBox::clicked, this, [this, consult, blind](QAbstractButton *b) {
        if (b != consult && b != blind) {
            reject();
            return;
        }
        if (target().isEmpty())
            return;
        m_mode = b == consult ? Mode::Attended : Mode::Blind;
        accept();
    });
    // Return in the field picks a search result first, it must not trigger the transfer
    auto updateButtons = [this, consult, blind] {
        consult->setEnabled(!target().isEmpty());
        blind->setEnabled(!target().isEmpty());
    };
    connect(m_target, &QLineEdit::textChanged, this, updateButtons);
    updateButtons();
    lay->addWidget(bb);
    setMinimumWidth(380);
    m_target->setFocus();
}

QString TransferDialog::target() const
{
    return m_target->text().trimmed();
}
