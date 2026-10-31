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
    // True once a save has been verified, until a verified clear. Lets
    // clear() fail closed when the wallet service disappears after a save:
    // a disabled backend can no longer prove entries are gone.
    bool m_hasPersisted = false;
};
