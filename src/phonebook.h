// SPDX-License-Identifier: MIT
// Read-only LDAP phonebook (inetOrgPerson: cn, telephoneNumber, mobile, homePhone).
#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

struct PhonebookEntry;
struct PhonebookResult;

struct PhonebookEntry {
    QString name;
    QString number;
    QString kind;     // "work", "mobile", "home" or empty (unknown)
    QByteArray photo; // jpegPhoto, only filled by lookupNumber()
    QString given, family; // name parts if known (for "Last, First")
    QString source;        // "ldap" or "local"
    QString display;       // name as shown, set by Phonebook according to the name format
};

struct PhonebookResult {
    QList<PhonebookEntry> entries;
    QString error; // empty on success
};

class Phonebook : public QObject
{
    Q_OBJECT
public:
    explicit Phonebook(QObject *parent = nullptr);

    void configure(const QString &uri, const QString &baseDn);
    bool isConfigured() const { return !m_uri.isEmpty() && !m_base.isEmpty(); } // LDAP
    // desktop address book (KPeople), asked after LDAP
    void setLocalContacts(class LocalContacts *local) { m_local = local; }
    enum class NameFormat { FirstLast = 0, LastFirst = 1 };
    void setNameFormat(NameFormat f) { m_nameFormat = f; }
    static QString displayName(const PhonebookEntry &e, NameFormat f);

    // Asynchronous; results are delivered via the signals.
    void search(const QString &text, int requestId);
    void lookupNumber(const QString &number, int requestId);

    static QString normalize(const QString &number); // "0171.." -> "+49171..", "0049.." -> "+49.."

Q_SIGNALS:
    void searchFinished(int requestId, const QList<PhonebookEntry> &entries);
    void lookupFinished(int requestId, const QString &name, const QByteArray &photo);
    void errorOccurred(const QString &message); // empty message = OK again

private:
    static PhonebookResult query(const QString &uri, const QString &base, const QString &filter, int limit, bool withPhoto);
    QString m_uri, m_base;
    class LocalContacts *m_local = nullptr;
    NameFormat m_nameFormat = NameFormat::FirstLast;
    QList<PhonebookEntry> withLocal(const QList<PhonebookEntry> &ldap, const QString &text) const;
};
