#include "DeviceFlowProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace DeviceFlowProtocol
{

bool parseDeviceCodeResponse(const QByteArray &json, DeviceCodeData *out, QString *errorString)
{
    if (!out) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorString) {
            *errorString = QStringLiteral("Malformed device-code response.");
        }
        return false;
    }
    const QJsonObject obj = doc.object();
    DeviceCodeData data;
    data.deviceCode = obj.value(QStringLiteral("device_code")).toString();
    data.userCode = obj.value(QStringLiteral("user_code")).toString();
    data.verificationUri = obj.value(QStringLiteral("verification_uri")).toString();
    data.expiresInSeconds = obj.value(QStringLiteral("expires_in")).toInt(0);
    data.intervalSeconds = obj.value(QStringLiteral("interval")).toInt(5);

    if (data.deviceCode.isEmpty() || data.userCode.isEmpty() || data.verificationUri.isEmpty()) {
        if (errorString) {
            *errorString = QStringLiteral("Incomplete device-code response.");
        }
        return false;
    }
    if (!isAllowedExternalGitHubUrl(data.verificationUri)) {
        if (errorString) {
            *errorString = QStringLiteral("Untrusted verification URL.");
        }
        return false;
    }
    if (data.intervalSeconds < 1) {
        data.intervalSeconds = 5;
    }
    *out = data;
    return true;
}

bool parseTokenPollResponse(const QByteArray &json, TokenPollData *out, QString *errorString)
{
    if (!out) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorString) {
            *errorString = QStringLiteral("Malformed token response.");
        }
        return false;
    }
    const QJsonObject obj = doc.object();
    TokenPollData data;
    const QString accessToken = obj.value(QStringLiteral("access_token")).toString();
    if (!accessToken.isEmpty()) {
        data.hasToken = true;
        data.accessToken = accessToken;
        data.refreshToken = obj.value(QStringLiteral("refresh_token")).toString();
        data.expiresInSeconds = obj.value(QStringLiteral("expires_in")).toInt(0);
        data.refreshExpiresInSeconds = obj.value(QStringLiteral("refresh_token_expires_in")).toInt(0);
        *out = data;
        return true;
    }
    const QString errorCode = obj.value(QStringLiteral("error")).toString();
    if (errorCode.isEmpty()) {
        if (errorString) {
            *errorString = QStringLiteral("Unrecognized token response.");
        }
        return false;
    }
    data.hasToken = false;
    data.errorCode = errorCode;
    data.errorDescription = obj.value(QStringLiteral("error_description")).toString();
    data.intervalSeconds = obj.value(QStringLiteral("interval")).toInt(0);
    *out = data;
    return true;
}

bool parseUserLoginResponse(const QByteArray &json, QString *loginOut, QString *errorString)
{
    if (!loginOut) {
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorString) {
            *errorString = QStringLiteral("Malformed user response.");
        }
        return false;
    }
    const QString login = doc.object().value(QStringLiteral("login")).toString();
    if (login.isEmpty()) {
        if (errorString) {
            *errorString = QStringLiteral("User response has no login.");
        }
        return false;
    }
    *loginOut = login;
    return true;
}

QByteArray deviceCodeRequestForm(const QString &clientId)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    // Request an expiring user token + refresh token, even if optional
    // expiration was disabled in the GitHub App registration.
    form.addQueryItem(QStringLiteral("scope"), QStringLiteral("offline_access"));
    return form.query(QUrl::FullyEncoded).toUtf8();
}

QByteArray tokenPollRequestForm(const QString &clientId, const QString &deviceCode)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    form.addQueryItem(QStringLiteral("device_code"), deviceCode);
    form.addQueryItem(QStringLiteral("grant_type"),
                      QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    return form.query(QUrl::FullyEncoded).toUtf8();
}

QByteArray refreshRequestForm(const QString &clientId, const QString &refreshToken)
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("client_id"), clientId);
    form.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
    form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    return form.query(QUrl::FullyEncoded).toUtf8();
}

bool parseOAuthErrorBody(const QByteArray &body, TokenPollData *out, QString *errorString)
{
    if (body.isEmpty()) {
        if (errorString) {
            *errorString = QStringLiteral("Empty error response.");
        }
        return false;
    }
    TokenPollData data;
    QString parseError;
    if (!parseTokenPollResponse(body, &data, &parseError)) {
        if (errorString) {
            *errorString = parseError;
        }
        return false;
    }
    if (data.hasToken || data.errorCode.isEmpty()) {
        if (errorString) {
            *errorString = QStringLiteral("Response is not an OAuth error.");
        }
        return false;
    }
    if (out) {
        *out = data;
    }
    return true;
}

QString httpFailureText(int httpStatus, const QString &networkText)
{
    if (httpStatus == 429) {
        return QStringLiteral("GitHub rate limit reached. Wait a little and retry.");
    }
    if (!networkText.isEmpty()) {
        return networkText;
    }
    if (httpStatus != 0) {
        return QStringLiteral("GitHub request failed (HTTP %1). Please retry.").arg(httpStatus);
    }
    return QStringLiteral("Network request failed. Check your connection and retry.");
}

bool isTransientPollError(const QString &errorCode)
{
    return errorCode == QLatin1String("authorization_pending") || errorCode == QLatin1String("slow_down");
}

bool isExpiredPollError(const QString &errorCode)
{
    return errorCode == QLatin1String("expired_token") || errorCode == QLatin1String("token_expired")
        || errorCode == QLatin1String("bad_verification_code");
}

bool isAccessDeniedError(const QString &errorCode)
{
    return errorCode == QLatin1String("access_denied");
}

bool isAllowedExternalGitHubUrl(const QString &urlString)
{
    const QUrl url(urlString);
    if (!url.isValid() || url.scheme() != QLatin1String("https")) {
        return false;
    }
    return url.host() == QLatin1String("github.com");
}

} // namespace DeviceFlowProtocol
