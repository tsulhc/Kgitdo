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

// Production KWallet backend. Removal success is decided by
// WalletRemovePolicy from the API's own return code plus read-back: an
// API-reported failure is fail-closed (false) unless absence is confirmed,
// and an API-reported success is verified by unreadability.
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

    bool hasEntry(const QString &key) override
    {
        return m_wallet && m_wallet->hasEntry(key);
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
        if (m_wallet->writePassword(key, value) != 0) {
            return false;
        }
        // Verify rather than trust the write call: a silent non-write must
        // not be treated as persisted (see contract above).
        QString check;
        return m_wallet->readPassword(key, check) == 0 && check == value;
    }

    bool writeBlob(const QString &key, const QByteArray &value) override
    {
        if (!m_wallet) {
            return false;
        }
        if (m_wallet->writeEntry(key, value) != 0) {
            return false;
        }
        QByteArray check;
        return m_wallet->readEntry(key, check) == 0 && check == value;
    }

    bool remove(const QString &key) override
    {
        if (!m_wallet) {
            return false;
        }
        const bool removeSucceeded = (m_wallet->removeEntry(key) == 0);
        const bool listed = m_wallet->hasEntry(key);
        QString secret;
        QByteArray blob;
        const bool secretReadable = (m_wallet->readPassword(key, secret) == 0);
        const bool blobReadable = (m_wallet->readEntry(key, blob) == 0);
        return WalletRemovePolicy::confirmRemoved(removeSucceeded, listed, secretReadable,
                                                 blobReadable);
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

bool KWalletSecureStore::credentialEntriesAbsent()
{
    // Both oracles must agree per key: not listed AND unreadable. A listing
    // alone never proves presence and a read error alone never proves
    // absence, so either oracle reporting a key keeps cleanup armed.
    QString secret;
    QByteArray blob;
    const bool accessGone = !m_backend->hasEntry(QLatin1String(kAccessKey))
        && !m_backend->readSecret(QLatin1String(kAccessKey), secret);
    const bool refreshGone = !m_backend->hasEntry(QLatin1String(kRefreshKey))
        && !m_backend->readSecret(QLatin1String(kRefreshKey), secret);
    const bool loginGone = !m_backend->hasEntry(QLatin1String(kLoginKey))
        && !m_backend->readSecret(QLatin1String(kLoginKey), secret);
    const bool expiresGone = !m_backend->hasEntry(QLatin1String(kExpiresKey))
        && !m_backend->readBlob(QLatin1String(kExpiresKey), blob);
    return accessGone && refreshGone && loginGone && expiresGone;
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
    if (!m_backend->folderExists()) {
        // No folder: nothing was ever persisted here, so emptiness is
        // established even on a fresh instance. A later disabled clear may
        // report success without claiming anything unverified.
        m_hasPersisted = false;
        m_backend->close();
        return false;
    }
    if (m_backend->selectFolder(false)) {
        QByteArray commit;
        const bool commitRead = m_backend->readBlob(QLatin1String(kCommitKey), commit);
        if (!commitRead || commit != QByteArray(kCommitValue)) {
            // No commit marker: absent session or interrupted/rolled-back
            // save. Never accept partial credentials as a session. A
            // non-marker value alone does NOT establish emptiness — residue
            // from an interrupted save may still be present — so stand the
            // cleanup duty down only when the credential entries themselves
            // are provably gone (listing and reads agree per key); a failed
            // read teaches nothing, so the duty stays armed.
            if (credentialEntriesAbsent()) {
                m_hasPersisted = false;
            }
            m_backend->close();
            return false;
        }
        // The commit marker proves a session was persisted here. Arm the
        // fail-closed cleanup duty now, before validating the remaining
        // fields: an unreadable access token must not leave a potentially
        // persisted session untracked.
        m_hasPersisted = true;
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
        // This instance is about to mutate persisted state, so arm the
        // fail-closed cleanup duty first: if any write below (or its
        // rollback) fails, credentials may remain behind, and a later
        // disabled clear must warn rather than claim success. Arming is
        // reset only by a verified clear.
        m_hasPersisted = true;
        // Invalidate any previous commit marker BEFORE writing new fields.
        // Otherwise a save over an existing session whose field writes fail
        // (and whose rollback removals also fail) would leave the old marker
        // in place and load() would accept the resulting mixed credentials.
        // If the old marker cannot be removed, abort without touching the
        // fields: the previous complete session (or nothing) remains, which
        // load() treats as before, never as a half-updated mix.
        if (!m_backend->remove(QLatin1String(kCommitKey))) {
            m_backend->close();
            return false;
        }
        const bool accessOk =
            m_backend->writeSecret(QLatin1String(kAccessKey), tokens.accessToken);
        const bool refreshOk =
            m_backend->writeSecret(QLatin1String(kRefreshKey), tokens.refreshToken);
        const bool loginOk = m_backend->writeSecret(QLatin1String(kLoginKey), tokens.login);
        const bool expiresOk = m_backend->writeBlob(
            QLatin1String(kExpiresKey), QByteArray::number(tokens.accessExpiresAtSecsSinceEpoch));
        // Commit only a fully written field set: the marker must never be
        // (re)written alongside failed fields.
        const bool fieldsOk = accessOk && refreshOk && loginOk && expiresOk;
        bool commitOk = false;
        if (fieldsOk) {
            commitOk =
                m_backend->writeBlob(QLatin1String(kCommitKey), QByteArray(kCommitValue));
        }
        ok = fieldsOk && commitOk;
        if (ok) {
            // Verify rather than trust the backend's write reports: re-read
            // every value and compare, so a backend that acknowledges
            // without persisting (silent write loss) cannot produce a
            // claimed success. Unverified saves roll back like failed ones.
            QString accessCheck;
            QString refreshCheck;
            QString loginCheck;
            QByteArray expiresCheck;
            QByteArray commitCheck;
            ok = m_backend->readSecret(QLatin1String(kAccessKey), accessCheck)
                && accessCheck == tokens.accessToken
                && m_backend->readSecret(QLatin1String(kRefreshKey), refreshCheck)
                && refreshCheck == tokens.refreshToken
                && m_backend->readSecret(QLatin1String(kLoginKey), loginCheck)
                && loginCheck == tokens.login
                && m_backend->readBlob(QLatin1String(kExpiresKey), expiresCheck)
                && expiresCheck == QByteArray::number(tokens.accessExpiresAtSecsSinceEpoch)
                && m_backend->readBlob(QLatin1String(kCommitKey), commitCheck)
                && commitCheck == QByteArray(kCommitValue);
        }
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
        // Service disabled: removal can no longer be verified. Fail closed
        // if this store instance has persisted credentials (a wallet can be
        // disabled after saving); only a store that never persisted has
        // provably nothing left to remove.
        return !m_hasPersisted;
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
        ok = true;
        ok = m_backend->remove(QLatin1String(kAccessKey)) && ok;
        ok = m_backend->remove(QLatin1String(kRefreshKey)) && ok;
        ok = m_backend->remove(QLatin1String(kExpiresKey)) && ok;
        ok = m_backend->remove(QLatin1String(kLoginKey)) && ok;
        ok = m_backend->remove(QLatin1String(kCommitKey)) && ok;
    }
    m_backend->close();
    if (ok) {
        m_hasPersisted = false;
    }
    return ok;
}
