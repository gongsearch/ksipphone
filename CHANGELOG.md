# Changelog

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
