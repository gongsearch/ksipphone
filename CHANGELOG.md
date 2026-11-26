# Changelog

## 1.0.1 – 2026-11-26

- Picking a search result calls right away (during a call: transfer), like a click in the extension list
- The window keeps its height when a call starts (room for the call info is reserved)
- Caller photo in the right size on scaled displays (was shown 1.5x / 2x)
- The search field is cleared after a call

## 1.0.0 – 2026-11-26

Feature-complete for a simple SIP phone.

- Call waiting: a second call during a call can be accepted (the first is held), declined or swapped with
- Do not disturb: in the tray menu and the window; calls are shown silently or rejected (busy);
  follows the desktop's do-not-disturb
- Call history: missed, incoming and outgoing calls with filter, call back, copy number, delete
- Extensions for the presence list automatically from the LDAP phonebook (internal numbers; numbers the
  server offers no status for are left out), plus additional ones
- Media (Linux): music and videos are paused during calls; media keys and headset buttons answer and hang up
- Mute follows the system microphone mute (Linux); push to talk (KDE)
- Window always on top (also on KDE Plasma Wayland), window position remembered on Plasma Wayland
- Keyboard shortcuts in the window and global shortcuts (KDE): accept, hang up, mute, push to talk,
  show dialer, do not disturb, call the number in the clipboard
- New settings tab "Controls"

## 0.2.4 – 2026-11-26

- Call transfer: blind, or attended (consult the target first, then connect or go back)
- Presence: list of internal extensions next to the keypad (free / busy / offline), with its own filter,
  sorting (number, first name, last name, status) and "only online" / "only free"; click to call,
  during a call click to transfer. Needs presence on the server (Asterisk: hints and modules, see README)
- Keep-alive for firewalls/NAT (on by default): re-registers every 90 s so incoming calls and status
  updates are not dropped by stateful firewalls
- Search: results from the first letter, sorted alphabetically, name format "First Last" or "Last, First",
  icons for source (LDAP / address book) and number type (mobile / home / work), drop-down closes when
  the field is cleared
- Codec list: clear gap between check box and name in every style (Windows 11 overlapped)
- Window: two columns (keypad, extension list), list can be hidden, size and layout are remembered;
  smaller search font

## 0.2.3 – 2026-11-26

- Settings in tabs: Account, Audio, Phonebook
- Address book of the computer as a second contact source after LDAP (names, photos, search):
  KDE (KPeople, incl. phone contacts via KDE Connect), GNOME / Evolution (only when Evolution Data
  Server is running; no package dependency), macOS Contacts (asks for permission once)
- Windows / macOS: light icons in dark mode, follows switching between light and dark

## 0.2.2 – 2026-11-26

- Settings: codecs as a list with checkboxes; drag & drop or the arrow buttons set the preference
- More codecs: L16 (16 kHz, for the local network) in all packages; AMR-WB and AMR when the installed baresip
  provides them. Codecs the engine cannot use are shown greyed out
- Windows / macOS: all icons come from the bundled Breeze set (the settings icon was missing on Windows)

## 0.2.1 – 2026-11-26

- Windows: the engine (`baresip.exe`) did not start because `libre.dll` was missing from the installer and the
  portable ZIP. libre is now linked statically, and the build checks that every DLL is part of the package.
- Packaging: the `.deb` is uploaded to the release automatically.

## 0.2.0 – 2026-11-26

Packages for all platforms and a more portable engine.

**Downloads**
- Windows: installer (per user, registers `tel:`/`sip:`/`callto:` links, optional autostart) and portable ZIP
- macOS (Apple Silicon): `.dmg` – unsigned, open it the first time with right-click → Open
- Linux: `.deb` (Debian testing, Ubuntu ≥ 25.04; ships its own baresip 4.6), `.rpm` (Fedora; uses the distribution's baresip),
  AppImage (runs on distributions from about 2022 on), Flatpak bundle
- Arch: `PKGBUILD`s for the AUR in `packaging/aur`

**Changes**
- Engine control via baresip's `ctrl_tcp` (loopback TCP) instead of D-Bus; no fixed bus name any more
- Passwords in the system keychain via QtKeychain (KWallet / Secret Service, Windows Credential Manager,
  macOS Keychain); passwords stored by 0.1 are taken over from KWallet
- Audio devices listed through Qt Multimedia instead of `pw-dump`
- Single instance via a local socket instead of D-Bus; new `--settings` option
- baresip is stopped automatically if KSipPhone dies (Linux)
- baresip's module directory is found automatically on Arch, Debian/Ubuntu and Fedora
- Builds without KDE Frameworks (Windows, macOS): Qt tray icon, incoming calls bring the dialer to the front
- Application icon; status line shows the account without port

## 0.1.0 – 2026-11-25

First public release: an audio-only SIP softphone for KDE Plasma on top of baresip.

- Tray application (StatusNotifierItem), dialer window, `--minimized` for autostart
- Incoming calls as KDE notifications with accept / decline, caller name and photo
- Missed calls time out into Plasma's notification history with *Call back*
- Voicemail: message-waiting indicator in window and tray, notification, call the mailbox
- Read-only LDAP phonebook: search while typing, name and `jpegPhoto` lookup
- HD / codec indicator, call timer, keypad with DTMF (RFC 4733, SIP INFO, auto), mute, hold
- Microphone, speaker and separate ring device (PipeWire), any ring tone, once or continuous
- Optional WebRTC echo cancellation, optional IPv6 preference
- Password in KWallet; handler for `tel:`, `sip:`, `sips:` and `callto:` links
- German translation
