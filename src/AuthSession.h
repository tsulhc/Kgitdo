#pragma once

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <functional>

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
//
// Simplified access (W8): a single primary button starts the flow. When the
// device code arrives the session copies the user code to the clipboard
// (best effort: the code stays visible for manual copy) and opens the
// GitHub verification page automatically; the user approves there while
// this side polls. Manual Copy/Open buttons remain for no-browser or
// no-clipboard environments. The clipboard/browser effects are injectable
// so unit tests assert them without touching the real desktop.
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
    Q_PROPERTY(int pollAttempts READ pollAttempts NOTIFY pollProgressChanged)
    Q_PROPERTY(int lastPollHttpStatus READ lastPollHttpStatus NOTIFY pollProgressChanged)
    Q_PROPERTY(QString lastPollResult READ lastPollResult NOTIFY pollProgressChanged)
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

    // Clipboard copy of the device user code. Returns whether the copy was
    // performed. Production default writes the system clipboard (when one
    // exists); tests inject a recorder.
    using CopyHandler = std::function<bool(const QString &text)>;
    // Browser opening for validated github.com URLs. Mirrors
    // QDesktopServices::openUrl (true when handled). Production default
    // opens the URL; tests inject a recorder.
    using OpenHandler = std::function<bool(const QUrl &url)>;

    explicit AuthSession(IAuthNetwork *network, ISecureStore *secureStore, QObject *parent = nullptr);

    // Test seams: replace the production clipboard/browser actions with
    // recorders. Production keeps the in-class defaults below, so main.cpp
    // and existing call sites are untouched.

    Status status() const { return m_status; }
    bool isAuthenticated() const { return m_status == Authenticated; }
    bool pending() const { return m_pending; }
    QString userLogin() const { return m_userLogin; }
    QString userCode() const { return m_userCode; }
    QString verificationUri() const { return m_verificationUri; }
    int deviceExpiresInSeconds() const;
    int pollIntervalSeconds() const { return m_pollIntervalSeconds; }
    int pollAttempts() const { return m_pollAttempts; }
    int lastPollHttpStatus() const { return m_lastPollHttpStatus; }
    QString lastPollResult() const { return m_lastPollResult; }
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
    // Manual re-copy of the current device user code (waiting screen
    // "Copy code" action). No-op while no code is pending.
    Q_INVOKABLE void copyDeviceCodeToClipboard();
    void setCopyHandler(CopyHandler handler) { m_copyHandler = handler; }
    void setOpenHandler(OpenHandler handler) { m_openHandler = handler; }

    // Drives one poll round. The internal timer calls it in production;
    // tests call it directly for deterministic stepping.
    void pollOnce();

Q_SIGNALS:
    void statusChanged();
    void pendingChanged();
    void userLoginChanged();
    void deviceCodeChanged();
    void pollIntervalChanged();
    void pollProgressChanged();
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
    void expireDeviceCode();
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
    // Instance method so tests can inject an OpenHandler recording the URL
    // instead of launching a browser.
    void openValidatedGitHubUrl(const QString &urlString);

    IAuthNetwork *m_network = nullptr;
    ISecureStore *m_secureStore = nullptr;
    InMemorySecureStore m_volatileStore;
    // W8 seams: production defaults copy to the system clipboard and open
    // the URL in the browser; tests inject recording handlers via the
    // setters above. In-class defaults keep every existing 3-arg call site
    // (main.cpp, tests) untouched.
    CopyHandler m_copyHandler = [](const QString &text) {
        QClipboard *clipboard = QGuiApplication::clipboard();
        if (!clipboard) {
            return false; // No clipboard: the code stays visible for copy.
        }
        clipboard->setText(text);
        return true;
    };
    OpenHandler m_openHandler = [](const QUrl &url) { return QDesktopServices::openUrl(url); };

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
    int m_pollAttempts = 0;
    int m_lastPollHttpStatus = 0;
    QString m_lastPollResult;

    bool m_volatileSession = false;
    bool m_clearFailed = false;
    int m_attemptId = 0;
    QTimer *m_pollTimer = nullptr;
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_countdownTimer = nullptr;
    QTimer *m_expiryTimer = nullptr;
};
