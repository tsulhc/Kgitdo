#include <QtTest/QtTest>
#include <QUrlQuery>

#include "DeviceFlowProtocol.h"

class DeviceFlowProtocolTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesDeviceCode();
    void rejectsDeviceCodeWithUntrustedUrl();
    void rejectsMalformedDeviceCode();
    void parsesTokenSuccess();
    void parsesAuthorizationPending();
    void parsesSlowDownWithInterval();
    void parsesExpiredAndDenied();
    void rejectsMalformedToken();
    void parsesUserLogin();
    void rejectsUserWithoutLogin();
    void buildsRequestBodies();
    void classifiesErrors();
    void validatesGitHubUrls();
};

void DeviceFlowProtocolTest::parsesDeviceCode()
{
    const QByteArray json = R"({"device_code":"dev1234567890123456789012345678901234","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})";
    DeviceFlowProtocol::DeviceCodeData out;
    QString err;
    QVERIFY(DeviceFlowProtocol::parseDeviceCodeResponse(json, &out, &err));
    QCOMPARE(out.deviceCode, QStringLiteral("dev1234567890123456789012345678901234"));
    QCOMPARE(out.userCode, QStringLiteral("WDJB-MJHT"));
    QCOMPARE(out.verificationUri, QStringLiteral("https://github.com/login/device"));
    QCOMPARE(out.expiresInSeconds, 900);
    QCOMPARE(out.intervalSeconds, 5);
}

void DeviceFlowProtocolTest::rejectsDeviceCodeWithUntrustedUrl()
{
    const QByteArray json = R"({"device_code":"d","user_code":"U","verification_uri":"https://evil.example/login/device","expires_in":900,"interval":5})";
    DeviceFlowProtocol::DeviceCodeData out;
    QString err;
    QVERIFY(!DeviceFlowProtocol::parseDeviceCodeResponse(json, &out, &err));
    QVERIFY(!err.isEmpty());
}

void DeviceFlowProtocolTest::rejectsMalformedDeviceCode()
{
    DeviceFlowProtocol::DeviceCodeData out;
    QString err;
    QVERIFY(!DeviceFlowProtocol::parseDeviceCodeResponse(QByteArray("{not json"), &out, &err));
    QVERIFY(!DeviceFlowProtocol::parseDeviceCodeResponse(QByteArray(R"({"device_code":"","user_code":"","verification_uri":""})"), &out, &err));
}

void DeviceFlowProtocolTest::parsesTokenSuccess()
{
    const QByteArray json = R"({"access_token":"ghu_abc","refresh_token":"ghr_def","expires_in":28800,"refresh_token_expires_in":15897600,"scope":"","token_type":"bearer"})";
    DeviceFlowProtocol::TokenPollData out;
    QString err;
    QVERIFY(DeviceFlowProtocol::parseTokenPollResponse(json, &out, &err));
    QVERIFY(out.hasToken);
    QCOMPARE(out.accessToken, QStringLiteral("ghu_abc"));
    QCOMPARE(out.refreshToken, QStringLiteral("ghr_def"));
    QCOMPARE(out.expiresInSeconds, 28800);
    QCOMPARE(out.refreshExpiresInSeconds, 15897600);
    QVERIFY(out.errorCode.isEmpty());
}

void DeviceFlowProtocolTest::parsesAuthorizationPending()
{
    const QByteArray json = R"({"error":"authorization_pending","error_description":"pending"})";
    DeviceFlowProtocol::TokenPollData out;
    QString err;
    QVERIFY(DeviceFlowProtocol::parseTokenPollResponse(json, &out, &err));
    QVERIFY(!out.hasToken);
    QCOMPARE(out.errorCode, QStringLiteral("authorization_pending"));
}

void DeviceFlowProtocolTest::parsesSlowDownWithInterval()
{
    const QByteArray json = R"({"error":"slow_down","error_description":"slow","interval":10})";
    DeviceFlowProtocol::TokenPollData out;
    QString err;
    QVERIFY(DeviceFlowProtocol::parseTokenPollResponse(json, &out, &err));
    QCOMPARE(out.errorCode, QStringLiteral("slow_down"));
    QCOMPARE(out.intervalSeconds, 10);
}

void DeviceFlowProtocolTest::parsesExpiredAndDenied()
{
    for (const char *code : {"expired_token", "token_expired", "bad_verification_code", "access_denied"}) {
        const QByteArray json = QByteArray(R"({"error":")") + code + "\"}";
        DeviceFlowProtocol::TokenPollData out;
        QString err;
        QVERIFY2(DeviceFlowProtocol::parseTokenPollResponse(json, &out, &err), code);
        QCOMPARE(out.errorCode, QString::fromUtf8(code));
    }
}

void DeviceFlowProtocolTest::rejectsMalformedToken()
{
    DeviceFlowProtocol::TokenPollData out;
    QString err;
    QVERIFY(!DeviceFlowProtocol::parseTokenPollResponse(QByteArray("{oops"), &out, &err));
    QVERIFY(!DeviceFlowProtocol::parseTokenPollResponse(QByteArray(R"({"unexpected":true})"), &out, &err));
}

void DeviceFlowProtocolTest::parsesUserLogin()
{
    const QByteArray json = R"({"login":"octocat","id":1})";
    QString login;
    QString err;
    QVERIFY(DeviceFlowProtocol::parseUserLoginResponse(json, &login, &err));
    QCOMPARE(login, QStringLiteral("octocat"));
}

void DeviceFlowProtocolTest::rejectsUserWithoutLogin()
{
    QString login;
    QString err;
    QVERIFY(!DeviceFlowProtocol::parseUserLoginResponse(QByteArray(R"({"id":1})"), &login, &err));
    QVERIFY(!DeviceFlowProtocol::parseUserLoginResponse(QByteArray("nope"), &login, &err));
}

void DeviceFlowProtocolTest::buildsRequestBodies()
{
    // Match the form-encoded token endpoint contract. A mock with only
    // synthetic JSON responses would not catch an incorrect POST encoding.
    const QString id = QStringLiteral("cid+&= ä");
    const QString code = QStringLiteral("dev:+/&=");
    const QString refreshToken = QStringLiteral("ghr_+&=:%");

    const QByteArray device = DeviceFlowProtocol::deviceCodeRequestForm(id);
    QVERIFY(!device.startsWith('{'));
    QVERIFY(device.contains("client_id="));
    QCOMPARE(QUrlQuery(QString::fromUtf8(device)).queryItemValue(QStringLiteral("client_id")), id);
    QCOMPARE(QUrlQuery(QString::fromUtf8(device)).queryItemValue(QStringLiteral("scope")),
             QStringLiteral("offline_access"));

    const QByteArray poll = DeviceFlowProtocol::tokenPollRequestForm(id, code);
    const QUrlQuery pollFields(QString::fromUtf8(poll));
    QCOMPARE(pollFields.queryItemValue(QStringLiteral("client_id")), id);
    QCOMPARE(pollFields.queryItemValue(QStringLiteral("device_code")), code);
    QCOMPARE(pollFields.queryItemValue(QStringLiteral("grant_type")),
             QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));

    const QByteArray refresh = DeviceFlowProtocol::refreshRequestForm(id, refreshToken);
    const QUrlQuery refreshFields(QString::fromUtf8(refresh));
    QCOMPARE(refreshFields.queryItemValue(QStringLiteral("client_id")), id);
    QCOMPARE(refreshFields.queryItemValue(QStringLiteral("refresh_token"), QUrl::FullyDecoded), refreshToken);
    QCOMPARE(refreshFields.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));

    // Public desktop client: never distribute client secret or log bodies.
    for (const QByteArray &body : {device, poll, refresh}) {
        QVERIFY(!body.contains("client_secret"));
        QVERIFY(!body.contains('\n'));
    }
}

void DeviceFlowProtocolTest::classifiesErrors()
{
    QVERIFY(DeviceFlowProtocol::isTransientPollError(QStringLiteral("authorization_pending")));
    QVERIFY(DeviceFlowProtocol::isTransientPollError(QStringLiteral("slow_down")));
    QVERIFY(!DeviceFlowProtocol::isTransientPollError(QStringLiteral("expired_token")));
    QVERIFY(DeviceFlowProtocol::isExpiredPollError(QStringLiteral("expired_token")));
    QVERIFY(DeviceFlowProtocol::isExpiredPollError(QStringLiteral("bad_verification_code")));
    QVERIFY(DeviceFlowProtocol::isAccessDeniedError(QStringLiteral("access_denied")));
    QVERIFY(!DeviceFlowProtocol::isAccessDeniedError(QStringLiteral("slow_down")));
}

void DeviceFlowProtocolTest::validatesGitHubUrls()
{
    QVERIFY(DeviceFlowProtocol::isAllowedExternalGitHubUrl(QStringLiteral("https://github.com/login/device")));
    QVERIFY(DeviceFlowProtocol::isAllowedExternalGitHubUrl(QStringLiteral("https://github.com/settings/installations")));
    QVERIFY(!DeviceFlowProtocol::isAllowedExternalGitHubUrl(QStringLiteral("http://github.com/login/device")));
    QVERIFY(!DeviceFlowProtocol::isAllowedExternalGitHubUrl(QStringLiteral("https://evil.example/login/device")));
    QVERIFY(!DeviceFlowProtocol::isAllowedExternalGitHubUrl(QStringLiteral("https://github.com.evil.example/")));
}

QTEST_MAIN(DeviceFlowProtocolTest)
#include "tst_deviceflowprotocol.moc"
