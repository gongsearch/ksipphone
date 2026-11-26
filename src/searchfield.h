// SPDX-License-Identifier: MIT
// Line edit for numbers, SIP addresses and names, with phonebook search (drop-down of results).
// Used by the dialer and the transfer dialog.
#pragma once

#include "phonebook.h"

#include <QLineEdit>

class QCompleter;
class QStandardItemModel;
class QTimer;

class SearchField : public QLineEdit
{
    Q_OBJECT
public:
    explicit SearchField(Phonebook *phonebook, QWidget *parent = nullptr);

    void setNumber(const QString &number); // sets the text without starting a search
    void clearResults();
    bool popupVisible() const;

Q_SIGNALS:
    void numberChosen(const QString &number); // a search result was picked (click or Enter)

private:
    void showResults(int requestId, const QList<PhonebookEntry> &entries);

    Phonebook *m_phonebook;
    QCompleter *m_completer;
    QStandardItemModel *m_model;
    QTimer *m_timer;
    int m_requestId = -1;
    bool m_setting = false;
};
