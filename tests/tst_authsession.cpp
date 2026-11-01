#include <QtTest/QtTest>

#include "AuthSession.h"
#include "SecureStore.h"

class FakeAuthNetwork : public IAuthNetwork
{
    Q_OBJECT
public:
    explicit FakeAuthNetwork(QObject *parent = nullptr)
        : IAuthNetwork(parent)
    {
    }

    QList<DeviceCodeResult> deviceQueue;
    QList<TokenResult> pollQueue;
    QList<TokenResult> refreshQueue;
    QList<UserResult> userQueue;

    int deviceRequests = 0;
    int pollRequests = 0;
    int refreshRequests = 0;
    int userRequests = 0;
    QString lastDeviceCode;
    QString lastRefreshToken;
    QString lastUserToken;

    static DeviceCodeResult makeDeviceOk()
    {
        DeviceCodeResult r;
        r.ok = true;
        r.data.deviceCode = QStringLiteral("dev-code-123");
        r.data.userCode = QStringLiteral("WDJB-MJHT");
        r.data.verificationUri = QStringLiteral("https://github.com/login/device");
        r.data.expiresInSeconds = 900;
        r.data.intervalSeconds = 5;
        return r;
    }

    static TokenResult makePollPending()
    {
        TokenResult r;
        r.ok = false;
        r.data.hasToken = false;
        r.data.errorCode = QStringLiteral("authorization_pending");
        return r;
    }

    static TokenResult makePollSlowDown(int interval = 10)
    {
        TokenResult r;
        r.ok = false;
        r.data.hasToken = false;
        r.data.errorCode = QStringLiteral("slow_down");
        r.data.intervalSeconds = interval;
        return r;
    }

    static TokenResult makePollError(const QString &code)
    {
        TokenResult r;
        r.ok = false;
        r.data.hasToken = false;
        r.data.errorCode = code;
        return r;
    }

    static TokenResult makeTokenOk(const QString &access = QStringLiteral("ghu_new"),
                                   const QString &refresh = QStringLiteral("ghr_new"))
    {
        TokenResult r;
        r.ok = true;
        r.data.hasToken = true;
        r.data.accessToken = access;
        r.data.refreshToken = refresh;
        r.data.expiresInSeconds = 28800;
        r.data.refreshExpiresInSeconds = 15897600;
        return r;
    }

    static TokenResult makeTransportError()
    {
        TokenResult r;
        r.ok = false;
        r.transportError = QStringLiteral("fake network down");
        return r;
    }

    static UserResult makeUserOk(const QString &login = QStringLiteral("octocat"))
    {
        UserResult r;
        r.ok = true;
        r.login = login;
        r.httpStatus = 200;
        return r;
    }

    static UserResult makeUserUnauthorized()
    {
        UserResult r;
        r.ok = false;
        r.httpStatus = 401;
        r.unauthorized = true;
        r.errorString = QStringLiteral("revoked");
        return r;
    }

    void requestDeviceCode(const QString &clientId, std::function<void(DeviceCodeResult)> cb) override
    {
        Q_UNUSED(clientId);
        ++deviceRequests;
        if (deviceQueue.isEmpty()) {
            DeviceCodeResult r;
            r.ok = false;
            r.errorString = QStringLiteral("fake network down");
            cb(r);
            return;
        }
        cb(deviceQueue.takeFirst());
    }

    void pollAccessToken(const QString &clientId, const QString &deviceCode,
                         std::function<void(TokenResult)> cb) override
    {
        Q_UNUSED(clientId);
        ++pollRequests;
        lastDeviceCode = deviceCode;
        if (pollQueue.isEmpty()) {
            cb(makeTransportError());
            return;
        }
        cb(pollQueue.takeFirst());
    }

    void refreshAccessToken(const QString &clientId, const QString &refreshToken,
                            std::function<void(TokenResult)> cb) override
    {
        Q_UNUSED(clientId);
        ++refreshRequests;
        lastRefreshToken = refreshToken;
        if (refreshQueue.isEmpty()) {
            cb(makeTransportError());
            return;
        }
        cb(refreshQueue.takeFirst());
    }

    void fetchUserLogin(const QString &accessToken, std::function<void(UserResult)> cb) override
    {
        ++userRequests;
        lastUserToken = accessToken;
        if (userQueue.isEmpty()) {
            UserResult r;
            r.ok = false;
            r.errorString = QStringLiteral("fake user down");
            cb(r);
            return;
        }
        cb(userQueue.takeFirst());
    }
};

// Test double simulating a wallet whose entries cannot be removed
// (locked wallet, removal failure): persistence operations fail the
// way FailingSecureStore models absence, but clear() reports failure.
class ClearFailingStore : public ISecureStore
{
public:
    explicit ClearFailingStore(QObject *parent = nullptr)
        : ISecureStore(parent)
    {
    }

    bool failClear = true;

    bool isAvailable() const override
    {
        return true;
    }
    QString backendName() const override
    {
        return QStringLiteral("clear-failing");
    }
    bool load(StoredTokens &out) override
    {
        return m_inner.load(out);
    }
    bool save(const StoredTokens &tokens) override
    {
        return m_inner.save(tokens);
    }
    bool clear() override
    {
        return !failClear;
    }

private:
    InMemorySecureStore m_inner;
};

class AuthSessionTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void loginSuccessAfterPending();
    void slowDownIncreasesInterval();
    void expiredCodeFails();
    void accessDeniedFails();
    void doubleStartIsIgnored();
    void cancelPreventsFurtherPoll();
    void refreshSuccessKeepsSession();
    void nearExpiryPreventsStaleApiToken();
    void badRefreshTokenRequiresRelogin();
    void restoreValidSession();
    void restoreExpiredRefreshes();
    void restoreWithoutTokensStaysSignedOut();
    void logoutClearsStoreAndMemory();
    void walletRefusalFallsBackToVolatile();
    void userUnauthorizedClearsSession();
    void missingClientIdFails();
    void networkErrorDuringPollIsRetryable();
    void logoutWithUnremovableWalletWarns();
    void failedClearSetsFlagAndRetrySucceeds();
    void retryClearWhileFailingKeepsWarning();
    void revokedSessionWithUnremovableWalletWarns();
    void badRefreshWithUnremovableWalletWarns();
};

void AuthSessionTest::loginSuccessAfterPending()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makePollPending() << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk(QStringLiteral("octocat"));

    session.startLogin();
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);
    QCOMPARE(session.userCode(), QStringLiteral("WDJB-MJHT"));

    session.pollOnce(); // pending -> still awaiting
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);

    session.pollOnce(); // token + user -> authenticated
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(session.userLogin(), QStringLiteral("octocat"));
    QVERIFY(!session.isVolatileSession());

    StoredTokens stored;
    QVERIFY(store.load(stored));
    QCOMPARE(stored.accessToken, QStringLiteral("ghu_new"));
    QCOMPARE(stored.login, QStringLiteral("octocat"));
}

void AuthSessionTest::slowDownIncreasesInterval()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makePollSlowDown(10) << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    QCOMPARE(session.pollIntervalSeconds(), 5);
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);
    QCOMPARE(session.pollIntervalSeconds(), 10);
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);
}

void AuthSessionTest::expiredCodeFails()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makePollError(QStringLiteral("expired_token"));

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Failed);
    QVERIFY(session.errorMessage().contains(QStringLiteral("expired"), Qt::CaseInsensitive));
    StoredTokens stored;
    QVERIFY(!store.load(stored));
}

void AuthSessionTest::accessDeniedFails()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makePollError(QStringLiteral("access_denied"));

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Failed);
    QVERIFY(session.errorMessage().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
}

void AuthSessionTest::doubleStartIsIgnored()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();

    session.startLogin();
    const int firstRequests = net.deviceRequests;
    session.startLogin(); // awaiting already: must be ignored
    QCOMPARE(net.deviceRequests, firstRequests);
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);
}

void AuthSessionTest::cancelPreventsFurtherPoll()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.cancelLogin();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    session.pollOnce();
    QCOMPARE(net.pollRequests, 0);
    QCOMPARE(session.status(), AuthSession::SignedOut);
}

void AuthSessionTest::refreshSuccessKeepsSession()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk(QStringLiteral("ghu_one"), QStringLiteral("ghr_one"));
    net.userQueue << FakeAuthNetwork::makeUserOk() << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);

    net.refreshQueue << FakeAuthNetwork::makeTokenOk(QStringLiteral("ghu_two"), QStringLiteral("ghr_two"));
    session.refreshSession();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(session.accessTokenForTesting(), QStringLiteral("ghu_two"));
    StoredTokens stored;
    QVERIFY(store.load(stored));
    QCOMPARE(stored.accessToken, QStringLiteral("ghu_two"));
}

void AuthSessionTest::nearExpiryPreventsStaleApiToken()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    auto nearExpiry = FakeAuthNetwork::makeTokenOk(QStringLiteral("ghu_old"), QStringLiteral("ghr_old"));
    nearExpiry.data.expiresInSeconds = 30; // within the 60-second safety margin
    net.pollQueue << nearExpiry;
    net.userQueue << FakeAuthNetwork::makeUserOk() << FakeAuthNetwork::makeUserOk();
    net.refreshQueue << FakeAuthNetwork::makeTokenOk(QStringLiteral("ghu_fresh"), QStringLiteral("ghr_fresh"));

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QVERIFY(!session.ensureFreshAccessToken());
    QCOMPARE(net.refreshRequests, 1);
    QCOMPARE(net.lastRefreshToken, QStringLiteral("ghr_old"));
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(session.currentAccessToken(), QStringLiteral("ghu_fresh"));
    QVERIFY(session.ensureFreshAccessToken());
    QCOMPARE(net.refreshRequests, 1);
}

void AuthSessionTest::badRefreshTokenRequiresRelogin()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);

    IAuthNetwork::TokenResult bad;
    bad.ok = false;
    bad.data.hasToken = false;
    bad.data.errorCode = QStringLiteral("bad_refresh_token");
    net.refreshQueue << bad;
    session.refreshSession();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.errorMessage().contains(QStringLiteral("sign in"), Qt::CaseInsensitive));
    StoredTokens stored;
    QVERIFY(!store.load(stored));
}

void AuthSessionTest::restoreValidSession()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    StoredTokens seed;
    seed.accessToken = QStringLiteral("ghu_saved");
    seed.refreshToken = QStringLiteral("ghr_saved");
    seed.accessExpiresAtSecsSinceEpoch = 0; // never expires
    seed.login = QStringLiteral("octocat");
    QVERIFY(store.save(seed));
    net.userQueue << FakeAuthNetwork::makeUserOk(QStringLiteral("octocat"));

    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    session.restore();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(session.userLogin(), QStringLiteral("octocat"));
}

void AuthSessionTest::restoreExpiredRefreshes()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    StoredTokens seed;
    seed.accessToken = QStringLiteral("ghu_old");
    seed.refreshToken = QStringLiteral("ghr_old");
    seed.accessExpiresAtSecsSinceEpoch = QDateTime::currentDateTimeUtc().toSecsSinceEpoch() - 1000;
    seed.login = QStringLiteral("octocat");
    QVERIFY(store.save(seed));
    net.refreshQueue << FakeAuthNetwork::makeTokenOk(QStringLiteral("ghu_fresh"), QStringLiteral("ghr_fresh"));
    net.userQueue << FakeAuthNetwork::makeUserOk(QStringLiteral("octocat"));

    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    session.restore();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(session.accessTokenForTesting(), QStringLiteral("ghu_fresh"));
}

void AuthSessionTest::restoreWithoutTokensStaysSignedOut()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    session.restore();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QCOMPARE(net.userRequests, 0);
    QCOMPARE(net.refreshRequests, 0);
}

void AuthSessionTest::logoutClearsStoreAndMemory()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    session.logout();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.userLogin().isEmpty());
    QVERIFY(session.accessTokenForTesting().isEmpty());
    StoredTokens stored;
    QVERIFY(!store.load(stored));
}

void AuthSessionTest::walletRefusalFallsBackToVolatile()
{
    FakeAuthNetwork net;
    FailingSecureStore failing;
    AuthSession session(&net, &failing);
    session.setClientId(QStringLiteral("test-client"));
    QVERIFY(!session.hasSecureStorage());
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QVERIFY(session.isVolatileSession());
}

void AuthSessionTest::userUnauthorizedClearsSession()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserUnauthorized();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.errorMessage().contains(QStringLiteral("revoked"), Qt::CaseInsensitive));
    StoredTokens stored;
    QVERIFY(!store.load(stored));
}

void AuthSessionTest::missingClientIdFails()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QString());
    session.startLogin();
    QCOMPARE(session.status(), AuthSession::Failed);
    QCOMPARE(net.deviceRequests, 0);
    QVERIFY(session.errorMessage().contains(QStringLiteral("KGITDO_GITHUB_CLIENT_ID")));
}

void AuthSessionTest::networkErrorDuringPollIsRetryable()
{
    FakeAuthNetwork net;
    InMemorySecureStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTransportError();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Failed);
    QVERIFY(!session.errorMessage().isEmpty());
    // Retry starts a fresh device flow instead of hot-looping.
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    session.retry();
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);
    QCOMPARE(net.deviceRequests, 2);
}

void AuthSessionTest::logoutWithUnremovableWalletWarns()
{
    // Regression: logout claimed success even when stored credentials could
    // not be removed. Memory must still be cleared, but the UI must warn.
    FakeAuthNetwork net;
    ClearFailingStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    session.logout();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.userLogin().isEmpty());
    QVERIFY(session.accessTokenForTesting().isEmpty());
    QVERIFY(session.clearFailed());
    QVERIFY(session.errorMessage().contains(QStringLiteral("could not be removed"), Qt::CaseInsensitive));
}

void AuthSessionTest::failedClearSetsFlagAndRetrySucceeds()
{
    // R8: a failed logout cleanup must leave a retryable state, not just a
    // message. retryClear() retries the removal and clears the flag and the
    // warning once the store cooperates, without touching session state.
    FakeAuthNetwork net;
    ClearFailingStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    session.logout();
    QVERIFY(session.clearFailed());
    QVERIFY(!session.errorMessage().isEmpty());

    store.failClear = false;
    session.retryClear();
    QVERIFY(!session.clearFailed());
    QVERIFY(session.errorMessage().isEmpty());
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.accessTokenForTesting().isEmpty());
}

void AuthSessionTest::retryClearWhileFailingKeepsWarning()
{
    // While the store still refuses removal, retryClear() must keep both
    // the flag and the warning so the retry affordance stays visible.
    FakeAuthNetwork net;
    ClearFailingStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    session.logout();
    QVERIFY(session.clearFailed());
    session.retryClear();
    QVERIFY(session.clearFailed());
    QVERIFY(session.errorMessage().contains(QStringLiteral("could not be removed"), Qt::CaseInsensitive));
}

void AuthSessionTest::revokedSessionWithUnremovableWalletWarns()
{
    FakeAuthNetwork net;
    ClearFailingStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserUnauthorized();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.clearFailed());
    QVERIFY(session.errorMessage().contains(QStringLiteral("revoked"), Qt::CaseInsensitive));
    QVERIFY(session.errorMessage().contains(QStringLiteral("could not be removed"), Qt::CaseInsensitive));
}

void AuthSessionTest::badRefreshWithUnremovableWalletWarns()
{
    FakeAuthNetwork net;
    ClearFailingStore store;
    AuthSession session(&net, &store);
    session.setClientId(QStringLiteral("test-client"));
    net.deviceQueue << FakeAuthNetwork::makeDeviceOk();
    net.pollQueue << FakeAuthNetwork::makeTokenOk();
    net.userQueue << FakeAuthNetwork::makeUserOk();

    session.startLogin();
    session.pollOnce();
    QCOMPARE(session.status(), AuthSession::Authenticated);

    IAuthNetwork::TokenResult bad;
    bad.ok = false;
    bad.data.hasToken = false;
    bad.data.errorCode = QStringLiteral("bad_refresh_token");
    net.refreshQueue << bad;
    session.refreshSession();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QVERIFY(session.clearFailed());
    QVERIFY(session.errorMessage().contains(QStringLiteral("sign in"), Qt::CaseInsensitive));
    QVERIFY(session.errorMessage().contains(QStringLiteral("could not be removed"), Qt::CaseInsensitive));
}

QTEST_MAIN(AuthSessionTest)
#include "tst_authsession.moc"
