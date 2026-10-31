#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Interpretation of one key-removal attempt's observable outcomes, shared by
// the production backend and test simulators so there is exactly one
// decision point to review. Fail-closed: an API-reported removal failure is
// a failure unless absence is confirmed by two agreeing oracles (not listed
// AND unreadable); an API-reported success is additionally verified by
// delisting AND unreadability, so a silent non-removal or a stale success
// code is still reported as a failure. Read errors alone never prove
// absence, and a listing alone never proves presence — both oracles must
// agree the key is gone.
namespace WalletRemovePolicy
{
inline bool confirmRemoved(bool removeSucceeded, bool hasEntry, bool secretReadable, bool blobReadable)
{
    if (!removeSucceeded) {
        return !hasEntry && !secretReadable && !blobReadable;
    }
    return !hasEntry && !secretReadable && !blobReadable;
}
} // namespace WalletRemovePolicy

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
    // (A false return does not say whether the folder is absent or merely
    // unselectable; use folderExists() when the distinction matters.)
    virtual bool selectFolder(bool createIfMissing) = 0;
    // True when the Kgitdo folder exists. Valid after open().
    virtual bool folderExists() = 0;
    // Listing oracle: true when the selected folder lists the key. Valid
    // after open() + selectFolder(). Used with the read primitives so two
    // agreeing oracles (not listed AND unreadable) must confirm absence
    // before cleanup duty is stood down; a listing alone never proves
    // presence and reads alone never prove absence.
    virtual bool hasEntry(const QString &key) = 0;
    virtual bool readSecret(const QString &key, QString &value) = 0;
    virtual bool readBlob(const QString &key, QByteArray &value) = 0;
    virtual bool writeSecret(const QString &key, const QString &value) = 0;
    virtual bool writeBlob(const QString &key, const QByteArray &value) = 0;
    // Removes the key; true only when the key is gone afterwards.
    // Removing an absent key succeeds (nothing remains).
    virtual bool remove(const QString &key) = 0;
};
