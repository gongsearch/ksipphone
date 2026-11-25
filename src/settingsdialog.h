// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDialog>

class QLineEdit;
class QComboBox;

struct Settings {
    QString user, server, transport, displayName, codecs;
    QString ldapUri, ldapBase;
    bool preferIpv6 = false;
    bool ringLoop = false;
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
    QLineEdit *m_user, *m_server, *m_display, *m_password, *m_codecs, *m_ldapUri, *m_ldapBase;
    QComboBox *m_transport;
    class QCheckBox *m_ipv6;
    QComboBox *m_ring;
    bool m_pwChanged = false;
};
