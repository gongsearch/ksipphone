// SPDX-License-Identifier: MIT
// GNOME and others: Evolution Data Server (the address books of GNOME Contacts / Evolution: local, Google,
// CardDAV/Nextcloud, ...). libebook is loaded at runtime, so packages don't depend on EDS; it is only used
// while EDS's source registry already runs on the session bus (GNOME starts it), never activated by us.
#include "contactsource.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFutureWatcher>
#include <QImage>
#include <QLoggingCategory>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>

#include <dlfcn.h>

Q_LOGGING_CATEGORY(EDSCONTACTS, "ksipphone.contacts.eds")

namespace
{
// the few GLib/EDS types we touch, ABI-stable public structs
struct GError_ {
    quint32 domain;
    int code;
    char *message;
};
struct GList_ {
    void *data;
    GList_ *next, *prev;
};
struct GSList_ {
    void *data;
    GSList_ *next;
};
struct EContactPhoto_ {
    int type; // 0 = inlined, 1 = uri
    union {
        struct {
            char *mime_type;
            size_t length;
            unsigned char *data;
        } inlined;
        char *uri;
    } data;
};

struct Eds {
    void *(*registry_new_sync)(void *cancellable, GError_ **err) = nullptr;
    GList_ *(*registry_list_enabled)(void *registry, const char *extension) = nullptr;
    void *(*client_connect_sync)(void *source, quint32 waitSeconds, void *cancellable, GError_ **err) = nullptr;
    int (*get_contacts_sync)(void *client, const char *sexp, GSList_ **out, void *cancellable, GError_ **err) = nullptr;
    void *(*query_field_exists)(int field) = nullptr;
    char *(*query_to_string)(void *query) = nullptr;
    void (*query_unref)(void *query) = nullptr;
    int (*field_id)(const char *name) = nullptr;
    void *(*contact_get)(void *contact, int field) = nullptr;
    const void *(*contact_get_const)(void *contact, int field) = nullptr;
    void (*photo_free)(void *photo) = nullptr;
    void (*object_unref)(void *obj) = nullptr;
    void (*list_free_full)(GList_ *list, void (*freeFunc)(void *)) = nullptr;
    void (*slist_free_full)(GSList_ *list, void (*freeFunc)(void *)) = nullptr;
    void (*free)(void *mem) = nullptr;
    void (*error_free)(GError_ *err) = nullptr;
    int fTel = 0, fFullName = 0, fFileAs = 0, fOrg = 0, fPhoto = 0;

    bool load()
    {
        void *h = nullptr;
        for (const char *name : {"libebook-1.2.so.21", "libebook-1.2.so.20", "libebook-1.2.so.19", "libebook-1.2.so"}) {
            if ((h = dlopen(name, RTLD_NOW | RTLD_LOCAL)))
                break;
        }
        if (!h)
            return false;
        bool ok = true;
        auto sym = [&](auto &fn, const char *name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(h, name));
            ok = ok && fn;
        };
        sym(registry_new_sync, "e_source_registry_new_sync");
        sym(registry_list_enabled, "e_source_registry_list_enabled");
        sym(client_connect_sync, "e_book_client_connect_sync");
        sym(get_contacts_sync, "e_book_client_get_contacts_sync");
        sym(query_field_exists, "e_book_query_field_exists");
        sym(query_to_string, "e_book_query_to_string");
        sym(query_unref, "e_book_query_unref");
        sym(field_id, "e_contact_field_id");
        sym(contact_get, "e_contact_get");
        sym(contact_get_const, "e_contact_get_const");
        sym(photo_free, "e_contact_photo_free");
        sym(object_unref, "g_object_unref");
        sym(list_free_full, "g_list_free_full");
        sym(slist_free_full, "g_slist_free_full");
        sym(free, "g_free");
        sym(error_free, "g_error_free");
        if (!ok) {
            qCWarning(EDSCONTACTS) << "libebook found but symbols missing";
            return false;
        }
        fTel = field_id("phone"); // E_CONTACT_TEL: all numbers of the vCard
        fFullName = field_id("full_name");
        fFileAs = field_id("file_as");
        fOrg = field_id("org");
        fPhoto = field_id("photo");
        return fTel > 0 && fFullName > 0 && fPhoto > 0;
    }
};

Eds *eds()
{
    static Eds *e = [] {
        auto *x = new Eds;
        if (!x->load()) {
            delete x;
            return static_cast<Eds *>(nullptr);
        }
        return x;
    }();
    return e;
}

QString takeError(Eds *e, GError_ *&err)
{
    QString msg = err && err->message ? QString::fromUtf8(err->message) : QString();
    if (err)
        e->error_free(err);
    err = nullptr;
    return msg;
}

// runs in a worker thread
QList<LocalPerson> loadAll()
{
    QList<LocalPerson> out;
    Eds *e = eds();
    GError_ *err = nullptr;
    void *registry = e->registry_new_sync(nullptr, &err);
    if (!registry) {
        qCDebug(EDSCONTACTS) << "no source registry:" << takeError(e, err);
        return out;
    }
    void *q = e->query_field_exists(e->fTel);
    char *sexp = e->query_to_string(q);
    GList_ *sources = e->registry_list_enabled(registry, "Address Book");
    for (GList_ *s = sources; s; s = s->next) {
        void *client = e->client_connect_sync(s->data, 10, nullptr, &err);
        if (!client) {
            qCDebug(EDSCONTACTS) << "address book not available:" << takeError(e, err);
            continue;
        }
        GSList_ *contacts = nullptr;
        if (!e->get_contacts_sync(client, sexp, &contacts, nullptr, &err)) {
            qCDebug(EDSCONTACTS) << "reading contacts failed:" << takeError(e, err);
        }
        for (GSList_ *c = contacts; c; c = c->next) {
            LocalPerson p;
            auto *tels = static_cast<GList_ *>(e->contact_get(c->data, e->fTel));
            for (GList_ *t = tels; t; t = t->next)
                p.numbers << QString::fromUtf8(static_cast<const char *>(t->data));
            if (tels)
                e->list_free_full(tels, e->free);
            if (p.numbers.isEmpty())
                continue;
            for (int f : {e->fFullName, e->fFileAs, e->fOrg}) {
                if (f > 0 && p.name.isEmpty())
                    p.name = QString::fromUtf8(static_cast<const char *>(e->contact_get_const(c->data, f))).trimmed();
            }
            if (auto *ph = static_cast<EContactPhoto_ *>(e->contact_get(c->data, e->fPhoto))) {
                QImage img;
                if (ph->type == 0 && ph->data.inlined.data)
                    img.loadFromData(ph->data.inlined.data, int(ph->data.inlined.length));
                else if (ph->type == 1 && ph->data.uri && QUrl(QString::fromUtf8(ph->data.uri)).isLocalFile())
                    img.load(QUrl(QString::fromUtf8(ph->data.uri)).toLocalFile());
                p.photo = ContactSource::photoPng(img);
                e->photo_free(ph);
            }
            out.append(p);
        }
        if (contacts)
            e->slist_free_full(contacts, e->object_unref);
        e->object_unref(client);
    }
    if (sources)
        e->list_free_full(sources, e->object_unref);
    e->free(sexp);
    e->query_unref(q);
    e->object_unref(registry);
    return out;
}

bool registryRunning()
{
    const QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (!bus)
        return false;
    const QStringList names = bus->registeredServiceNames().value();
    return std::any_of(names.begin(), names.end(), [](const QString &n) {
        return n.startsWith(QLatin1String("org.gnome.evolution.dataserver.Sources"));
    });
}

class EdsSource : public ContactSource
{
public:
    explicit EdsSource(QObject *parent)
        : ContactSource(parent)
    {
        // EDS has change notifications only per address book view; a periodic reload is plenty here
        auto *timer = new QTimer(this);
        timer->setInterval(10 * 60 * 1000);
        connect(timer, &QTimer::timeout, this, &EdsSource::reload);
        timer->start();
        QTimer::singleShot(0, this, &EdsSource::reload);
    }

private:
    void reload()
    {
        if (m_loading || !registryRunning())
            return;
        m_loading = true;
        QPointer<EdsSource> self(this);
        auto *w = new QFutureWatcher<QList<LocalPerson>>(this);
        connect(w, &QFutureWatcher<QList<LocalPerson>>::finished, this, [self, w] {
            if (self) {
                self->m_loading = false;
                qCDebug(EDSCONTACTS) << "EDS:" << w->result().size() << "contacts with numbers";
                self->setPeople(w->result());
            }
            w->deleteLater();
        });
        w->setFuture(QtConcurrent::run(&loadAll));
    }
    bool m_loading = false;
};
}

bool edsAvailable()
{
    return eds() != nullptr;
}

ContactSource *createEdsSource(QObject *parent)
{
    return eds() ? new EdsSource(parent) : nullptr;
}
