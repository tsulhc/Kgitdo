#pragma once

#include <QObject>

#include "SecureStore.h"
#include "WalletBackend.h"

// KWallet-backed persistence (KF6). Tokens live in the wallet folder
// "Kgitdo"; nothing is written to plain files, QSettings, or logs.
// Save rolls back partial writes so a failed persist never leaves mixed
// credentials behind; clear reports failure when entries may remain, so
// callers never claim a logout they cannot verify.
// An injected backend is not owned (tests pass stack instances); when
// omitted, the store creates and owns a KWallet backend.
class KWalletSecureStore : public ISecureStore
{
    Q_OBJECT
public:
    explicit KWalletSecureStore(QObject *parent = nullptr, IWalletBackend *backend = nullptr);

    bool isAvailable() const override;
    QString backendName() const override;
    bool load(StoredTokens &out) override;
    bool save(const StoredTokens &tokens) override;
    bool clear() override;

private:
    IWalletBackend *m_backend = nullptr;
    // Unknown-unless-verified-empty: true until this instance establishes
    // that no credentials are persisted — a verified clear, an absent
    // folder, or a folder provably carrying no commit marker. Starts true
    // because credentials from an earlier run may exist even if this
    // process never saved: a disabled/unopenable wallet (or an
    // unselectable folder) teaches nothing, so a later disabled clear must
    // warn rather than claim success. Reset only by a verified clear (or by
    // establishing emptiness as above); re-armed by any save attempt and by
    // observing a commit marker, so cleanup fails closed whenever
    // credentials may remain but removal can no longer be verified.
    bool m_hasPersisted = true;
};
