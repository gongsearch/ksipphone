// SPDX-License-Identifier: MIT
// One address book of the desktop/OS (KPeople, Evolution Data Server, macOS Contacts).
#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QImage;

struct LocalPerson {
    QString name;
    QString given, family; // if known
    QStringList numbers;   // as stored
    QStringList kinds;     // per number: "mobile", "home", "work" or ""
    QByteArray photo;      // PNG, empty if the contact has no real photo
};

class ContactSource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    QList<LocalPerson> people() const { return m_people; }

    static QByteArray photoPng(const QImage &img); // scaled down to 256 px

Q_SIGNALS:
    void changed();

protected:
    void setPeople(const QList<LocalPerson> &people)
    {
        m_people = people;
        Q_EMIT changed();
    }

private:
    QList<LocalPerson> m_people;
};

// backends; each returns nullptr where it is not compiled in
ContactSource *createKPeopleSource(QObject *parent);
ContactSource *createEdsSource(QObject *parent);
bool edsAvailable(); // libebook can be loaded
ContactSource *createMacContactsSource(QObject *parent);
