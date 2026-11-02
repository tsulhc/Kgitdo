#pragma once

#include <QThread>

#include "SecureStore.h"

// Runs all KWallet operations serially on a dedicated Qt thread. The GUI
// never invokes the potentially blocking KWallet D-Bus open/save/load/clear.
// This is the production adapter; existing KWalletSecureStore remains the
// tested synchronous implementation and lives only on the worker thread.
class AsyncKWalletStore final : public ISecureStore
{
public:
    explicit AsyncKWalletStore(QObject *parent = nullptr);
    ~AsyncKWalletStore() override;

    bool isAvailable() const override;
    QString backendName() const override { return QStringLiteral("kwallet"); }

    // Deliberately unavailable: production callers must use the async API.
    bool load(StoredTokens &) override { return false; }
    bool save(const StoredTokens &) override { return false; }
    bool clear() override { return false; }

    void loadAsync(std::function<void(bool, StoredTokens)> done) override;
    void saveAsync(const StoredTokens &tokens, std::function<void(bool)> done) override;
    void clearAsync(std::function<void(bool)> done) override;

private:
    QThread m_thread;
    QObject *m_worker = nullptr;
};
