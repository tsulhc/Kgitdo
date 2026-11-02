#include "AsyncKWalletStore.h"

#include <KWallet>
#include <QMetaObject>

#include "KWalletSecureStore.h"

AsyncKWalletStore::AsyncKWalletStore(QObject *parent)
    : ISecureStore(parent)
{
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.start();
}

AsyncKWalletStore::~AsyncKWalletStore()
{
    m_thread.quit();
    m_thread.wait(); // Never destroy a running worker or lose queued wallet cleanup.
}

bool AsyncKWalletStore::isAvailable() const
{
    return KWallet::Wallet::isEnabled();
}

void AsyncKWalletStore::loadAsync(std::function<void(bool, StoredTokens)> done)
{
    QMetaObject::invokeMethod(m_worker, [this, done = std::move(done)]() mutable {
        KWalletSecureStore store;
        StoredTokens tokens;
        const bool loaded = store.load(tokens);
        QMetaObject::invokeMethod(this, [done = std::move(done), loaded, tokens = std::move(tokens)]() mutable {
            done(loaded, std::move(tokens));
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void AsyncKWalletStore::saveAsync(const StoredTokens &tokens, std::function<void(bool)> done)
{
    QMetaObject::invokeMethod(m_worker, [this, tokens, done = std::move(done)]() mutable {
        KWalletSecureStore store;
        const bool saved = store.save(tokens);
        QMetaObject::invokeMethod(this, [done = std::move(done), saved]() mutable {
            done(saved);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void AsyncKWalletStore::clearAsync(std::function<void(bool)> done)
{
    QMetaObject::invokeMethod(m_worker, [this, done = std::move(done)]() mutable {
        KWalletSecureStore store;
        const bool cleared = store.clear();
        QMetaObject::invokeMethod(this, [done = std::move(done), cleared]() mutable {
            done(cleared);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
