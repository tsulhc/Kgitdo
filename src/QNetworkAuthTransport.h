#pragma once

#include <QObject>
#include <QTimer>

#include "AuthTransport.h"

// Production HTTP implementation using QNetworkAccessManager.
// No tokens are logged. All GitHub calls use HTTPS.
class QNetworkAuthTransport : public IAuthNetwork
{
    Q_OBJECT
public:
    explicit QNetworkAuthTransport(QObject *parent = nullptr);
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
