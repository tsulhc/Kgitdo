#include "KWalletSecureStore.h"

#include <KWallet>

namespace
{
constexpr char kFolder[] = "Kgitdo";
constexpr char kAccessKey[] = "accessToken";
constexpr char kRefreshKey[] = "refreshToken";
constexpr char kExpiresKey[] = "accessExpiresAt";
constexpr char kLoginKey[] = "login";
} // namespace

KWalletSecureStore::KWalletSecureStore(QObject *parent)
    : ISecureStore(parent)
{
}

bool KWalletSecureStore::isAvailable() const
{
    return KWallet::Wallet::isEnabled();
}

QString KWalletSecureStore::backendName() const
{
    return QStringLiteral("kwallet");
}

bool KWalletSecureStore::load(StoredTokens &out)
{
    if (!KWallet::Wallet::isEnabled()) {
        return false;
    }
    KWallet::Wallet *wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet || !wallet->isOpen()) {
        delete wallet;
        return false;
    }
    bool loaded = false;
    if (!wallet->hasFolder(QLatin1String(kFolder))) {
        delete wallet;
        return false;
    }
    if (wallet->setFolder(QLatin1String(kFolder))) {
        QString access;
        QString refresh;
        QString login;
        QByteArray expiresRaw;
        if (wallet->readPassword(QLatin1String(kAccessKey), access) == 0 && !access.isEmpty()) {
            wallet->readPassword(QLatin1String(kRefreshKey), refresh);
            wallet->readPassword(QLatin1String(kLoginKey), login);
            qint64 expiresAt = 0;
            if (wallet->readEntry(QLatin1String(kExpiresKey), expiresRaw) == 0 && !expiresRaw.isEmpty()) {
                expiresAt = expiresRaw.toLongLong();
            }
            out.accessToken = access;
            out.refreshToken = refresh;
            out.accessExpiresAtSecsSinceEpoch = expiresAt;
            out.login = login;
            loaded = true;
        }
    }
    delete wallet;
    return loaded;
}

bool KWalletSecureStore::save(const StoredTokens &tokens)
{
    if (!KWallet::Wallet::isEnabled() || tokens.accessToken.isEmpty()) {
        return false;
    }
    KWallet::Wallet *wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet || !wallet->isOpen()) {
        delete wallet;
        return false;
    }
    bool ok = false;
    if (!wallet->hasFolder(QLatin1String(kFolder))) {
        wallet->createFolder(QLatin1String(kFolder));
    }
    if (wallet->hasFolder(QLatin1String(kFolder)) && wallet->setFolder(QLatin1String(kFolder))) {
        const int accessRc = wallet->writePassword(QLatin1String(kAccessKey), tokens.accessToken);
        const int refreshRc = wallet->writePassword(QLatin1String(kRefreshKey), tokens.refreshToken);
        const int loginRc = wallet->writePassword(QLatin1String(kLoginKey), tokens.login);
        const int expiresRc = wallet->writeEntry(QLatin1String(kExpiresKey),
                                                 QByteArray::number(tokens.accessExpiresAtSecsSinceEpoch));
        ok = (accessRc == 0 && refreshRc == 0 && loginRc == 0 && expiresRc == 0);
    }
    delete wallet;
    return ok;
}

bool KWalletSecureStore::clear()
{
    if (!KWallet::Wallet::isEnabled()) {
        return true;
    }
    KWallet::Wallet *wallet =
        KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0, KWallet::Wallet::Synchronous);
    if (!wallet || !wallet->isOpen()) {
        delete wallet;
        // Nothing persisted that we can remove; local memory is cleared anyway.
        return true;
    }
    if (wallet->hasFolder(QLatin1String(kFolder)) && wallet->setFolder(QLatin1String(kFolder))) {
        wallet->removeEntry(QLatin1String(kAccessKey));
        wallet->removeEntry(QLatin1String(kRefreshKey));
        wallet->removeEntry(QLatin1String(kExpiresKey));
        wallet->removeEntry(QLatin1String(kLoginKey));
    }
    delete wallet;
    return true;
}
