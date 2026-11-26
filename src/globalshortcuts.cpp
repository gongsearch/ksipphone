// SPDX-License-Identifier: MIT
#include "globalshortcuts.h"

#include <KLocalizedString>

#include <QAction>

#ifdef KSIPPHONE_WITH_GLOBALACCEL
#include <KGlobalAccel>
#endif

GlobalShortcuts::GlobalShortcuts(QObject *parent)
    : QObject(parent)
{
#ifdef KSIPPHONE_WITH_GLOBALACCEL
    auto add = [this](const char *id, const QString &text, auto signal) {
        auto *a = new QAction(text, this);
        a->setObjectName(QString::fromLatin1(id));
        KGlobalAccel::setGlobalShortcut(a, QList<QKeySequence>()); // registered, no default key
        connect(a, &QAction::triggered, this, signal);
        return a;
    };
    add("toggle_window", i18n("Show or hide the dialer"), &GlobalShortcuts::toggleWindow);
    add("accept_call", i18n("Accept call"), &GlobalShortcuts::accept);
    add("hangup_call", i18n("Hang up"), &GlobalShortcuts::hangup);
    add("toggle_mute", i18n("Mute / unmute"), &GlobalShortcuts::toggleMute);
    add("toggle_dnd", i18n("Do not disturb on / off"), &GlobalShortcuts::toggleDnd);
    add("call_clipboard", i18n("Call the number in the clipboard"), &GlobalShortcuts::callClipboard);
    // push-to-talk needs press and release
    auto *ptt = new QAction(i18n("Push to talk (hold)"), this);
    ptt->setObjectName(QStringLiteral("push_to_talk"));
    KGlobalAccel::setGlobalShortcut(ptt, QList<QKeySequence>());
    connect(KGlobalAccel::self(), &KGlobalAccel::globalShortcutActiveChanged, this, [this, ptt](QAction *a, bool active) {
        if (a == ptt)
            Q_EMIT pushToTalk(active);
    });
#endif
}

bool GlobalShortcuts::isAvailable()
{
#ifdef KSIPPHONE_WITH_GLOBALACCEL
    return true;
#else
    return false;
#endif
}
