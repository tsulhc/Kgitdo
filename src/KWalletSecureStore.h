#pragma once

#include <QObject>

#include "SecureStore.h"

// KWallet-backed persistence (KF6). Tokens live in the wallet folder
// "Kgitdo"; nothing is written to plain files, QSettings, or logs.
// When the wallet service is disabled, absent, or refuses to open, every
// operation reports failure and AuthSession falls back to a volatile
// in-memory session with an explicit UI notice.
class KWalletSecureStore : public ISecureStore
{
    Q_OBJECT
public:
    explicit KWalletSecureStore(QObject *parent = nullptr);

    bool isAvailable() const override;
    QString backendName() const override;
    bool load(StoredTokens &out) override;
    bool save(const StoredTokens &tokens) override;
    bool clear() override;
};
