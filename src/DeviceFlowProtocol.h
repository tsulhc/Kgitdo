#pragma once

#include <QByteArray>
#include <QString>

// Pure helpers for the GitHub App Device Authorization Grant (RFC 8628).
// No network, no credentials storage, no logging of secrets. All functions
// are deterministic so they can be unit-tested with synthetic fixtures.
//
// Endpoints (see docs in masterplan §8):
//   POST https://github.com/login/device/code
//   POST https://github.com/login/oauth/access_token
//   GET  https://api.github.com/user
//
// The app never uses a client secret: device flow for a public desktop
// client authenticates with the public client ID only.
namespace DeviceFlowProtocol
{

struct DeviceCodeData {
    QString deviceCode;
    QString userCode;
    QString verificationUri;
    int expiresInSeconds = 0;
    int intervalSeconds = 5;
};

struct TokenPollData {
    bool hasToken = false;
    QString accessToken;
    QString refreshToken;
    int expiresInSeconds = 0;
    int refreshExpiresInSeconds = 0;
    QString errorCode;
    QString errorDescription;
    int intervalSeconds = 0; // present on slow_down responses
};

// Parse POST /login/device/code JSON response.
bool parseDeviceCodeResponse(const QByteArray &json, DeviceCodeData *out, QString *errorString);

// Parse POST /login/oauth/access_token JSON response, both success
// (access_token present) and polling errors (error present).
bool parseTokenPollResponse(const QByteArray &json, TokenPollData *out, QString *errorString);

// Parse GET /user JSON response (minimal: login field).
bool parseUserLoginResponse(const QByteArray &json, QString *loginOut, QString *errorString);

// Request bodies (JSON, sent with Accept: application/json).
QByteArray deviceCodeRequestJson(const QString &clientId);
QByteArray tokenPollRequestJson(const QString &clientId, const QString &deviceCode);
QByteArray refreshRequestJson(const QString &clientId, const QString &refreshToken);

// Parse an OAuth-style {"error": "...", "error_description": "..."} body.
// True only for structured error objects (no token present); used when an
// endpoint answers an HTTP error status with a JSON body, e.g. HTTP 400
// {"error":"bad_refresh_token"} from the token endpoint.
bool parseOAuthErrorBody(const QByteArray &body, TokenPollData *out, QString *errorString);

// User-facing text when no structured body exists. Honors HTTP 429.
QString httpFailureText(int httpStatus, const QString &networkText);

// Error classification for poll responses.
bool isTransientPollError(const QString &errorCode); // authorization_pending, slow_down
bool isExpiredPollError(const QString &errorCode); // expired_token, token_expired, bad_verification_code
bool isAccessDeniedError(const QString &errorCode); // access_denied

// Only open external URLs that are https://github.com/... (plan §3.3).
bool isAllowedExternalGitHubUrl(const QString &urlString);

} // namespace DeviceFlowProtocol
