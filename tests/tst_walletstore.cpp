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
    bool folderExists = false;
    bool failOpen = false;
    bool failSelect = false;
    QMap<QString, QByteArray> entries;
    QSet<QString> failingWrites;
    QSet<QString> failingRemoves;
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
        if (failSelect) {
            return false;
        }
        if (!folderExists) {
            if (!createIfMissing) {
                return false;
            }
            folderExists = true;
        }
        return true;
    }

    bool readSecret(const QString &key, QString &value) override
    {
        if (!entries.contains(key)) {
            return false;
        }
        value = QString::fromUtf8(entries.value(key));
        return true;
    }

    bool readBlob(const QString &key, QByteArray &value) override
    {
        if (!entries.contains(key)) {
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
        removedKeys << key;
        if (failingRemoves.contains(key)) {
            return false;
        }
        entries.remove(key);
        return !entries.contains(key);
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

class WalletStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void saveLoadRoundTrip();
    void saveEmptyAccessFails();
    void savePartialFailureRollsBack();
    void saveOpenFailureReportsFailure();
    void clearRemovesEverything();
    void clearWithoutFolderReportsSuccess();
    void clearOpenFailureReportsFailure();
    void clearRemoveFailureReportsFailure();
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
    QVERIFY(!backend.entries.contains(QStringLiteral("accessToken")));
    StoredTokens loaded;
    QVERIFY(!store.load(loaded));
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
    // The surviving entry is still readable: callers must warn, not claim.
    StoredTokens loaded;
    QVERIFY(store.load(loaded));
    QCOMPARE(loaded.accessToken, QStringLiteral("ghu_saved"));
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
