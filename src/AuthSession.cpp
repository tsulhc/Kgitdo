#include "AuthSession.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QUrl>

#include "DeviceFlowProtocol.h"
#include "GitHubAppConfig.h"

namespace
{
constexpr int kExpiryLeewaySecs = 60;

QString missingClientIdText()
{
    return QStringLiteral("GitHub App is not configured. Set KGITDO_GITHUB_CLIENT_ID to the public client ID.");
}
} // namespace

AuthSession::AuthSession(IAuthNetwork *network, ISecureStore *secureStore, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_secureStore(secureStore)
{
    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &AuthSession::deviceCodeChanged);
    m_expiryTimer = new QTimer(this);
    m_expiryTimer->setSingleShot(true);
    connect(m_expiryTimer, &QTimer::timeout, this, &AuthSession::expireDeviceCode);
    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(true);
    connect(m_pollTimer, &QTimer::timeout, this, [this]() { pollOnce(); });
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
        if (isAuthenticated()) {
            refreshSession();
        }
    });
}

int AuthSession::deviceExpiresInSeconds() const
{
    if (m_status != AwaitingAuthorization || !m_deviceExpiresAt.isValid()) {
        return 0;
    }
    const int remaining = static_cast<int>(QDateTime::currentDateTimeUtc().secsTo(m_deviceExpiresAt));
    return remaining > 0 ? remaining : 0;
}

bool AuthSession::hasSecureStorage() const
{
    return m_secureStore && m_secureStore->isAvailable();
}

void AuthSession::setClientId(const QString &clientId)
{
    const QString trimmed = clientId.trimmed();
    if (trimmed == m_clientId) {
        return;
    }
    m_clientId = trimmed;
    Q_EMIT clientIdChanged();
}

void AuthSession::startLogin()
{
    if (m_pending) {
        return;
    }
    if (m_status == AwaitingAuthorization || m_status == RequestingDeviceCode || m_status == Refreshing) {
        return;
    }
    if (m_status == Authenticated) {
        return;
    }
    if (!m_network || m_clientId.isEmpty()) {
        setError(missingClientIdText());
        setStatus(Failed);
        return;
    }
    const int attempt = ++m_attemptId;
    stopPollTimer();
    clearDeviceState();
    setError(QString());
    setStatus(RequestingDeviceCode);
    setPending(true);
    const QString clientId = m_clientId;
    m_network->requestDeviceCode(clientId, [this, attempt](const IAuthNetwork::DeviceCodeResult &result) {
        handleDeviceCodeResult(attempt, result);
    });
}

void AuthSession::cancelLogin()
{
    ++m_attemptId;
    stopPollTimer();
    const bool wasAuthenticated = (m_status == Authenticated);
    clearDeviceState();
    setPending(false);
    if (!wasAuthenticated) {
        setError(QString());
        setStatus(SignedOut);
    }
}

void AuthSession::retry()
{
    if (m_pending) {
        return;
    }
    if (m_status == Authenticated) {
        return;
    }
    // Fresh device code keeps the flow simple and avoids reusing an
    // expired or denied code.
    startLogin();
}

void AuthSession::refreshSession()
{
    if (m_pending || !m_network) {
        return;
    }
    if (m_status == AwaitingAuthorization || m_status == RequestingDeviceCode) {
        return;
    }
    if (m_clientId.isEmpty()) {
        setError(missingClientIdText());
        setStatus(Failed);
        return;
    }
    if (m_refreshToken.isEmpty()) {
        setError(QStringLiteral("Session expired. Please sign in again."));
        setStatus(Failed);
        return;
    }
    const int attempt = ++m_attemptId;
    stopPollTimer();
    setError(QString());
    setStatus(Refreshing);
    setPending(true);
    const QString clientId = m_clientId;
    const QString refreshToken = m_refreshToken;
    m_network->refreshAccessToken(clientId, refreshToken,
                                  [this, attempt](const IAuthNetwork::TokenResult &result) {
                                      handleRefreshResult(attempt, result);
                                  });
}

bool AuthSession::ensureFreshAccessToken()
{
    if (!isAuthenticated()) {
        return false;
    }
    if (!isAccessExpired()) {
        return true;
    }
    if (m_refreshToken.isEmpty()) {
        logout();
        setError(QStringLiteral("Session expired. Please sign in again."));
        return false;
    }
    refreshSession();
    return false; // Never send a request while the token is being rotated.
}

void AuthSession::logout()
{
    ++m_attemptId;
    stopPollTimer();
    clearDeviceState();
    clearTokens();
    const bool cleared = clearPersistedTokens();
    m_volatileSession = false;
    Q_EMIT volatileSessionChanged();
    if (cleared) {
        setError(QString());
    } else {
        setError(QStringLiteral("Signed out, but saved credentials could not be removed. "
                                "Use “Retry removing saved credentials” once the wallet is available."));
    }
    setPending(false);
    setStatus(SignedOut);
}

void AuthSession::restore()
{
    if (m_pending || !m_secureStore) {
        return;
    }
    if (m_status == Authenticated || m_status == AwaitingAuthorization || m_status == Refreshing
        || m_status == RequestingDeviceCode) {
        return;
    }
    StoredTokens stored;
    if (!m_secureStore->load(stored) || stored.accessToken.isEmpty()) {
        setPending(false);
        setStatus(SignedOut);
        return;
    }
    m_accessToken = stored.accessToken;
    m_refreshToken = stored.refreshToken;
    m_accessExpiresAtSecs = stored.accessExpiresAtSecsSinceEpoch;
    m_userLogin = stored.login;
    m_volatileSession = false;
    Q_EMIT volatileSessionChanged();
    Q_EMIT userLoginChanged();

    if (!m_network) {
        setPending(false);
        setStatus(SignedOut);
        return;
    }
    if (isAccessExpired() && !m_refreshToken.isEmpty()) {
        refreshSession();
        return;
    }
    const int attempt = ++m_attemptId;
    setError(QString());
    setStatus(Refreshing);
    setPending(true);
    fetchUserAndFinish(attempt);
}

void AuthSession::openVerificationPage()
{
    openValidatedGitHubUrl(m_verificationUri.isEmpty() ? GitHubAppConfig::deviceVerificationUrl()
                                                       : m_verificationUri);
}

void AuthSession::openManageInstallationsPage()
{
    openValidatedGitHubUrl(GitHubAppConfig::manageInstallationsUrl());
}

void AuthSession::pollOnce()
{
    if (m_status != AwaitingAuthorization || !m_network) {
        return;
    }
    if (m_deviceCode.isEmpty()) {
        return;
    }
    if (m_deviceExpiresAt.isValid() && QDateTime::currentDateTimeUtc() >= m_deviceExpiresAt) {
        ++m_attemptId;
        stopPollTimer();
        clearDeviceState();
        setPending(false);
        setError(QStringLiteral("The sign-in code expired. Please start again."));
        setStatus(Failed);
        return;
    }
    const int attempt = m_attemptId;
    const QString clientId = m_clientId;
    const QString deviceCode = m_deviceCode;
    ++m_pollAttempts;
    m_lastPollResult = QStringLiteral("checking");
    Q_EMIT pollProgressChanged();
    setPending(true);
    m_network->pollAccessToken(clientId, deviceCode,
                               [this, attempt](const IAuthNetwork::TokenResult &result) {
                                   handlePollResult(attempt, result);
                               });
}

void AuthSession::setStatus(Status status)
{
    if (m_status == status) {
        return;
    }
    if (m_refreshTimer && status != Authenticated) {
        m_refreshTimer->stop();
    }
    // deviceExpiresInSeconds is time-derived: notify QML every second
    // while authorization is pending instead of displaying a frozen timer.
    if (status == AwaitingAuthorization) {
        m_countdownTimer->start();
    } else {
        m_countdownTimer->stop();
        m_expiryTimer->stop();
    }
    m_status = status;
    Q_EMIT statusChanged();
    if (status == Authenticated) {
        scheduleAutomaticRefresh();
    }
    if (status == AwaitingAuthorization || status == Authenticated) {
        Q_EMIT deviceCodeChanged();
    }
}

void AuthSession::setPending(bool pending)
{
    if (m_pending == pending) {
        return;
    }
    m_pending = pending;
    Q_EMIT pendingChanged();
}

void AuthSession::setError(const QString &message)
{
    if (m_errorMessage == message) {
        return;
    }
    m_errorMessage = message;
    Q_EMIT errorMessageChanged();
}

void AuthSession::clearDeviceState()
{
    m_deviceCode.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    m_deviceExpiresAt = QDateTime();
    m_pollIntervalSeconds = 5;
    m_pollAttempts = 0;
    m_lastPollHttpStatus = 0;
    m_lastPollResult.clear();
    Q_EMIT pollProgressChanged();
}

void AuthSession::clearTokens()
{
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessExpiresAtSecs = 0;
    m_userLogin.clear();
    Q_EMIT userLoginChanged();
}

void AuthSession::beginPollTimer()
{
    stopPollTimer();
    m_pollTimer->start(m_pollIntervalSeconds * 1000);
}

void AuthSession::stopPollTimer()
{
    if (m_pollTimer && m_pollTimer->isActive()) {
        m_pollTimer->stop();
    }
}

void AuthSession::expireDeviceCode()
{
    if (m_status != AwaitingAuthorization) {
        return;
    }
    // The deadline is independent of polling, so a stalled HTTP request
    // cannot leave the authorization screen waiting forever.
    ++m_attemptId;
    stopPollTimer();
    clearDeviceState();
    setPending(false);
    setError(QStringLiteral("The sign-in code expired. Please start again."));
    setStatus(Failed);
}

void AuthSession::scheduleAutomaticRefresh()
{
    m_refreshTimer->stop();
    if (m_accessExpiresAtSecs == 0 || m_refreshToken.isEmpty()) {
        return; // Non-expiring token or no refresh capability.
    }
    const qint64 delayMs = (m_accessExpiresAtSecs - nowSecs() - kExpiryLeewaySecs) * 1000;
    // Refresh one minute before expiry; cap the delay to Qt's int range.
    m_refreshTimer->start(static_cast<int>(qBound<qint64>(1LL, delayMs, 2000000000LL)));
}

bool AuthSession::isAccessExpired() const
{
    if (m_accessToken.isEmpty() || m_accessExpiresAtSecs == 0) {
        return false;
    }
    return nowSecs() + kExpiryLeewaySecs >= m_accessExpiresAtSecs;
}

qint64 AuthSession::nowSecs() const
{
    return QDateTime::currentDateTimeUtc().toSecsSinceEpoch();
}

void AuthSession::persistCurrentTokens()
{
    if (m_accessToken.isEmpty()) {
        return;
    }
    StoredTokens stored;
    stored.accessToken = m_accessToken;
    stored.refreshToken = m_refreshToken;
    stored.accessExpiresAtSecsSinceEpoch = m_accessExpiresAtSecs;
    stored.login = m_userLogin;
    if (m_secureStore && m_secureStore->save(stored)) {
        m_volatileSession = false;
        // A fresh complete session is committed over everything persisted
        // before: no stale leftovers remain to retry about.
        setClearFailed(false);
    } else {
        // Wallet absent/refused: keep a volatile in-memory session and say so.
        m_volatileStore.save(stored);
        m_volatileSession = true;
    }
    Q_EMIT volatileSessionChanged();
    Q_EMIT secureStorageChanged();
}

bool AuthSession::clearPersistedTokens()
{
    m_volatileStore.clear();
    bool cleared = true;
    if (m_secureStore) {
        cleared = m_secureStore->clear();
    }
    setClearFailed(!cleared);
    return cleared;
}

void AuthSession::setClearFailed(bool failed)
{
    if (m_clearFailed == failed) {
        return;
    }
    m_clearFailed = failed;
    Q_EMIT clearFailedChanged();
}

void AuthSession::retryClear()
{
    // Only meaningful after a failed cleanup; harmless otherwise. Retries
    // the store removal without touching session state, so a still-locked
    // wallet keeps the warning and the retry affordance.
    if (!m_clearFailed || !m_secureStore) {
        return;
    }
    if (clearPersistedTokens()) {
        setError(QString());
    }
}

void AuthSession::handleDeviceCodeResult(int attemptId, const IAuthNetwork::DeviceCodeResult &result)
{
    if (attemptId != m_attemptId) {
        return;
    }
    if (!result.ok) {
        setPending(false);
        setError(result.errorString.isEmpty() ? QStringLiteral("Could not start sign-in. Please retry.")
                                              : result.errorString);
        setStatus(Failed);
        return;
    }
    m_deviceCode = result.data.deviceCode;
    m_userCode = result.data.userCode;
    m_verificationUri = result.data.verificationUri;
    m_pollIntervalSeconds = result.data.intervalSeconds > 0 ? result.data.intervalSeconds : 5;
    Q_EMIT pollIntervalChanged();
    const int expiresIn = result.data.expiresInSeconds > 0 ? result.data.expiresInSeconds : 900;
    m_deviceExpiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresIn);
    setStatus(AwaitingAuthorization);
    m_expiryTimer->start(qMin(expiresIn, 2147483) * 1000);
    Q_EMIT deviceCodeChanged();
    beginPollTimer();
    // W8 simplified access: copy the code up front and open the
    // verification page automatically. Both are best-effort — the waiting
    // screen always offers manual Copy code / Open GitHub actions.
    copyDeviceCodeToClipboard();
    openVerificationPage();
    // Keep pending true while polling: buttons use it for double-click guard.
}

void AuthSession::handlePollResult(int attemptId, const IAuthNetwork::TokenResult &result)
{
    if (attemptId != m_attemptId || m_status != AwaitingAuthorization) {
        return;
    }
    m_lastPollHttpStatus = result.httpStatus;
    if (result.ok && result.data.hasToken) {
        m_lastPollResult = QStringLiteral("authorized");
    } else if (!result.transportError.isEmpty()) {
        m_lastPollResult = QStringLiteral("network error");
    } else if (result.data.errorCode == QLatin1String("authorization_pending")) {
        m_lastPollResult = QStringLiteral("authorization pending");
    } else if (result.data.errorCode == QLatin1String("slow_down")) {
        m_lastPollResult = QStringLiteral("polling slowed down");
    } else {
        m_lastPollResult = QStringLiteral("GitHub error");
    }
    Q_EMIT pollProgressChanged();
    if (result.ok && result.data.hasToken) {
        stopPollTimer();
        m_accessToken = result.data.accessToken;
        m_refreshToken = result.data.refreshToken;
        if (result.data.expiresInSeconds > 0) {
            m_accessExpiresAtSecs = nowSecs() + result.data.expiresInSeconds;
        } else {
            m_accessExpiresAtSecs = 0;
        }
        setStatus(Refreshing);
        fetchUserAndFinish(attemptId);
        return;
    }
    if (!result.transportError.isEmpty()) {
        // Network/timeout/parse failure: stop and let the user retry with a
        // fresh code rather than hot-looping the token endpoint.
        stopPollTimer();
        setPending(false);
        setError(result.transportError);
        setStatus(Failed);
        return;
    }
    const QString code = result.data.errorCode;
    if (DeviceFlowProtocol::isTransientPollError(code)) {
        if (code == QLatin1String("slow_down")) {
            const int serverInterval = result.data.intervalSeconds;
            m_pollIntervalSeconds = (serverInterval > m_pollIntervalSeconds ? serverInterval
                                                                             : m_pollIntervalSeconds + 5);
            Q_EMIT pollIntervalChanged();
        }
        beginPollTimer();
        return;
    }
    stopPollTimer();
    setPending(false);
    if (DeviceFlowProtocol::isExpiredPollError(code)) {
        clearDeviceState();
        setError(QStringLiteral("The sign-in code expired. Please start again."));
    } else if (DeviceFlowProtocol::isAccessDeniedError(code)) {
        clearDeviceState();
        setError(QStringLiteral("Authorization was cancelled on GitHub. Please start again if needed."));
    } else {
        clearDeviceState();
        setError(friendlyPollError(code, result.data.errorDescription));
    }
    setStatus(Failed);
}

void AuthSession::handleRefreshResult(int attemptId, const IAuthNetwork::TokenResult &result)
{
    if (attemptId != m_attemptId) {
        return;
    }
    if (result.ok && result.data.hasToken) {
        m_accessToken = result.data.accessToken;
        // A rotated refresh token replaces the old one; keep the old one
        // when the server omits a new value.
        if (!result.data.refreshToken.isEmpty()) {
            m_refreshToken = result.data.refreshToken;
        }
        if (result.data.expiresInSeconds > 0) {
            m_accessExpiresAtSecs = nowSecs() + result.data.expiresInSeconds;
        }
        fetchUserAndFinish(attemptId);
        return;
    }
    if (!result.transportError.isEmpty()) {
        setPending(false);
        setError(result.transportError);
        setStatus(Failed);
        return;
    }
    const QString code = result.data.errorCode;
    if (code == QLatin1String("bad_refresh_token")) {
        ++m_attemptId;
        clearTokens();
        const bool cleared = clearPersistedTokens();
        m_volatileSession = false;
        Q_EMIT volatileSessionChanged();
        setPending(false);
        if (cleared) {
            setError(QStringLiteral("Session expired. Please sign in again."));
        } else {
            setError(QStringLiteral("Session expired. Please sign in again. "
                                    "Saved credentials could not be removed; use “Retry removing "
                                    "saved credentials” once the wallet is available."));
        }
        setStatus(SignedOut);
        return;
    }
    setPending(false);
    setError(friendlyPollError(code, result.data.errorDescription));
    setStatus(Failed);
}

void AuthSession::fetchUserAndFinish(int attemptId)
{
    const QString token = m_accessToken;
    m_network->fetchUserLogin(token, [this, attemptId](const IAuthNetwork::UserResult &result) {
        // fetchUserAndFinish is called for initial login (fromRefresh=false)
        // and for refresh/restore validation. Distinguish only for status
        // reporting; both end Authenticated on success.
        handleUserResult(attemptId, result, m_status == Refreshing);
    });
}

void AuthSession::handleUserResult(int attemptId, const IAuthNetwork::UserResult &result, bool /*fromRefresh*/)
{
    if (attemptId != m_attemptId) {
        return;
    }
    if (result.ok) {
        m_userLogin = result.login;
        Q_EMIT userLoginChanged();
        persistCurrentTokens();
        setError(QString());
        setPending(false);
        setStatus(Authenticated);
        return;
    }
    if (result.unauthorized) {
        // Revoked/expired token: drop local credentials, require sign-in.
        ++m_attemptId;
        stopPollTimer();
        clearDeviceState();
        clearTokens();
        const bool cleared = clearPersistedTokens();
        m_volatileSession = false;
        Q_EMIT volatileSessionChanged();
        setPending(false);
        if (cleared) {
            setError(QStringLiteral("Session was revoked or expired. Please sign in again."));
        } else {
            setError(QStringLiteral("Session was revoked or expired. Please sign in again. "
                                    "Saved credentials could not be removed; use “Retry removing "
                                    "saved credentials” once the wallet is available."));
        }
        setStatus(SignedOut);
        return;
    }
    // Rate limits, 403/404, offline, malformed: keep tokens out of the store.
    stopPollTimer();
    clearDeviceState();
    setPending(false);
    if (result.rateLimited) {
        setError(QStringLiteral("GitHub rate limit reached. Wait a little and retry."));
    } else if (!result.errorString.isEmpty()) {
        setError(result.errorString);
    } else {
        setError(QStringLiteral("Could not verify the GitHub session. Please retry."));
    }
    // Preserve the freshly issued token in memory so a retry does not force
    // a new device code when only the /user check failed? No: W1 keeps it
    // simple and discards the unverified token to avoid half sessions.
    m_accessToken.clear();
    m_refreshToken.clear();
    m_accessExpiresAtSecs = 0;
    setStatus(Failed);
}

QString AuthSession::friendlyPollError(const QString &code, const QString &description)
{
    if (!description.isEmpty()) {
        return description;
    }
    if (code == QLatin1String("incorrect_client_credentials")) {
        return QStringLiteral("Unknown GitHub App client ID. Check KGITDO_GITHUB_CLIENT_ID.");
    }
    if (code == QLatin1String("device_flow_disabled")) {
        return QStringLiteral("Device flow is disabled for this GitHub App. Enable it in the app settings.");
    }
    if (code == QLatin1String("incorrect_device_code")) {
        return QStringLiteral("Invalid sign-in code. Please start again.");
    }
    if (code == QLatin1String("unsupported_grant_type")) {
        return QStringLiteral("GitHub rejected the sign-in request. Please retry.");
    }
    if (code == QLatin1String("unverified_user_email")) {
        return QStringLiteral("Please verify your primary GitHub email address, then sign in again.");
    }
    if (!code.isEmpty()) {
        return QStringLiteral("Sign-in failed (%1). Please retry.").arg(code);
    }
    return QStringLiteral("Sign-in failed. Please retry.");
}

void AuthSession::copyDeviceCodeToClipboard()
{
    // Manual re-copy of the pending device code. No-op unless a code is
    // actually awaiting authorization (matches the header contract: after
    // authentication there is no pending code to copy).
    if (m_status != AwaitingAuthorization || m_userCode.isEmpty()) {
        return;
    }
    m_copyHandler(m_userCode);
}

void AuthSession::openValidatedGitHubUrl(const QString &urlString)
{
    if (!DeviceFlowProtocol::isAllowedExternalGitHubUrl(urlString)) {
        return;
    }
    m_openHandler(QUrl(urlString));
}
