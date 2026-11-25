// SPDX-License-Identifier: MIT
#include "secrets.h"

#include <QEventLoop>
#include <QLoggingCategory>

#include <qt6keychain/keychain.h>

#ifdef HAVE_KWALLET
#include <KWallet>
#include <memory>
#endif

Q_LOGGING_CATEGORY(SECRETS, "ksipphone.secrets")

static const QString kService = QStringLiteral("KSipPhone");

namespace
{
template<typename Job>
void runSync(Job &job)
{
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
}

#ifdef HAVE_KWALLET
QString readLegacyKWallet(const QString &key)
{
    using KWallet::Wallet;
    QString pw;
    std::unique_ptr<Wallet> w(Wallet::openWallet(Wallet::NetworkWallet(), 0, Wallet::Synchronous));
    if (w && w->hasFolder(QStringLiteral("KSipPhone")) && w->setFolder(QStringLiteral("KSipPhone")))
        w->readPassword(key, pw);
    return pw;
}
#endif
} // namespace

QString Secrets::readPassword(const QString &key)
{
    QKeychain::ReadPasswordJob job(kService);
    job.setAutoDelete(false);
    job.setKey(key);
    runSync(job);
    if (job.error() == QKeychain::NoError)
        return job.textData();
    if (job.error() != QKeychain::EntryNotFound)
        qCWarning(SECRETS) << "keychain read failed:" << job.errorString();
#ifdef HAVE_KWALLET
    const QString legacy = readLegacyKWallet(key);
    if (!legacy.isEmpty()) {
        const QString err = writePassword(key, legacy);
        qCInfo(SECRETS) << "migrated password from KWallet folder KSipPhone" << (err.isEmpty() ? "ok" : qPrintable(err));
        return legacy;
    }
#endif
    return {};
}

QString Secrets::writePassword(const QString &key, const QString &password)
{
    QKeychain::WritePasswordJob job(kService);
    job.setAutoDelete(false);
    job.setKey(key);
    job.setTextData(password);
    runSync(job);
    return job.error() == QKeychain::NoError ? QString() : job.errorString();
}
