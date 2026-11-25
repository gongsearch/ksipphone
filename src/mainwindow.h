// SPDX-License-Identifier: GPL-3.0-or-later
// Compact dialer window.
#pragma once

#include "engine.h"
#include "phonebook.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QCompleter;
class QStandardItemModel;
class QTimer;

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void setRegistration(const QString &text, bool ok);
    void setCall(Engine::CallState state, const QString &peerText);
    void setNumber(const QString &number);
    void setSearchResults(int requestId, const QList<PhonebookEntry> &entries);

Q_SIGNALS:
    void dialRequested(const QString &target);
    void hangupRequested();
    void acceptRequested();
    void muteToggled(bool on);
    void holdToggled(bool on);
    void dtmf(const QString &digit);
    void searchRequested(const QString &text, int requestId);
    void settingsRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void keypad(const QString &digit);
    void callClicked();

    QLabel *m_reg, *m_status;
    QLineEdit *m_number;
    QPushButton *m_call, *m_hangup;
    QToolButton *m_mute, *m_hold;
    QCompleter *m_completer;
    QStandardItemModel *m_model;
    QTimer *m_searchTimer;
    Engine::CallState m_state = Engine::CallState::Idle;
    int m_searchId = 0;
    bool m_settingNumber = false;
};
