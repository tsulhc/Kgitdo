#pragma once

#include <QByteArray>
#include <QObject>
#include <QTimer>

class QNetworkAccessManager;

#include "AuthTransport.h"

// Pure mapping of an HTTP exchange to transport results, unit-tested with
// synthetic bodies. `networkError` mirrors QNetworkReply::error() != NoError
// (set for both transport failures and HTTP error statuses); `networkText`
// is the corresponding user-facing message. Structured OAuth bodies are
// honored whenever present, including on HTTP error statuses (GitHub answers
// e.g. bad_refresh_token with HTTP 400 + JSON).
namespace AuthHttpMapping
{
IAuthNetwork::TokenResult mapTokenResponse(int httpStatus, const QByteArray &body, bool networkError,
                                            const QString &networkText);
IAuthNetwork::DeviceCodeResult mapDeviceCodeResponse(int httpStatus, const QByteArray &body,
                                                     bool networkError, const QString &networkText);
} // namespace AuthHttpMapping

// Production HTTP implementation using QNetworkAccessManager.
// No tokens are logged. All GitHub calls use HTTPS.
class QNetworkAuthTransport : public IAuthNetwork
{
    Q_OBJECT
public:
    explicit QNetworkAuthTransport(QObject *parent = nullptr, QNetworkAccessManager *networkManager = nullptr);
    ~QNetworkAuthTransport() override;

    void requestDeviceCode(const QString &clientId,
                           std::function<void(DeviceCodeResult)> callback) override;
    void pollAccessToken(const QString &clientId, const QString &deviceCode,
                         std::function<void(TokenResult)> callback) override;
    void refreshAccessToken(const QString &clientId, const QString &refreshToken,
                            std::function<void(TokenResult)> callback) override;
    void fetchUserLogin(const QString &accessToken, std::function<void(UserResult)> callback) override;

private:
    class Private;
    Private *d = nullptr;
};
