#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Minimal wallet backend contract so KWalletSecureStore can guarantee
// atomic-feeling saves (rollback on partial failure) and honest clear()
// results without requiring a running wallet daemon in unit tests.
// Production uses KWalletBackend; tests inject scripted fakes.
//
// Write/remove operations report success only when the key holds (or no
// longer holds) the expected value afterwards; implementations must verify
// rather than trust a single write/remove call.
// (No Q_OBJECT: the contract carries no signals; QObject parenting only.)
class IWalletBackend : public QObject
{
public:
    explicit IWalletBackend(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual bool isEnabled() const = 0;
    // Opens the wallet. False: nothing was touched.
    virtual bool open() = 0;
    virtual void close() = 0;
    // Selects the Kgitdo folder, creating it when requested. False: the
    // folder is unavailable, so reads find nothing and writes must fail.
    virtual bool selectFolder(bool createIfMissing) = 0;
    virtual bool readSecret(const QString &key, QString &value) = 0;
    virtual bool readBlob(const QString &key, QByteArray &value) = 0;
    virtual bool writeSecret(const QString &key, const QString &value) = 0;
    virtual bool writeBlob(const QString &key, const QByteArray &value) = 0;
    // Removes the key; true only when the key is gone afterwards.
    // Removing an absent key succeeds (nothing remains).
    virtual bool remove(const QString &key) = 0;
};
