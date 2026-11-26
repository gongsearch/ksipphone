// SPDX-License-Identifier: MIT
// Keep a window above others. Qt's WindowStaysOnTopHint works on X11, Windows and macOS;
// on Wayland applications cannot ask for it, so on KDE Plasma a tiny KWin script sets keepAbove.
#pragma once

class QWidget;

namespace KeepAbove
{
void apply(QWidget *window, bool on); // call again after the window was (re)shown
}
