// SPDX-License-Identifier: MIT
// Thin desktop integration layer: tray icon and notifications.
// With KSIPPHONE_WITH_KDE: KStatusNotifierItem + KNotification (Plasma, also other Linux desktops).
// Without (Windows, macOS, minimal Linux builds): QSystemTrayIcon + tray balloon messages; actions
// are not available there, the caller shows the dialer window instead.
#pragma once

#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>

class QMenu;
class QWidget;

namespace Desktop
{
// true if notifications can carry action buttons (accept / decline / call back)
bool richNotifications();

class Tray : public QObject
{
    Q_OBJECT
public:
    explicit Tray(QWidget *window, QObject *parent = nullptr);
    ~Tray() override;
    void setIconName(const QString &name);
    void setToolTip(const QString &title, const QString &text);
    void setAttention(bool on);
    void setMenu(QMenu *menu);
    // plain message near the tray (used for notifications without KDE)
    void showMessage(const QString &title, const QString &text, const QPixmap &pixmap);

Q_SIGNALS:
    void activated();
    void messageClicked();

private:
    struct Private;
    Private *d;
};

class Notification : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Transient, Persistent };
    Notification(const QString &eventId, Kind kind, QObject *parent = nullptr);
    ~Notification() override;
    void setTitle(const QString &t);
    void setText(const QString &t); // updates a visible notification
    void setIconName(const QString &n);
    void setPixmap(const QPixmap &p);
    void setUrgent(bool urgent);
    int addAction(const QString &label); // returns the action index
    void setDefaultAction(const QString &label);
    void send();
    void close();

    // the tray used for the fallback balloon messages
    static void setTray(Tray *tray);
    static void error(const QString &title, const QString &text);

Q_SIGNALS:
    void actionTriggered(int index);
    void defaultTriggered();

private:
    struct Private;
    Private *d;
};
} // namespace Desktop
