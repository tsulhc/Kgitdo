#include "KWalletSecureStore.h"

#include <KWallet>

namespace
{
constexpr char kFolder[] = "Kgitdo";
constexpr char kAccessKey[] = "accessToken";
constexpr char kRefreshKey[] = "refreshToken";
constexpr char kExpiresKey[] = "accessExpiresAt";
constexpr char kLoginKey[] = "login";
// Commit marker, always written last. load() only accepts a token set that
// carries this marker, so a save interrupted (or rolled back) midway can
// never be mistaken for a valid session, even if rollback removal fails.
constexpr char kCommitKey[] = "committed";
constexpr char kCommitValue[] = "1";
} // namespace

// Production KWallet backend. Removal success is verified by read-back so a
// failed removal is never reported as cleared, regardless of how the
// underlying removeEntry call reports its result.
class KWalletBackend : public IWalletBackend
{
public:
    explicit KWalletBackend(QObject *parent = nullptr)
        : IWalletBackend(parent)
    {
    }

    bool isEnabled() const override
    {
        return KWallet::Wallet::isEnabled();
    }

    bool open() override
    {
        close();
        if (!KWallet::Wallet::isEnabled()) {
            return false;
        }
        m_wallet = KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(), 0,
                                               KWallet::Wallet::Synchronous);
        return m_wallet && m_wallet->isOpen();
    }

    void close() override
    {
        delete m_wallet;
        m_wallet = nullptr;
    }

    bool selectFolder(bool createIfMissing) override
    {
        if (!m_wallet) {
            return false;
        }
        if (!m_wallet->hasFolder(QLatin1String(kFolder))) {
            if (!createIfMissing) {
                return false;
            }
            m_wallet->createFolder(QLatin1String(kFolder));
        }
        return m_wallet->hasFolder(QLatin1String(kFolder))
            && m_wallet->setFolder(QLatin1String(kFolder));
    }

    bool folderExists() override
    {
        return m_wallet && m_wallet->hasFolder(QLatin1String(kFolder));
    }

    bool readSecret(const QString &key, QString &value) override
    {
        if (!m_wallet) {
            return false;
        }
        return m_wallet->readPassword(key, value) == 0;
    }

    bool readBlob(const QString &key, QByteArray &value) override
    {
        if (!m_wallet) {
            return false;
        }
        return m_wallet->readEntry(key, value) == 0;
    }

    bool writeSecret(const QString &key, const QString &value) override
    {
        if (!m_wallet) {
            return false;
        }
        return m_wallet->writePassword(key, value) == 0;
    }

    bool writeBlob(const QString &key, const QByteArray &value) override
    {
        if (!m_wallet) {
            return false;
        }
        return m_wallet->writeEntry(key, value) == 0;
    }

    bool remove(const QString &key) override
    {
        if (!m_wallet) {
            return false;
        }
        m_wallet->removeEntry(key);
        // Verify the key is really gone instead of trusting the removal call.
        QString secret;
        QByteArray blob;
        return m_wallet->readPassword(key, secret) != 0 && m_wallet->readEntry(key, blob) != 0;
    }

private:
    KWallet::Wallet *m_wallet = nullptr;
};

KWalletSecureStore::KWalletSecureStore(QObject *parent, IWalletBackend *backend)
    : ISecureStore(parent)
    , m_backend(backend ? backend : new KWalletBackend(this))
{
}

bool KWalletSecureStore::isAvailable() const
{
    return m_backend && m_backend->isEnabled();
}

QString KWalletSecureStore::backendName() const
{
    return QStringLiteral("kwallet");
}

bool KWalletSecureStore::load(StoredTokens &out)
{
    if (!m_backend || !m_backend->isEnabled()) {
        return false;
    }
    if (!m_backend->open()) {
        return false;
    }
    bool loaded = false;
    if (m_backend->selectFolder(false)) {
        QByteArray commit;
        if (!m_backend->readBlob(QLatin1String(kCommitKey), commit)
            || commit != QByteArray(kCommitValue)) {
            // No commit marker: absent session or interrupted/rolled-back
            // save. Never accept partial credentials as a session.
            m_backend->close();
            return false;
        }
        QString access;
        if (m_backend->readSecret(QLatin1String(kAccessKey), access) && !access.isEmpty()) {
            StoredTokens tokens;
            QString refresh;
            QString login;
            QByteArray expiresRaw;
            m_backend->readSecret(QLatin1String(kRefreshKey), refresh);
            m_backend->readSecret(QLatin1String(kLoginKey), login);
            qint64 expiresAt = 0;
            if (m_backend->readBlob(QLatin1String(kExpiresKey), expiresRaw) && !expiresRaw.isEmpty()) {
                expiresAt = expiresRaw.toLongLong();
            }
            tokens.accessToken = access;
            tokens.refreshToken = refresh;
            tokens.accessExpiresAtSecsSinceEpoch = expiresAt;
            tokens.login = login;
            out = tokens;
            loaded = true;
        }
    }
    m_backend->close();
    return loaded;
}

bool KWalletSecureStore::save(const StoredTokens &tokens)
{
    if (!m_backend || !m_backend->isEnabled() || tokens.accessToken.isEmpty()) {
        return false;
    }
    if (!m_backend->open()) {
        return false;
    }
    bool ok = false;
    if (m_backend->selectFolder(true)) {
        const bool accessOk =
            m_backend->writeSecret(QLatin1String(kAccessKey), tokens.accessToken);
        const bool refreshOk =
            m_backend->writeSecret(QLatin1String(kRefreshKey), tokens.refreshToken);
        const bool loginOk = m_backend->writeSecret(QLatin1String(kLoginKey), tokens.login);
        const bool expiresOk = m_backend->writeBlob(
            QLatin1String(kExpiresKey), QByteArray::number(tokens.accessExpiresAtSecsSinceEpoch));
        const bool commitOk =
            m_backend->writeBlob(QLatin1String(kCommitKey), QByteArray(kCommitValue));
        ok = accessOk && refreshOk && loginOk && expiresOk && commitOk;
        if (!ok) {
            // Roll back partial writes: never leave mixed or half-rotated
            // credentials behind for a later launch to pick up. Removal is
            // best effort; load() additionally requires the commit marker,
            // so even a failed rollback cannot produce an accepted session.
            m_backend->remove(QLatin1String(kAccessKey));
            m_backend->remove(QLatin1String(kRefreshKey));
            m_backend->remove(QLatin1String(kExpiresKey));
            m_backend->remove(QLatin1String(kLoginKey));
            m_backend->remove(QLatin1String(kCommitKey));
        }
    }
    m_backend->close();
    return ok;
}

bool KWalletSecureStore::clear()
{
    if (!m_backend || !m_backend->isEnabled()) {
        // Wallet service absent: nothing could have been persisted here.
        return true;
    }
    // Wallet enabled but not openable: credentials may remain, so report
    // failure instead of claiming a logout that cannot be verified.
    if (!m_backend->open()) {
        return false;
    }
    bool ok = false;
    if (!m_backend->folderExists()) {
        // No folder: nothing was ever persisted here.
        ok = true;
    } else if (!m_backend->selectFolder(false)) {
        // Folder exists but cannot be selected: credentials may remain, so
        // report failure instead of claiming a logout that cannot be verified.
        ok = false;
    } else {
        ok = m_backend->remove(QLatin1String(kAccessKey)) && ok;
        ok = m_backend->remove(QLatin1String(kRefreshKey)) && ok;
        ok = m_backend->remove(QLatin1String(kExpiresKey)) && ok;
        ok = m_backend->remove(QLatin1String(kLoginKey)) && ok;
        ok = m_backend->remove(QLatin1String(kCommitKey)) && ok;
    }
    m_backend->close();
    return ok;
}
