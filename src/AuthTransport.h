#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "DeviceFlowProtocol.h"

// Async HTTP contract for GitHub App device flow + identity.
// Production uses QNetworkAccessManager; tests inject a fake.
// Callbacks are always invoked (possibly synchronously by fakes).
// (No Q_OBJECT: the contract carries no signals; QObject parenting only.)
class IAuthNetwork : public QObject
{
public:
    struct DeviceCodeResult {
        bool ok = false;
        DeviceFlowProtocol::DeviceCodeData data;
        QString errorString; // transport/parse error, no secrets
    };

    struct TokenResult {
        bool ok = false; // true only when a token was issued
        DeviceFlowProtocol::TokenPollData data;
        QString transportError; // network/timeout/parse failure
        int httpStatus = 0;
    };

    struct UserResult {
        bool ok = false;
        QString login;
        int httpStatus = 0;
        QString errorString;
        bool unauthorized = false; // HTTP 401: revoked/expired/bad token
        bool rateLimited = false;
    };

    explicit IAuthNetwork(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual void requestDeviceCode(const QString &clientId,
                                   std::function<void(DeviceCodeResult)> callback) = 0;
    virtual void pollAccessToken(const QString &clientId, const QString &deviceCode,
                                 std::function<void(TokenResult)> callback) = 0;
    virtual void refreshAccessToken(const QString &clientId, const QString &refreshToken,
                                    std::function<void(TokenResult)> callback) = 0;
    virtual void fetchUserLogin(const QString &accessToken, std::function<void(UserResult)> callback) = 0;
};
