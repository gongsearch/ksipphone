// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only LDAP phonebook (inetOrgPerson: cn, telephoneNumber, mobile, homePhone).
#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

struct PhonebookEntry {
    QString name;
    QString number;
    QString kind; // "telephoneNumber", "mobile", "homePhone"
};

class Phonebook : public QObject
{
    Q_OBJECT
public:
    explicit Phonebook(QObject *parent = nullptr);

    void configure(const QString &uri, const QString &baseDn);
    bool isConfigured() const { return !m_uri.isEmpty() && !m_base.isEmpty(); }

    // Asynchronous; results are delivered via the signals.
    void search(const QString &text, int requestId);
    void lookupNumber(const QString &number, int requestId);

    static QString normalize(const QString &number); // "0171.." -> "+49171..", "0049.." -> "+49.."

Q_SIGNALS:
    void searchFinished(int requestId, const QList<PhonebookEntry> &entries);
    void lookupFinished(int requestId, const QString &name);

private:
    static QList<PhonebookEntry> query(const QString &uri, const QString &base, const QString &filter, int limit);
    QString m_uri, m_base;
};
