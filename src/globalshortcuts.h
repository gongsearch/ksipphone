// SPDX-License-Identifier: MIT
// System-wide shortcuts (KDE: KGlobalAccel, set in System Settings > Shortcuts > KSipPhone).
// No keys are assigned by default to avoid conflicts. Not available without KDE.
#pragma once

#include <QObject>

class GlobalShortcuts : public QObject
{
    Q_OBJECT
public:
    explicit GlobalShortcuts(QObject *parent = nullptr);
    static bool isAvailable();

Q_SIGNALS:
    void toggleWindow();
    void accept();
    void hangup();
    void toggleMute();
    void toggleDnd();
    void callClipboard();
    void pushToTalk(bool down);
};
