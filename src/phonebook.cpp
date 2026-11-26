// SPDX-License-Identifier: MIT
#include "phonebook.h"
#include "localcontacts.h"

#include <QSet>

#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QPointer>
#include <QtConcurrent>

#include <ldap.h>

Q_LOGGING_CATEGORY(PHONEBOOK, "ksipphone.phonebook")

static const char *const kAttrs[] = {"cn", "telephoneNumber", "mobile", "homePhone", nullptr};
static const char *const kAttrsPhoto[] = {"cn", "telephoneNumber", "mobile", "homePhone", "jpegPhoto", nullptr};

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

PhonebookResult Phonebook::query(const QString &uri, const QString &base, const QString &filter, int limit, bool withPhoto)
{
    PhonebookResult result;
    LDAP *ld = nullptr;
    if (ldap_initialize(&ld, uri.toUtf8().constData()) != LDAP_SUCCESS || !ld) {
        qCWarning(PHONEBOOK) << "ldap_initialize failed" << uri;
        result.error = QStringLiteral("invalid LDAP URI %1").arg(uri);
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
        result.error = QString::fromUtf8(ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return result;
    }
    LDAPMessage *res = nullptr;
    rc = ldap_search_ext_s(ld, base.toUtf8().constData(), LDAP_SCOPE_SUBTREE, filter.toUtf8().constData(),
                           const_cast<char **>(withPhoto ? kAttrsPhoto : kAttrs), 0, nullptr, nullptr, &tv, limit, &res);
    if (rc != LDAP_SUCCESS && rc != LDAP_SIZELIMIT_EXCEEDED) {
        qCWarning(PHONEBOOK) << "search failed" << ldap_err2string(rc);
        result.error = QString::fromUtf8(ldap_err2string(rc));
    }
    for (LDAPMessage *e = res ? ldap_first_entry(ld, res) : nullptr; e; e = ldap_next_entry(ld, e)) {
        QString name;
        if (berval **v = ldap_get_values_len(ld, e, "cn")) {
            if (v[0])
                name = QString::fromUtf8(v[0]->bv_val, int(v[0]->bv_len));
            ldap_value_free_len(v);
        }
        QByteArray photo;
        if (withPhoto) {
            if (berval **v = ldap_get_values_len(ld, e, "jpegPhoto")) {
                if (v[0])
                    photo = QByteArray(v[0]->bv_val, int(v[0]->bv_len));
                ldap_value_free_len(v);
            }
        }
        for (const char *attr : {"telephoneNumber", "mobile", "homePhone"}) {
            berval **v = ldap_get_values_len(ld, e, attr);
            if (!v)
                continue;
            for (int i = 0; v[i]; ++i)
                result.entries.append({name, QString::fromUtf8(v[i]->bv_val, int(v[i]->bv_len)), QString::fromLatin1(attr), photo});
            ldap_value_free_len(v);
        }
    }
    if (res)
        ldap_msgfree(res);
    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return result;
}

QList<PhonebookEntry> Phonebook::withLocal(const QList<PhonebookEntry> &ldap, const QString &text) const
{
    // LDAP first; local address book entries whose number LDAP did not return already
    QList<PhonebookEntry> out = ldap;
    if (!m_local)
        return out;
    QSet<QString> seen;
    for (const PhonebookEntry &e : ldap)
        seen.insert(normalize(e.number));
    for (const PhonebookEntry &e : m_local->search(text)) {
        if (!seen.contains(normalize(e.number))) {
            seen.insert(normalize(e.number));
            out.append(e);
        }
    }
    return out;
}

void Phonebook::search(const QString &text, int requestId)
{
    const QString q = text.trimmed();
    if (q.size() < 2) {
        Q_EMIT searchFinished(requestId, {});
        return;
    }
    if (!isConfigured()) {
        Q_EMIT searchFinished(requestId, withLocal({}, q));
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
    auto *w = new QFutureWatcher<PhonebookResult>(this);
    connect(w, &QFutureWatcher<PhonebookResult>::finished, this, [self, w, requestId, q] {
        if (self) {
            const PhonebookResult r = w->result();
            Q_EMIT self->errorOccurred(r.error);
            Q_EMIT self->searchFinished(requestId, self->withLocal(r.entries, q));
        }
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(&Phonebook::query, m_uri, m_base, filter, 25, false));
}

void Phonebook::lookupNumber(const QString &number, int requestId)
{
    const QString n = normalize(number);
    if (n.size() < 3) {
        Q_EMIT lookupFinished(requestId, {}, {});
        return;
    }
    if (!isConfigured()) {
        QString name;
        QByteArray photo;
        if (m_local)
            m_local->lookup(number, &name, &photo);
        Q_EMIT lookupFinished(requestId, name, photo);
        return;
    }
    const QString e = escapeFilter(n);
    const QString filter = QStringLiteral("(&(objectClass=inetOrgPerson)(|(telephoneNumber=%1)(mobile=%1)(homePhone=%1)))").arg(e);
    QPointer<Phonebook> self(this);
    auto *w = new QFutureWatcher<PhonebookResult>(this);
    connect(w, &QFutureWatcher<PhonebookResult>::finished, this, [self, w, requestId, number] {
        const PhonebookResult r = w->result();
        if (self) {
            Q_EMIT self->errorOccurred(r.error);
            QString name = r.entries.isEmpty() ? QString() : r.entries.first().name;
            QByteArray photo = r.entries.isEmpty() ? QByteArray() : r.entries.first().photo;
            // not in LDAP (or no photo there): ask the desktop address book
            if (self->m_local && (name.isEmpty() || photo.isEmpty())) {
                QString lname;
                QByteArray lphoto;
                if (self->m_local->lookup(number, &lname, &lphoto)) {
                    if (name.isEmpty()) {
                        name = lname;
                        photo = lphoto;
                    } else if (photo.isEmpty() && lname.compare(name, Qt::CaseInsensitive) == 0) {
                        photo = lphoto; // same person: borrow the photo
                    }
                }
            }
            Q_EMIT self->lookupFinished(requestId, name, photo);
        }
        w->deleteLater();
    });
    w->setFuture(QtConcurrent::run(&Phonebook::query, m_uri, m_base, filter, 1, true));
}
