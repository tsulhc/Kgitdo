#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>

#include "AuthTransport.h"
#include "SecureStore.h"

// Owns the GitHub App device-flow state machine, in-memory tokens, and
// persistence through ISecureStore (KWallet in production, fakes in tests).
//
// Guarantees for W1:
//  - no token is logged or written outside the secure store;
//  - polling honors the server interval and slow_down backoff;
//  - stale callbacks after cancel/logout are ignored via attempt fencing;
//  - double activation (double click) cannot start two concurrent flows;
//  - logout clears local state; server-side revocation stays manual on
//    github.com (see openManageInstallationsPage()).
class AuthSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool isAuthenticated READ isAuthenticated NOTIFY statusChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QString userLogin READ userLogin NOTIFY userLoginChanged)
    Q_PROPERTY(QString userCode READ userCode NOTIFY deviceCodeChanged)
    Q_PROPERTY(QString verificationUri READ verificationUri NOTIFY deviceCodeChanged)
    Q_PROPERTY(int deviceExpiresInSeconds READ deviceExpiresInSeconds NOTIFY deviceCodeChanged)
    Q_PROPERTY(int pollIntervalSeconds READ pollIntervalSeconds NOTIFY pollIntervalChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool clientIdConfigured READ clientIdConfigured NOTIFY clientIdChanged)
    Q_PROPERTY(bool volatileSession READ isVolatileSession NOTIFY volatileSessionChanged)
    Q_PROPERTY(bool clearFailed READ clearFailed NOTIFY clearFailedChanged)
    Q_PROPERTY(bool hasSecureStorage READ hasSecureStorage NOTIFY secureStorageChanged)
    Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged)

public:
    enum Status {
        SignedOut,
        RequestingDeviceCode,
        AwaitingAuthorization,
        Refreshing,
        Authenticated,
        Failed
    };
    Q_ENUM(Status)

    explicit AuthSession(IAuthNetwork *network, ISecureStore *secureStore, QObject *parent = nullptr);

    Status status() const { return m_status; }
    bool isAuthenticated() const { return m_status == Authenticated; }
    bool pending() const { return m_pending; }
    QString userLogin() const { return m_userLogin; }
    QString userCode() const { return m_userCode; }
    QString verificationUri() const { return m_verificationUri; }
    int deviceExpiresInSeconds() const;
    int pollIntervalSeconds() const { return m_pollIntervalSeconds; }
    QString errorMessage() const { return m_errorMessage; }
    QString clientId() const { return m_clientId; }
    bool clientIdConfigured() const { return !m_clientId.isEmpty(); }
    bool isVolatileSession() const { return m_volatileSession; }
    // True when a logout/revocation cleanup could not remove the persisted
    // credentials. The UI offers retryClear() in that state; the flag clears
    // on a successful removal or a fresh successful persist.
    bool clearFailed() const { return m_clearFailed; }
    bool hasSecureStorage() const;
    // Test/debug accessors (no secrets exposed to QML).
    QString accessTokenForTesting() const { return m_accessToken; }
    int loginAttemptId() const { return m_attemptId; }
    // C++-only bearer token for API clients (GitHubClient). Plain method:
    // not a Q_PROPERTY/Q_INVOKABLE, so QML cannot reach it.
    QString currentAccessToken() const { return m_accessToken; }

    void setClientId(const QString &clientId);

    Q_INVOKABLE void startLogin();
    Q_INVOKABLE void cancelLogin();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void refreshSession();
    // Called by C++ API clients: starts a refresh instead of using a stale token.
    bool ensureFreshAccessToken();
    Q_INVOKABLE void logout();
    // Retries removing persisted credentials after a failed cleanup
    // (clearFailed). Clears the warning on success; keeps it otherwise.
    Q_INVOKABLE void retryClear();
    Q_INVOKABLE void restore();
    Q_INVOKABLE void openVerificationPage();
    Q_INVOKABLE void openManageInstallationsPage();

    // Drives one poll round. The internal timer calls it in production;
    // tests call it directly for deterministic stepping.
    void pollOnce();

Q_SIGNALS:
    void statusChanged();
    void pendingChanged();
    void userLoginChanged();
    void deviceCodeChanged();
    void pollIntervalChanged();
    void errorMessageChanged();
    void clientIdChanged();
    void volatileSessionChanged();
    void clearFailedChanged();
    void secureStorageChanged();

private:
    void setStatus(Status status);
    void setPending(bool pending);
    void setError(const QString &message);
    void clearDeviceState();
    void clearTokens();
    void beginPollTimer();
    void stopPollTimer();
    void scheduleAutomaticRefresh();
    bool isAccessExpired() const;
    qint64 nowSecs() const;
    void persistCurrentTokens();
    void setClearFailed(bool failed);
    // Clears the volatile fallback and the secure store. True when nothing
    // may remain: callers must warn instead of claiming a clean logout
    // when this returns false.
    bool clearPersistedTokens();
    void handleDeviceCodeResult(int attemptId, const IAuthNetwork::DeviceCodeResult &result);
    void handlePollResult(int attemptId, const IAuthNetwork::TokenResult &result);
    void handleRefreshResult(int attemptId, const IAuthNetwork::TokenResult &result);
    void fetchUserAndFinish(int attemptId);
    void handleUserResult(int attemptId, const IAuthNetwork::UserResult &result, bool fromRefresh);
    static QString friendlyPollError(const QString &code, const QString &description);
    static void openValidatedGitHubUrl(const QString &urlString);

    IAuthNetwork *m_network = nullptr;
    ISecureStore *m_secureStore = nullptr;
    InMemorySecureStore m_volatileStore;

    Status m_status = SignedOut;
    bool m_pending = false;
    QString m_clientId;
    QString m_accessToken;
    QString m_refreshToken;
    qint64 m_accessExpiresAtSecs = 0;
    QString m_userLogin;
    QString m_errorMessage;

    QString m_deviceCode;
    QString m_userCode;
    QString m_verificationUri;
    QDateTime m_deviceExpiresAt;
    int m_pollIntervalSeconds = 5;

    bool m_volatileSession = false;
    bool m_clearFailed = false;
    int m_attemptId = 0;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_refreshTimer = nullptr;
};
