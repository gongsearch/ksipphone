// SPDX-License-Identifier: GPL-3.0-or-later
#include "phonebook.h"

#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent>

#include <ldap.h>

Q_LOGGING_CATEGORY(PHONEBOOK, "ksipphone.phonebook")

static const char *const kAttrs[] = {"cn", "telephoneNumber", "mobile", "homePhone", nullptr};

static QString escapeFilter(const QString &in)
{
    QString out;
    for (const QChar c : in) {
        switch (c.unicode()) {
        case '*': out += QLatin1String("\\2a"); break;
        case '(': out += QLatin1String("\\28"); break;
        case ')': out += QLatin1String("\\29"); break;
        case '\\': out += QLatin1String("\\5c"); break;
        case 0: break;
        default: out += c;
        }
    }
    return out;
}

Phonebook::Phonebook(QObject *parent)
    : QObject(parent)
{
}

void Phonebook::configure(const QString &uri, const QString &baseDn)
{
    m_uri = uri;
    m_base = baseDn;
}

QString Phonebook::normalize(const QString &number)
{
    QString n;
    for (const QChar c : number) {
        if (c.isDigit() || c == QLatin1Char('+'))
            n += c;
    }
    if (n.startsWith(QLatin1String("00")))
        n = QLatin1Char('+') + n.mid(2);
    else if (n.startsWith(QLatin1Char('0')))
        n = QLatin1String("+49") + n.mid(1);
    return n;
}

QList<PhonebookEntry> Phonebook::query(const QString &uri, const QString &base, const QString &filter, int limit)
{
    QList<PhonebookEntry> result;
    LDAP *ld = nullptr;
    if (ldap_initialize(&ld, uri.toUtf8().constData()) != LDAP_SUCCESS || !ld) {
        qCWarning(PHONEBOOK) << "ldap_initialize failed" << uri;
        return result;
    }
    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);
    timeval tv{3, 0};
    ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);

    berval cred{0, nullptr};
    int rc = ldap_sasl_bind_s(ld, nullptr, LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr); // anonymous
    if (rc != LDAP_SUCCESS) {
        qCWarning(PHONEBOOK) << "bind failed" << ldap_err2string(rc);
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return result;
    }
    LDAPMessage *res = nullptr;
    rc = ldap_search_ext_s(ld, base.toUtf8().constData(), LDAP_SCOPE_SUBTREE, filter.toUtf8().constData(),
                           const_cast<char **>(kAttrs), 0, nullptr, nullptr, &tv, limit, &res);
    if (rc != LDAP_SUCCESS && rc != LDAP_SIZELIMIT_EXCEEDED) {
        qCWarning(PHONEBOOK) << "search failed" << ldap_err2string(rc);
    }
    for (LDAPMessage *e = res ? ldap_first_entry(ld, res) : nullptr; e; e = ldap_next_entry(ld, e)) {
        QString name;
        if (berval **v = ldap_get_values_len(ld, e, "cn")) {
            if (v[0])
                name = QString::fromUtf8(v[0]->bv_val, int(v[0]->bv_len));
            ldap_value_free_len(v);
        }
        for (const char *attr : {"telephoneNumber", "mobile", "homePhone"}) {
            berval **v = ldap_get_values_len(ld, e, attr);
            if (!v)
                continue;
            for (int i = 0; v[i]; ++i)
                result.append({name, QString::fromUtf8(v[i]->bv_val, int(v[i]->bv_len)), QString::fromLatin1(attr)});
            ldap_value_free_len(v);
        }
    }
    if (res)
        ldap_msgfree(res);
    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return result;
}

void Phonebook::search(const QString &text, int requestId)
{
    const QString q = text.trimmed();
    if (!isConfigured() || q.size() < 2) {
        Q_EMIT searchFinished(requestId, {});
        return;
    }
    const QString e = escapeFilter(q);
    QString filter = QStringLiteral("(&(objectClass=inetOrgPerson)(|(cn=*%1*)(sn=%1*)(givenName=%1*)").arg(e);
    // allow searching by (part of) a number, too
    QString digits;
    for (const QChar c : q)
        if (c.isDigit())
            digits += c;
    if (digits.size() >= 3 && digits.size() * 2 >= q.size()) {
        const QString tail = digits.startsWith(QLatin1Char('0')) ? digits.mid(1) : digits;
        filter += QStringLiteral("(telephoneNumber=*%1*)(mobile=*%1*)(homePhone=*%1*)").arg(escapeFilter(tail));
    }
    filter += QStringLiteral("))");

    QPointer<Phonebook> self(this);
    auto *w = new QFutureWatcher<QList<PhonebookEntry>>(this);
    connect(w, &QFutureWatcher<QList<PhonebookEntry>>::finished, this, [self, w, requestId] {
        if (self)
            Q_EMIT self->searchFinished(requestId, w->result());
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(&Phonebook::query, m_uri, m_base, filter, 25));
}

void Phonebook::lookupNumber(const QString &number, int requestId)
{
    const QString n = normalize(number);
    if (!isConfigured() || n.size() < 3) {
        Q_EMIT lookupFinished(requestId, {});
        return;
    }
    const QString e = escapeFilter(n);
    const QString filter = QStringLiteral("(&(objectClass=inetOrgPerson)(|(telephoneNumber=%1)(mobile=%1)(homePhone=%1)))").arg(e);
    QPointer<Phonebook> self(this);
    auto *w = new QFutureWatcher<QList<PhonebookEntry>>(this);
    connect(w, &QFutureWatcher<QList<PhonebookEntry>>::finished, this, [self, w, requestId] {
        const auto r = w->result();
        if (self)
            Q_EMIT self->lookupFinished(requestId, r.isEmpty() ? QString() : r.first().name);
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(&Phonebook::query, m_uri, m_base, filter, 1));
}
