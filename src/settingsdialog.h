// SPDX-License-Identifier: MIT
#pragma once

#include <QDialog>

class QSettings;
// the application's settings file (~/.config/ksipphonerc)
QSettings &appSettings();

class QLineEdit;
class QComboBox;

struct Settings {
    QString user, server, transport, displayName, codecs;
    QString ldapUri, ldapBase;
    bool useLocalContacts = true; // desktop address book (KPeople)
    int nameFormat = 0;           // Phonebook::NameFormat: 0 first last, 1 last, first
    QString voicemail; // number to dial for the mailbox
    QStringList watched; // extensions shown with presence status
    QString dtmfMode;  // rtpevent | info | auto
    bool preferIpv6 = false;
    bool keepAlive = true;
    bool ringLoop = false;
    bool dndReject = false;       // do not disturb: reject calls (else: show them silently)
    bool dndFollowSystem = true;  // also when the desktop's do-not-disturb is on
    bool echoCancel = false; // baresip webrtc_aec
    QString micDevice, speakerDevice, ringDevice, ringFile; // PipeWire node names, "" = default
    static Settings load();
    void save() const;
};

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    SettingsDialog(const Settings &s, const QString &password, QWidget *parent = nullptr);
    Settings settings() const;
    QString password() const;
    bool passwordChanged() const { return m_pwChanged; }

private:
    QLineEdit *m_voicemail;
    QLineEdit *m_watched;
    QLineEdit *m_user, *m_server, *m_display, *m_password, *m_ldapUri, *m_ldapBase;
    class QListWidget *m_codecs;
    QComboBox *m_transport;
    class QCheckBox *m_ipv6;
    class QCheckBox *m_aec;
    class QCheckBox *m_keepAlive;
    class QCheckBox *m_local = nullptr;
    QComboBox *m_dtmf;
    QComboBox *m_nameFormat;
    QComboBox *m_ring, *m_mic, *m_speaker, *m_ringDev, *m_dnd;
    class QCheckBox *m_dndSystem = nullptr;
    QLineEdit *m_ringFile;
    class Ringer *m_preview;
    bool m_pwChanged = false;
};
