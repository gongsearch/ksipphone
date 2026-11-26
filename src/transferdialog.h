// SPDX-License-Identifier: MIT
// Choose the target of a call transfer: connect right away (blind) or consult the target first.
#pragma once

#include <QDialog>

class Phonebook;
class SearchField;

class TransferDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Blind, Attended };
    TransferDialog(Phonebook *phonebook, const QString &initialTarget, QWidget *parent = nullptr);

    QString target() const;
    Mode mode() const { return m_mode; }

private:
    SearchField *m_target;
    Mode m_mode = Mode::Attended;
};
