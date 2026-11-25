// SPDX-License-Identifier: MIT
#include "desktop.h"

#include <QIcon>
#include <QMenu>
#include <QWidget>

#ifdef KSIPPHONE_WITH_KDE
#include <KNotification>
#include <KStatusNotifierItem>
#include <QWindow>
#else
#include <QSystemTrayIcon>
#endif

namespace Desktop
{

#ifdef KSIPPHONE_WITH_KDE
// ------------------------------------------------------------------ KDE implementation
bool richNotifications()
{
    return true;
}

struct Tray::Private {
    KStatusNotifierItem *sni;
};

Tray::Tray(QWidget *window, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->sni = new KStatusNotifierItem(QStringLiteral("ksipphone"), this);
    d->sni->setCategory(KStatusNotifierItem::Communications);
    d->sni->setTitle(QStringLiteral("KSipPhone"));
    d->sni->setStandardActionsEnabled(false);
    window->winId();
    d->sni->setAssociatedWindow(window->windowHandle());
    connect(d->sni, &KStatusNotifierItem::activateRequested, this, [this](bool) { Q_EMIT activated(); });
}

Tray::~Tray()
{
    delete d;
}

void Tray::setIconName(const QString &name)
{
    d->sni->setIconByName(name);
    d->sni->setAttentionIconByName(name);
}

void Tray::setToolTip(const QString &title, const QString &text)
{
    d->sni->setToolTip(d->sni->iconName(), title, text);
}

void Tray::setAttention(bool on)
{
    d->sni->setStatus(on ? KStatusNotifierItem::NeedsAttention : KStatusNotifierItem::Active);
}

void Tray::setMenu(QMenu *menu)
{
    d->sni->setContextMenu(menu);
}

void Tray::showMessage(const QString &title, const QString &text, const QPixmap &)
{
    KNotification::event(KNotification::Notification, title, text);
}

struct Notification::Private {
    QPointer<KNotification> n;
};

Notification::Notification(const QString &eventId, Kind kind, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
    d->n = new KNotification(eventId, kind == Kind::Persistent ? KNotification::Persistent : KNotification::CloseOnTimeout);
    // KNotification deletes itself when closed; so do we.
    connect(d->n, &KNotification::closed, this, &QObject::deleteLater);
}

Notification::~Notification()
{
    delete d;
}

void Notification::setTitle(const QString &t) { if (d->n) d->n->setTitle(t); }
void Notification::setText(const QString &t) { if (d->n) d->n->setText(t); }
void Notification::setIconName(const QString &n) { if (d->n) d->n->setIconName(n); }
void Notification::setPixmap(const QPixmap &p) { if (d->n) d->n->setPixmap(p); }
void Notification::setUrgent(bool urgent)
{
    if (d->n)
        d->n->setUrgency(urgent ? KNotification::CriticalUrgency : KNotification::NormalUrgency);
}

int Notification::addAction(const QString &label)
{
    static int counter = 0;
    if (!d->n)
        return -1;
    const int index = counter++;
    auto *a = d->n->addAction(label);
    connect(a, &KNotificationAction::activated, this, [this, index] { Q_EMIT actionTriggered(index); });
    return index;
}

void Notification::setDefaultAction(const QString &label)
{
    if (!d->n)
        return;
    auto *a = d->n->addDefaultAction(label);
    connect(a, &KNotificationAction::activated, this, &Notification::defaultTriggered);
}

void Notification::send() { if (d->n) d->n->sendEvent(); }
void Notification::close() { if (d->n) d->n->close(); }
void Notification::setTray(Tray *) {}

void Notification::error(const QString &title, const QString &text)
{
    KNotification::event(KNotification::Error, title, text);
}

#else
// ------------------------------------------------------------------ Qt implementation
static QPointer<Tray> s_tray;

bool richNotifications()
{
    return false;
}

struct Tray::Private {
    QSystemTrayIcon *icon;
    QWidget *window;
};

Tray::Tray(QWidget *window, QObject *parent)
    : QObject(parent)
    , d(new Private{new QSystemTrayIcon(this), window})
{
    connect(d->icon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r != QSystemTrayIcon::Trigger && r != QSystemTrayIcon::DoubleClick)
            return;
        if (d->window->isVisible() && d->window->isActiveWindow()) {
            d->window->hide();
        } else {
            d->window->show();
            d->window->raise();
            d->window->activateWindow();
        }
        Q_EMIT activated();
    });
    connect(d->icon, &QSystemTrayIcon::messageClicked, this, &Tray::messageClicked);
    d->icon->show();
}

Tray::~Tray()
{
    delete d;
}

void Tray::setIconName(const QString &name)
{
    d->icon->setIcon(QIcon::fromTheme(name));
}

void Tray::setToolTip(const QString &title, const QString &text)
{
    QString t = text;
    t.replace(QLatin1String("<br>"), QLatin1String("\n"));
    d->icon->setToolTip(title + QLatin1Char('\n') + t);
}

void Tray::setAttention(bool) {}

void Tray::setMenu(QMenu *menu)
{
    d->icon->setContextMenu(menu);
}

void Tray::showMessage(const QString &title, const QString &text, const QPixmap &pixmap)
{
    if (pixmap.isNull())
        d->icon->showMessage(title, text, QSystemTrayIcon::Information, 10000);
    else
        d->icon->showMessage(title, text, QIcon(pixmap), 10000);
}

struct Notification::Private {
    QString title, text;
    QPixmap pixmap;
    bool sent = false;
    QMetaObject::Connection click;
};

Notification::Notification(const QString &, Kind, QObject *parent)
    : QObject(parent)
    , d(new Private)
{
}

Notification::~Notification()
{
    QObject::disconnect(d->click);
    delete d;
}

void Notification::setTitle(const QString &t) { d->title = t; }
void Notification::setText(const QString &t)
{
    d->text = t;
    if (d->sent && s_tray)
        s_tray->showMessage(d->title, d->text, d->pixmap);
}
void Notification::setIconName(const QString &) {}
void Notification::setPixmap(const QPixmap &p) { d->pixmap = p; }
void Notification::setUrgent(bool) {}
int Notification::addAction(const QString &) { return -1; } // no buttons in tray balloons
void Notification::setDefaultAction(const QString &) {}

void Notification::send()
{
    if (!s_tray)
        return;
    d->sent = true;
    QObject::disconnect(d->click);
    d->click = connect(s_tray, &Tray::messageClicked, this, &Notification::defaultTriggered);
    s_tray->showMessage(d->title, d->text, d->pixmap);
}

void Notification::close()
{
    QObject::disconnect(d->click);
    deleteLater();
}

void Notification::setTray(Tray *tray)
{
    s_tray = tray;
}

void Notification::error(const QString &title, const QString &text)
{
    if (s_tray)
        s_tray->showMessage(title, text, {});
}
#endif

} // namespace Desktop
