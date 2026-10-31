#include "QNetworkAuthTransport.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "DeviceFlowProtocol.h"

namespace
{
constexpr int kTransferTimeoutMs = 30000;

QNetworkRequest makeJsonPost(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    request.setTransferTimeout(kTransferTimeoutMs);
    return request;
}

QString networkFailureText(QNetworkReply *reply)
{
    if (!reply) {
        return QStringLiteral("Network request failed.");
    }
    switch (reply->error()) {
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("Request timed out. Check your connection and retry.");
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("Cannot reach github.com. Check your connection and retry.");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("Secure connection failed. Check your connection and retry.");
    default:
        break;
    }
    return QStringLiteral("Network request failed. Check your connection and retry.");
}
} // namespace

class QNetworkAuthTransport::Private
{
public:
    QNetworkAccessManager nam;
};

QNetworkAuthTransport::QNetworkAuthTransport(QObject *parent)
    : IAuthNetwork(parent)
    , d(new Private)
{
    d->nam.setParent(this);
}

QNetworkAuthTransport::~QNetworkAuthTransport()
{
    delete d;
}

void QNetworkAuthTransport::requestDeviceCode(const QString &clientId,
                                             std::function<void(DeviceCodeResult)> callback)
{
    QNetworkRequest request = makeJsonPost(QUrl(QStringLiteral("https://github.com/login/device/code")));
    QNetworkReply *reply = d->nam.post(request, DeviceFlowProtocol::deviceCodeRequestJson(clientId));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        DeviceCodeResult result;
        result.httpStatus = 0;
        if (reply->error() != QNetworkReply::NoError) {
            result.ok = false;
            result.errorString = networkFailureText(reply);
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        DeviceFlowProtocol::DeviceCodeData data;
        QString parseError;
        if (!DeviceFlowProtocol::parseDeviceCodeResponse(body, &data, &parseError)) {
            result.ok = false;
            result.errorString = parseError;
        } else {
            result.ok = true;
            result.data = data;
        }
        const auto cb = callback;
        reply->deleteLater();
        cb(result);
    });
}

void QNetworkAuthTransport::pollAccessToken(const QString &clientId, const QString &deviceCode,
                                           std::function<void(TokenResult)> callback)
{
    QNetworkRequest request = makeJsonPost(QUrl(QStringLiteral("https://github.com/login/oauth/access_token")));
    QNetworkReply *reply =
        d->nam.post(request, DeviceFlowProtocol::tokenPollRequestJson(clientId, deviceCode));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        TokenResult result;
        if (reply->error() != QNetworkReply::NoError) {
            result.ok = false;
            result.transportError = networkFailureText(reply);
            result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        DeviceFlowProtocol::TokenPollData data;
        QString parseError;
        if (!DeviceFlowProtocol::parseTokenPollResponse(body, &data, &parseError)) {
            result.ok = false;
            result.transportError = parseError;
        } else if (data.hasToken) {
            result.ok = true;
            result.data = data;
        } else {
            result.ok = false;
            result.data = data;
        }
        const auto cb = callback;
        reply->deleteLater();
        cb(result);
    });
}

void QNetworkAuthTransport::refreshAccessToken(const QString &clientId, const QString &refreshToken,
                                              std::function<void(TokenResult)> callback)
{
    QNetworkRequest request = makeJsonPost(QUrl(QStringLiteral("https://github.com/login/oauth/access_token")));
    QNetworkReply *reply =
        d->nam.post(request, DeviceFlowProtocol::refreshRequestJson(clientId, refreshToken));
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        TokenResult result;
        if (reply->error() != QNetworkReply::NoError) {
            result.ok = false;
            result.transportError = networkFailureText(reply);
            result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        DeviceFlowProtocol::TokenPollData data;
        QString parseError;
        if (!DeviceFlowProtocol::parseTokenPollResponse(body, &data, &parseError)) {
            result.ok = false;
            result.transportError = parseError;
        } else if (data.hasToken) {
            result.ok = true;
            result.data = data;
        } else {
            result.ok = false;
            result.data = data;
        }
        const auto cb = callback;
        reply->deleteLater();
        cb(result);
    });
}

void QNetworkAuthTransport::fetchUserLogin(const QString &accessToken,
                                          std::function<void(UserResult)> callback)
{
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/user")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
    request.setTransferTimeout(kTransferTimeoutMs);
    QNetworkReply *reply = d->nam.get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        UserResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError && result.httpStatus == 0) {
            result.ok = false;
            result.errorString = networkFailureText(reply);
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        const QByteArray body = reply->readAll();
        if (result.httpStatus == 401) {
            result.ok = false;
            result.unauthorized = true;
            result.errorString = QStringLiteral("Session was revoked or expired. Please sign in again.");
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        if (result.httpStatus == 429
            || (result.httpStatus == 403
                && reply->rawHeader("X-RateLimit-Remaining") == QByteArray("0"))) {
            result.ok = false;
            result.rateLimited = true;
            result.errorString =
                QStringLiteral("GitHub rate limit reached. Wait a little and retry.");
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        if (result.httpStatus < 200 || result.httpStatus >= 300) {
            result.ok = false;
            if (result.httpStatus == 403 || result.httpStatus == 404) {
                result.errorString =
                    QStringLiteral("GitHub refused the request. The authorization may not cover this resource.");
            } else {
                result.errorString = QStringLiteral("GitHub request failed. Please retry.");
            }
            const auto cb = callback;
            reply->deleteLater();
            cb(result);
            return;
        }
        QString login;
        QString parseError;
        if (!DeviceFlowProtocol::parseUserLoginResponse(body, &login, &parseError)) {
            result.ok = false;
            result.errorString = parseError;
        } else {
            result.ok = true;
            result.login = login;
        }
        const auto cb = callback;
        reply->deleteLater();
        cb(result);
    });
}
