// SPDX-License-Identifier: MIT
// macOS: the system Contacts (Contacts app, iCloud, CardDAV, Exchange accounts). Compiled with ARC.
#include "contactsource.h"


#include <QCoreApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QPointer>

#import <Contacts/Contacts.h>
#import <Foundation/Foundation.h>

Q_LOGGING_CATEGORY(MACCONTACTS, "ksipphone.contacts.mac")

namespace
{
class MacContactsSource : public ContactSource
{
public:
    explicit MacContactsSource(QObject *parent)
        : ContactSource(parent)
    {
        m_store = [[CNContactStore alloc] init];
        QPointer<MacContactsSource> self(this);
        m_observer = [[NSNotificationCenter defaultCenter] addObserverForName:CNContactStoreDidChangeNotification
                                                                       object:nil
                                                                        queue:[NSOperationQueue mainQueue]
                                                                   usingBlock:^(NSNotification *) {
                                                                       if (self)
                                                                           self->reload();
                                                                   }];
        // asks the user once (NSContactsUsageDescription); the answer is remembered by macOS
        [m_store requestAccessForEntityType:CNEntityTypeContacts
                          completionHandler:^(BOOL granted, NSError *error) {
                              if (!granted) {
                                  qCInfo(MACCONTACTS) << "no access to contacts"
                                                      << QString::fromNSString(error.localizedDescription ?: @"");
                                  return;
                              }
                              QMetaObject::invokeMethod(
                                  QCoreApplication::instance(), [self] {
                                      if (self)
                                          self->reload();
                                  },
                                  Qt::QueuedConnection);
                          }];
    }
    ~MacContactsSource() override
    {
        if (m_observer)
            [[NSNotificationCenter defaultCenter] removeObserver:m_observer];
    }

private:
    void reload()
    {
        if ([CNContactStore authorizationStatusForEntityType:CNEntityTypeContacts] != CNAuthorizationStatusAuthorized)
            return;
        QList<LocalPerson> people;
        QList<LocalPerson> *out = &people; // blocks capture C++ objects as const copies
        @autoreleasepool {
            NSArray *keys = @[
                [CNContactFormatter descriptorForRequiredKeysForStyle:CNContactFormatterStyleFullName],
                CNContactPhoneNumbersKey, CNContactOrganizationNameKey, CNContactThumbnailImageDataKey
            ];
            CNContactFetchRequest *req = [[CNContactFetchRequest alloc] initWithKeysToFetch:keys];
            NSError *error = nil;
            BOOL ok = [m_store enumerateContactsWithFetchRequest:req
                                                           error:&error
                                                      usingBlock:^(CNContact *c, BOOL *) {
                                                          if (c.phoneNumbers.count == 0)
                                                              return;
                                                          LocalPerson p;
                                                          for (CNLabeledValue<CNPhoneNumber *> *n in c.phoneNumbers)
                                                              p.numbers << QString::fromNSString(n.value.stringValue);
                                                          NSString *name = [CNContactFormatter stringFromContact:c
                                                                                                           style:CNContactFormatterStyleFullName];
                                                          p.name = QString::fromNSString(name.length ? name : c.organizationName);
                                                          if (NSData *d = c.thumbnailImageData)
                                                              p.photo = photoPng(QImage::fromData(QByteArray::fromNSData(d)));
                                                          out->append(p);
                                                      }];
            if (!ok)
                qCWarning(MACCONTACTS) << "reading contacts failed" << QString::fromNSString(error.localizedDescription ?: @"");
        }
        setPeople(people);
    }

    CNContactStore *m_store = nil;
    id m_observer = nil;
};
}

ContactSource *createMacContactsSource(QObject *parent)
{
    return new MacContactsSource(parent);
}
