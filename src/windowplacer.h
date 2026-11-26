// SPDX-License-Identifier: MIT
// Window position on KDE Plasma (Wayland): applications cannot place their windows there, and a window
// shown again from the tray is placed anew by KWin. A KWin script that stays loaded while KSipPhone runs
// remembers the dialer's position and puts it back when the window reappears; it also reports the
// position over D-Bus so it survives a restart. Elsewhere Qt's saved geometry does the job.
#pragma once

#include <QObject>

class QWidget;

class WindowPlacer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.gongsearch.KSipPhone.Window")
public:
    explicit WindowPlacer(QWidget *window, QObject *parent = nullptr);
    ~WindowPlacer() override;

    void start(); // after the window got its title
    void stop();  // unload the KWin script (on quit)

public Q_SLOTS:
    Q_SCRIPTABLE void storePosition(int x, int y); // called by the KWin script

private:
    QWidget *m_window;
    bool m_active = false;
};
