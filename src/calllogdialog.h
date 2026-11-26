// SPDX-License-Identifier: MIT
// Window with the call history: filter, call back (double-click), copy number, delete entries.
#pragma once

#include <QDialog>

class CallLog;
class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

class CallLogDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CallLogDialog(CallLog *log, QWidget *parent = nullptr);

Q_SIGNALS:
    void callRequested(const QString &number);

private:
    void refresh();
    void contextMenu(const QPoint &pos);
    int entryIndex(const QTreeWidgetItem *item) const;

    CallLog *m_log;
    QLineEdit *m_filter;
    QComboBox *m_kind;
    QTreeWidget *m_list;
};
