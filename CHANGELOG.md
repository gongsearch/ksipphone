# Changelog

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
