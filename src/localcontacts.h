// SPDX-License-Identifier: MIT
// The address books of the desktop / OS (see contactsource.h), merged into one number index.
// KDE: KPeople; GNOME: Evolution Data Server; macOS: Contacts. Windows: none.
#pragma once

#include "contactsource.h"
#include "phonebook.h"

#include <QHash>
#include <QObject>

class LocalContacts : public QObject
{
    Q_OBJECT
public:
    explicit LocalContacts(QObject *parent = nullptr);
    ~LocalContacts() override;

    static bool isAvailable();   // compiled with at least one backend
    static QStringList titles(); // backends that would be used here (for the settings)
    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

    // synchronous, local and fast
    QList<PhonebookEntry> search(const QString &text, int limit = 25) const;
    bool lookup(const QString &number, QString *name, QByteArray *photo) const;

private:
    void rebuild();

    struct Person {
        QString name, given, family;
        QStringList kinds;
        QStringList numbers;    // as stored
        QStringList normalized; // Phonebook::normalize()
        QByteArray photo;
    };
    QList<Person> m_people;
    QHash<QString, int> m_byNumber; // normalized number -> index in m_people
    QList<ContactSource *> m_sources;
    bool m_enabled = false;
};
