#include <QMap>
#include <QSet>
#include <QStringList>
#include <QtTest/QtTest>

#include "KWalletSecureStore.h"
#include "WalletBackend.h"

// Scripted in-memory backend: exercises KWalletSecureStore orchestration
// (rollback on partial save, honest clear results) without a wallet daemon.
class FakeBackend : public IWalletBackend
{
public:
    bool enabled = true;
    bool hasFolder = false;
    bool failOpen = false;
    bool failSelect = false;
    int selectCalls = 0;
    QMap<QString, QByteArray> entries;
    QSet<QString> failingWrites;
    QSet<QString> failingRemoves;
    // Simulates wallet read errors: reads of these keys fail even when the
    // entry exists, mirroring a wallet that errors mid-operation.
    QSet<QString> failingReads;
    // Simulates a stale folder listing: these keys are reported as present
    // even after removal, mirroring a wallet whose entry list lags reads.
    QSet<QString> staleListed;
    QStringList removedKeys;
    bool wasOpened = false;

    bool isEnabled() const override
    {
        return enabled;
    }

    bool open() override
    {
        if (!enabled || failOpen) {
            return false;
        }
        wasOpened = true;
        return true;
    }

    void close() override
    {
    }

    bool selectFolder(bool createIfMissing) override
    {
        ++selectCalls;
        if (failSelect) {
            return false;
        }
        if (!hasFolder) {
            if (!createIfMissing) {
                return false;
            }
            hasFolder = true;
        }
        return true;
    }

    bool folderExists() override
    {
        return hasFolder;
    }

    bool readSecret(const QString &key, QString &value) override
    {
        if (failingReads.contains(key) || !entries.contains(key)) {
            return false;
        }
        value = QString::fromUtf8(entries.value(key));
        return true;
    }

    bool readBlob(const QString &key, QByteArray &value) override
    {
        if (failingReads.contains(key) || !entries.contains(key)) {
            return false;
        }
        value = entries.value(key);
        return true;
    }

    bool writeSecret(const QString &key, const QString &value) override
    {
        if (failingWrites.contains(key)) {
            return false;
        }
        entries.insert(key, value.toUtf8());
        return true;
    }

    bool writeBlob(const QString &key, const QByteArray &value) override
    {
        if (failingWrites.contains(key)) {
            return false;
        }
        entries.insert(key, value);
        return true;
    }

    bool remove(const QString &key) override
    {
        // Simulates the KWallet primitives and interprets them with the same
        // production policy, so policy regressions fail here too.
        removedKeys << key;
        const bool removeSucceeded = !failingRemoves.contains(key);
        if (removeSucceeded) {
            entries.remove(key);
        }
        const bool listed = entries.contains(key) || staleListed.contains(key);
        QString secret;
        QByteArray blob;
        const bool secretReadable = readSecret(key, secret);
        const bool blobReadable = readBlob(key, blob);
        return WalletRemovePolicy::confirmRemoved(removeSucceeded, listed, secretReadable,
                                                  blobReadable);
    }
};

static StoredTokens makeTokens()
{
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("ghu_saved");
    tokens.refreshToken = QStringLiteral("ghr_saved");
    tokens.accessExpiresAtSecsSinceEpoch = 1700000000;
    tokens.login = QStringLiteral("octocat");
    return tokens;
}

static StoredTokens makeTokensAlt()
{
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("ghu_rotated");
    tokens.refreshToken = QStringLiteral("ghr_rotated");
    tokens.accessExpiresAtSecsSinceEpoch = 1800000000;
    tokens.login = QStringLiteral("monalisa");
    return tokens;
}

class WalletStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void saveLoadRoundTrip();
    void saveEmptyAccessFails();
    void savePartialFailureRollsBack();
    void saveCommitWriteFailureLeavesUnloadableState();
    void saveStaleMarkerInvalidatedBeforeFields();
    void saveMarkerRemovalFailureAbortsKeepingPreviousSession();
    void saveOpenFailureReportsFailure();
    void clearRemovesEverything();
    void clearWithoutFolderReportsSuccess();
    void clearWithoutFolderIgnoresSelectFailure();
    void clearFolderSelectFailureReportsFailure();
    void clearOpenFailureReportsFailure();
    void clearRemoveFailureReportsFailure();
    void clearApiFailureWithUnreadableStateReportsFailure();
    void clearStaleListingWithUnreadableStateReportsFailure();
    void clearAbsentKeysDespiteApiErrorsReportsSuccess();
    void clearDisabledAfterSaveReportsFailure();
    void removePolicyDecisionTable();
    void disabledBackendLoadSaveFailClearSucceeds();
};

void WalletStoreTest::saveLoadRoundTrip()
{
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.isAvailable());
    QVERIFY(store.save(makeTokens()));
    StoredTokens loaded;
    QVERIFY(store.load(loaded));
    QCOMPARE(loaded.accessToken, QStringLiteral("ghu_saved"));
    QCOMPARE(loaded.refreshToken, QStringLiteral("ghr_saved"));
    QCOMPARE(loaded.accessExpiresAtSecsSinceEpoch, 1700000000);
    QCOMPARE(loaded.login, QStringLiteral("octocat"));
}

void WalletStoreTest::saveEmptyAccessFails()
{
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    StoredTokens tokens;
    QVERIFY(!store.save(tokens));
    QVERIFY(!backend.wasOpened);
}

void WalletStoreTest::savePartialFailureRollsBack()
{
    // Regression: save() used to leave earlier entries behind when a later
    // write failed, so a later launch could pick up mixed credentials.
    FakeBackend backend;
    backend.failingWrites.insert(QStringLiteral("accessExpiresAt"));
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(!store.save(makeTokens()));
    // Rollback attempted every entry, and nothing usable remains.
    QVERIFY(backend.removedKeys.contains(QStringLiteral("accessToken")));
    QVERIFY(backend.removedKeys.contains(QStringLiteral("refreshToken")));
    QVERIFY(backend.removedKeys.contains(QStringLiteral("accessExpiresAt")));
    QVERIFY(backend.removedKeys.contains(QStringLiteral("login")));
    QVERIFY(backend.removedKeys.contains(QStringLiteral("committed")));
    QVERIFY(!backend.entries.contains(QStringLiteral("accessToken")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::saveCommitWriteFailureLeavesUnloadableState()
{
    // Regression: rollback ignored removal results, so a surviving access
    // token from a failed save was still accepted by load(). The commit
    // marker (written last, required by load) closes that hole: the save
    // fails and the leftovers are unloadable even when removal also fails.
    FakeBackend backend;
    backend.failingWrites.insert(QStringLiteral("committed"));
    backend.failingRemoves.insert(QStringLiteral("accessToken"));
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(!store.save(makeTokens()));
    // The access token genuinely survived the failed rollback...
    QVERIFY(backend.entries.contains(QStringLiteral("accessToken")));
    // ...yet load() must not accept it without the commit marker.
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::saveStaleMarkerInvalidatedBeforeFields()
{
    // Regression: save() wrote new fields before refreshing the commit
    // marker, so a save over an existing session whose field writes failed
    // (and whose rollback removals also failed) left the OLD marker in place
    // and load() accepted the resulting mixed credentials. The marker is now
    // invalidated first and rewritten only after a fully written field set.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    StoredTokens check;
    QVERIFY(store.load(check));
    QCOMPARE(check.login, QStringLiteral("octocat"));

    backend.failingWrites.insert(QStringLiteral("refreshToken"));
    backend.failingRemoves.insert(QStringLiteral("accessToken"));
    QVERIFY(!store.save(makeTokensAlt()));
    // The new access token genuinely survived the failed rollback...
    QVERIFY(backend.entries.contains(QStringLiteral("accessToken")));
    QCOMPARE(backend.entries.value(QStringLiteral("accessToken")),
             QByteArray("ghu_rotated"));
    // ...yet without a commit marker the leftovers are unloadable: neither
    // the old session nor a half-rotated mix may be accepted.
    QVERIFY(!backend.entries.contains(QStringLiteral("committed")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::saveMarkerRemovalFailureAbortsKeepingPreviousSession()
{
    // When the old marker itself cannot be removed, save() aborts before
    // touching any field: the previous complete session stays intact (still
    // loadable, never mixed), while the caller sees failure and AuthSession
    // keeps the fresh tokens volatile for this process only.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    backend.failingRemoves.insert(QStringLiteral("committed"));
    QVERIFY(!store.save(makeTokensAlt()));
    StoredTokens loaded;
    QVERIFY(store.load(loaded));
    QCOMPARE(loaded.login, QStringLiteral("octocat"));
    QCOMPARE(loaded.accessToken, QStringLiteral("ghu_saved"));
}

void WalletStoreTest::saveOpenFailureReportsFailure()
{
    FakeBackend backend;
    backend.failOpen = true;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(!store.save(makeTokens()));
}

void WalletStoreTest::clearRemovesEverything()
{
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    QVERIFY(store.clear());
    QVERIFY(!backend.entries.contains(QStringLiteral("committed")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::clearWithoutFolderReportsSuccess()
{
    // Nothing was ever persisted: clearing an empty store is a no-op success.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.clear());
    QVERIFY(backend.removedKeys.isEmpty());
}

void WalletStoreTest::clearWithoutFolderIgnoresSelectFailure()
{
    // Absent folder means nothing is persisted, even if selection would fail.
    FakeBackend backend;
    backend.failSelect = true;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.clear());
    QCOMPARE(backend.selectCalls, 0);
}

void WalletStoreTest::clearFolderSelectFailureReportsFailure()
{
    // Regression: clear() conflated "folder absent" with "folder exists but
    // unselectable" and reported success while credentials may have remained.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    QVERIFY(backend.hasFolder);
    backend.failSelect = true;
    QVERIFY(!store.clear());
}

void WalletStoreTest::clearOpenFailureReportsFailure()
{
    // Regression: clear() used to report success when the wallet could not
    // even be opened, so logout claimed more than it achieved.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    backend.failOpen = true;
    QVERIFY(!store.clear());
}

void WalletStoreTest::clearRemoveFailureReportsFailure()
{
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    backend.failingRemoves.insert(QStringLiteral("accessToken"));
    QVERIFY(!store.clear());
    // The entry physically survived, but without the commit marker (removed
    // with the rest) it is unloadable: no session can be resurrected from
    // the leftovers, and the caller must warn and offer retry.
    QVERIFY(backend.entries.contains(QStringLiteral("accessToken")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::clearApiFailureWithUnreadableStateReportsFailure()
{
    // Regression (R7): the production backend ignored removeEntry's return
    // code and treated unreadability as proof of absence, so an API failure
    // combined with read errors was reported as successfully removed. The
    // policy is fail-closed: a reported failure with the entry still listed
    // is a failure even when reads also error.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    backend.failingRemoves.insert(QStringLiteral("accessToken"));
    backend.failingReads.insert(QStringLiteral("accessToken"));
    QVERIFY(!store.clear());
    QVERIFY(backend.entries.contains(QStringLiteral("accessToken")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::clearStaleListingWithUnreadableStateReportsFailure()
{
    // Regression: the success branch ignored the folder listing, so a stale
    // API success code with the key still listed (and reads erroring) was
    // reported as removed. Removal is confirmed only when the key is
    // delisted AND unreadable.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    backend.staleListed.insert(QStringLiteral("accessToken"));
    backend.failingReads.insert(QStringLiteral("accessToken"));
    backend.failingReads.insert(QStringLiteral("refreshToken"));
    backend.failingReads.insert(QStringLiteral("accessExpiresAt"));
    backend.failingReads.insert(QStringLiteral("login"));
    backend.failingReads.insert(QStringLiteral("committed"));
    QVERIFY(!store.clear());
    // The entry is physically gone, but the stale listing means absence is
    // unverified: the caller must warn and offer retry, not claim success.
    QVERIFY(!backend.entries.contains(QStringLiteral("accessToken")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
}

void WalletStoreTest::clearAbsentKeysDespiteApiErrorsReportsSuccess()
{
    // Absence tolerance: removing keys that are not there (e.g. the commit
    // marker on a first save, or clearing an empty folder) succeeds even
    // when the API reports an error, as long as absence is confirmed.
    FakeBackend backend;
    backend.hasFolder = true;
    backend.failingRemoves.insert(QStringLiteral("accessToken"));
    backend.failingRemoves.insert(QStringLiteral("refreshToken"));
    backend.failingRemoves.insert(QStringLiteral("accessExpiresAt"));
    backend.failingRemoves.insert(QStringLiteral("login"));
    backend.failingRemoves.insert(QStringLiteral("committed"));
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.clear());
}

void WalletStoreTest::clearDisabledAfterSaveReportsFailure()
{
    // Regression: clear() trusted a disabled wallet. A wallet can be
    // disabled after saving, so a store that has persisted credentials must
    // fail closed once removal can no longer be verified. Re-enabling
    // restores the verifiable (and retryable) path.
    FakeBackend backend;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(store.save(makeTokens()));
    backend.enabled = false;
    QVERIFY(!store.isAvailable());
    QVERIFY(!store.clear());
    backend.enabled = true;
    QVERIFY(store.clear());
    // The verified clear reset the persisted flag: disabling again is clean.
    backend.enabled = false;
    QVERIFY(store.clear());
}

void WalletStoreTest::removePolicyDecisionTable()
{
    using WalletRemovePolicy::confirmRemoved;
    // Reported success, unreadable: gone.
    QVERIFY(confirmRemoved(true, false, false, false));
    // Reported success but still readable: silent non-removal.
    QVERIFY(!confirmRemoved(true, true, true, true));
    QVERIFY(!confirmRemoved(true, false, true, false));
    QVERIFY(!confirmRemoved(true, false, false, true));
    // Reported failure but confirmed absent (not listed, unreadable): nothing
    // to remove.
    QVERIFY(confirmRemoved(false, false, false, false));
    // Reported failure with the entry still listed: failure, even when reads
    // also error (read errors never prove absence).
    QVERIFY(!confirmRemoved(false, true, false, false));
    QVERIFY(!confirmRemoved(false, true, true, true));
    // Reported success with the key still listed but unreadable: a stale
    // success code must not overstate removal.
    QVERIFY(!confirmRemoved(true, true, false, false));
    // Reported failure, unlisted, but still readable: contradictory state,
    // fail closed.
    QVERIFY(!confirmRemoved(false, false, true, false));
}

void WalletStoreTest::disabledBackendLoadSaveFailClearSucceeds()
{
    FakeBackend backend;
    backend.enabled = false;
    KWalletSecureStore store(nullptr, &backend);
    QVERIFY(!store.isAvailable());
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
    QVERIFY(!store.save(makeTokens()));
    // Service absent: nothing could have been persisted here.
    QVERIFY(store.clear());
    QVERIFY(!backend.wasOpened);
}

QTEST_MAIN(WalletStoreTest)
#include "tst_walletstore.moc"
