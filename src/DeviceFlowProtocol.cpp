#include "DeviceFlowProtocol.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

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

QByteArray deviceCodeRequestJson(const QString &clientId)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("client_id"), clientId);
    // Forces an expiring user token + refresh token even if the app has
    // expiry disabled, so refresh/session restore always works.
    obj.insert(QStringLiteral("scope"), QStringLiteral("offline_access"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray tokenPollRequestJson(const QString &clientId, const QString &deviceCode)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("client_id"), clientId);
    obj.insert(QStringLiteral("device_code"), deviceCode);
    obj.insert(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray refreshRequestJson(const QString &clientId, const QString &refreshToken)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("client_id"), clientId);
    obj.insert(QStringLiteral("refresh_token"), refreshToken);
    obj.insert(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
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
