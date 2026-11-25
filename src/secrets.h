// SPDX-License-Identifier: MIT
// Password storage via QtKeychain: KWallet / Secret Service on Linux, Credential Manager on
// Windows, Keychain on macOS.
#pragma once

#include <QString>

namespace Secrets
{
// Returns an empty string if nothing is stored. On Linux, a password stored by KSipPhone <= 0.1
// in KWallet is migrated to the keychain on first read (if built with KWallet support).
QString readPassword(const QString &key);
// Returns an error message, empty on success.
QString writePassword(const QString &key, const QString &password);
}
