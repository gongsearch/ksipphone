# KSipPhone

A small, audio-only SIP softphone for KDE Plasma.

KSipPhone is a thin Qt 6 / KDE Frameworks 6 front end on top of the
[baresip](https://github.com/baresip/baresip) SIP stack. Baresip does all the SIP and
media work (registration, codecs, SRTP, PipeWire audio); KSipPhone runs it as a child
process, controls it over D-Bus (`ctrl_dbus` module) and integrates it into the desktop:
system tray, KDE notifications, KWallet, `tel:` links and an LDAP phonebook.

> Status: early (0.1). Developed and tested against Asterisk (PJSIP); not tested with many other
> servers yet. Issues and pull requests are welcome.

## Features

- **Tray application** (StatusNotifierItem): stays in the tray, the dialer window opens on click;
  starts hidden with `--minimized` (e.g. for autostart).
- **Incoming calls as KDE notifications** with *Accept* / *Decline*, caller name and photo.
- **Missed calls** time out into Plasma's notification history and keep a *Call back* button.
- **Voicemail**: message-waiting indicator (MWI) in window and tray, notification for new
  messages, one click to call the mailbox.
- **LDAP phonebook** (anonymous, read-only): search by name or number while typing,
  caller/callee name and `jpegPhoto` lookup for incoming and outgoing calls.
- **HD indicator**: shows the negotiated codec (HD badge for G.722 / Opus) and a call timer.
- **Audio**: choose microphone, speaker/headset and a separate ring device (PipeWire nodes);
  any sound file as ring tone, ringing once or continuously; optional WebRTC echo cancellation
  (leave it off if PipeWire already cancels echo).
- **Keypad / DTMF** (RFC 4733, SIP INFO or automatic), mute, hold.
- **`tel:`, `sip:`, `sips:`, `callto:` URL handler**, single instance (a click on a phone number
  dials in the running instance).
- **Password in KWallet**, never written to disk by KSipPhone.
- **IPv6**: optionally prefer the server's IPv6 address.

Not (yet) supported: video, chat/presence, call transfer, multiple accounts, conference.

## Requirements

- Qt ≥ 6.6: Core, Gui, Widgets, DBus, Concurrent, Network, Multimedia
- KDE Frameworks ≥ 6.0: CoreAddons, I18n, Config, Notifications, StatusNotifierItem,
  DBusAddons, Wallet, WidgetsAddons; extra-cmake-modules
- OpenLDAP client library (`libldap`)
- **baresip ≥ 4** with the modules `ctrl_dbus`, `menu`, `pipewire`, `g711`, `g722`, `opus`,
  `auconv`, `auresamp`, `srtp`, `mwi` (optional: `webrtc_aec`), installed in
  `/usr/lib/baresip/modules`
- PipeWire (`pw-dump` is used to list audio devices)

On Arch Linux:

```sh
sudo pacman -S --needed baresip cmake extra-cmake-modules ninja qt6-base qt6-multimedia \
    kstatusnotifieritem knotifications kwallet kdbusaddons kconfig ki18n kcoreaddons kwidgetsaddons libldap
```

## Build and install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=$HOME/.local
ninja -C build install
```

This installs the binary, the desktop file (with the URL handlers) and the notification
configuration. After a user-local install run `kbuildsycoca6` once so Plasma finds the app.

To make KSipPhone the handler for phone links:

```sh
for s in tel sip sips callto; do xdg-mime default io.github.gongsearch.KSipPhone.desktop x-scheme-handler/$s; done
```

Autostart (hidden in the tray): add `ksipphone --minimized` in *System Settings → Autostart*.

## Configuration

Everything is set in the settings dialog (gear icon) and stored in `~/.config/ksipphonerc`;
the SIP password goes to KWallet (folder *KSipPhone*).

| Setting | Notes |
|---|---|
| User, server, transport | `sip:user@server;transport=udp|tcp|tls`; server may be `host:port` |
| Prefer IPv6 | registers via the server's IPv6 address if its name has one |
| Codecs | preference order, e.g. `G722,PCMA,PCMU,opus` |
| Voicemail | number to dial for the mailbox |
| DTMF | must match the server (Asterisk `dtmf_mode`) |
| Audio devices | PipeWire nodes, or system default |
| Ring tone | sound file, once or continuously, on its own device |
| LDAP | `ldap://host` and base DN; anonymous read of `cn`, `telephoneNumber`, `mobile`, `homePhone`, `jpegPhoto` |

Baresip's configuration is generated on every start in `~/.local/share/ksipphone/baresip/`;
the account (with password) only exists at runtime.

Note: baresip's `ctrl_dbus` uses the fixed bus name `com.github.Baresip`, so only one
baresip instance with D-Bus control can run per session.

## License

MIT, see [LICENSE](LICENSE).
