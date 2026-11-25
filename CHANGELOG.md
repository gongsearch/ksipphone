# Changelog

## Unreleased

- Engine control via baresip's `ctrl_tcp` (loopback TCP) instead of D-Bus; no fixed bus name any more
- Passwords in the system keychain via QtKeychain; passwords from 0.1 are taken over from KWallet
- Audio devices listed through Qt Multimedia instead of `pw-dump`
- Single instance via a local socket instead of D-Bus
- baresip is stopped automatically if KSipPhone dies (Linux)
- Groundwork for Windows / macOS (platform audio driver, configurable baresip paths)

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
