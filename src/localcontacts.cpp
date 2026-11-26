// SPDX-License-Identifier: MIT
#include "localcontacts.h"

#include <KLocalizedString>

#include <QBuffer>
#include <QSet>
#include <QImage>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(LOCALCONTACTS, "ksipphone.contacts")

QByteArray ContactSource::photoPng(const QImage &img)
{
    QByteArray out;
    if (img.isNull())
        return out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    const QImage scaled = img.width() > 256 || img.height() > 256
        ? img.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : img;
    scaled.save(&buf, "PNG");
    return out;
}

#ifndef KSIPPHONE_WITH_KPEOPLE
ContactSource *createKPeopleSource(QObject *)
{
    return nullptr;
}
#endif
#ifndef KSIPPHONE_WITH_EDS
ContactSource *createEdsSource(QObject *)
{
    return nullptr;
}
bool edsAvailable()
{
    return false;
}
#endif
#ifndef KSIPPHONE_WITH_MACCONTACTS
ContactSource *createMacContactsSource(QObject *)
{
    return nullptr;
}
#endif

LocalContacts::LocalContacts(QObject *parent)
    : QObject(parent)
{
}

LocalContacts::~LocalContacts() = default;

bool LocalContacts::isAvailable()
{
#if defined(KSIPPHONE_WITH_KPEOPLE) || defined(KSIPPHONE_WITH_EDS) || defined(KSIPPHONE_WITH_MACCONTACTS)
    return true;
#else
    return false;
#endif
}

QStringList LocalContacts::titles()
{
    QStringList out;
#ifdef KSIPPHONE_WITH_KPEOPLE
    out << i18n("KDE contacts (incl. phone contacts via KDE Connect)");
#endif
    if (edsAvailable())
        out << i18n("GNOME / Evolution contacts");
#ifdef KSIPPHONE_WITH_MACCONTACTS
    out << i18n("macOS Contacts");
#endif
    return out;
}

void LocalContacts::setEnabled(bool on)
{
    if (on == m_enabled)
        return;
    m_enabled = on;
    qDeleteAll(m_sources);
    m_sources.clear();
    m_people.clear();
    m_byNumber.clear();
    if (!on)
        return;
    // order = priority when two address books know the same number
    for (auto create : {&createKPeopleSource, &createEdsSource, &createMacContactsSource}) {
        if (ContactSource *s = create(this)) {
            connect(s, &ContactSource::changed, this, &LocalContacts::rebuild);
            m_sources.append(s);
        }
    }
}

void LocalContacts::rebuild()
{
    m_people.clear();
    m_byNumber.clear();
    for (const ContactSource *src : std::as_const(m_sources)) {
        for (const LocalPerson &lp : src->people()) {
            Person p{lp.name, lp.numbers, {}, lp.photo};
            for (const QString &n : std::as_const(p.numbers))
                p.normalized << Phonebook::normalize(n);
            const int idx = m_people.size();
            for (const QString &n : std::as_const(p.normalized)) {
                if (n.size() >= 3 && !m_byNumber.contains(n))
                    m_byNumber.insert(n, idx);
            }
            m_people.append(p);
        }
    }
    qCDebug(LOCALCONTACTS) << "address book:" << m_people.size() << "people with numbers from" << m_sources.size() << "source(s)";
}

QList<PhonebookEntry> LocalContacts::search(const QString &text, int limit) const
{
    QList<PhonebookEntry> out;
    const QString q = text.trimmed();
    if (!m_enabled || q.size() < 2)
        return out;
    QString digits;
    for (const QChar c : q)
        if (c.isDigit())
            digits += c;
    const bool byNumber = digits.size() >= 3 && digits.size() * 2 >= q.size();
    const QString tail = digits.startsWith(QLatin1Char('0')) ? digits.mid(1) : digits;
    QSet<QString> seen;
    for (const Person &p : m_people) {
        const bool nameMatch = p.name.contains(q, Qt::CaseInsensitive);
        for (int i = 0; i < p.numbers.size(); ++i) {
            if ((nameMatch || (byNumber && p.normalized[i].contains(tail))) && !seen.contains(p.normalized[i])) {
                seen.insert(p.normalized[i]);
                out.append({p.name, p.numbers[i], QStringLiteral("local"), {}});
                if (out.size() >= limit)
                    return out;
            }
        }
    }
    return out;
}

bool LocalContacts::lookup(const QString &number, QString *name, QByteArray *photo) const
{
    if (!m_enabled)
        return false;
    const auto it = m_byNumber.constFind(Phonebook::normalize(number));
    if (it == m_byNumber.constEnd())
        return false;
    const Person &p = m_people.at(*it);
    *name = p.name;
    *photo = p.photo;
    return true;
}
