#include <QtTest/QtTest>

#include "DeviceFlowProtocol.h"
#include "QNetworkAuthTransport.h"

// Covers the transport mapping with synthetic HTTP exchanges: structured
// OAuth bodies must be honored on any status (finding: HTTP 400 bodies were
// discarded), and bare failures must map to actionable text (429-aware).
class AuthHttpMappingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tokenSuccessOnHttp200();
    void tokenPendingOnHttp200();
    void tokenPendingOnHttp400ContinuesPolling();
    void tokenBadRefreshOnHttp400IsStructured();
    void tokenExpiredOnHttp400IsStructured();
    void tokenErrorBodyOnHttp500IsStructured();
    void tokenEmptyBodyOnHttp400IsTransportError();
    void tokenMalformedBodyOnHttp400IsTransportError();
    void tokenOfflineIsTransportError();
    void tokenRateLimitWithoutBodyMentionsLimit();
    void deviceSuccessOnHttp200();
    void deviceOAuthErrorOnHttp401IsStructured();
    void deviceEmptyBodyOnHttp401IsTransportError();
    void protocolOAuthErrorBodyHelpers();
};

void AuthHttpMappingTest::tokenSuccessOnHttp200()
{
    const QByteArray body = R"({"access_token":"ghu_x","refresh_token":"ghr_x","expires_in":28800})";
    const auto r = AuthHttpMapping::mapTokenResponse(200, body, false, QString());
    QVERIFY(r.ok);
    QVERIFY(r.data.hasToken);
    QCOMPARE(r.data.accessToken, QStringLiteral("ghu_x"));
    QCOMPARE(r.httpStatus, 200);
    QVERIFY(r.transportError.isEmpty());
}

void AuthHttpMappingTest::tokenPendingOnHttp200()
{
    const QByteArray body = R"({"error":"authorization_pending","error_description":"waiting"})";
    const auto r = AuthHttpMapping::mapTokenResponse(200, body, false, QString());
    QVERIFY(!r.ok);
    QCOMPARE(r.data.errorCode, QStringLiteral("authorization_pending"));
    QVERIFY(r.transportError.isEmpty());
}

void AuthHttpMappingTest::tokenPendingOnHttp400ContinuesPolling()
{
    const QByteArray body = R"({\"error\":\"authorization_pending\"})";
    const auto result = AuthHttpMapping::mapTokenResponse(400, body, true, QStringLiteral("HTTP 400"));
    QVERIFY(!result.ok);
    QCOMPARE(result.data.errorCode, QStringLiteral("authorization_pending"));
    QVERIFY(result.transportError.isEmpty());
}

void AuthHttpMappingTest::tokenBadRefreshOnHttp400IsStructured()
{
    // Regression: HTTP 400 {"error":"bad_refresh_token"} used to surface as a
    // generic transport failure, so AuthSession kept revoked credentials.
    const QByteArray body = R"({"error":"bad_refresh_token","error_description":"refresh token revoked"})";
    const auto r = AuthHttpMapping::mapTokenResponse(400, body, true, QStringLiteral("bad request"));
    QVERIFY(!r.ok);
    QCOMPARE(r.data.errorCode, QStringLiteral("bad_refresh_token"));
    QCOMPARE(r.httpStatus, 400);
    QVERIFY(r.transportError.isEmpty());
}

void AuthHttpMappingTest::tokenExpiredOnHttp400IsStructured()
{
    const QByteArray body = R"({"error":"expired_token","error_description":"expired"})";
    const auto r = AuthHttpMapping::mapTokenResponse(400, body, true, QStringLiteral("bad request"));
    QVERIFY(!r.ok);
    QCOMPARE(r.data.errorCode, QStringLiteral("expired_token"));
    QVERIFY(r.transportError.isEmpty());
}

void AuthHttpMappingTest::tokenErrorBodyOnHttp500IsStructured()
{
    const QByteArray body = R"({"error":"temporarily_unavailable"})";
    const auto r = AuthHttpMapping::mapTokenResponse(500, body, true, QStringLiteral("server error"));
    QVERIFY(!r.ok);
    QCOMPARE(r.data.errorCode, QStringLiteral("temporarily_unavailable"));
    QVERIFY(r.transportError.isEmpty());
}

void AuthHttpMappingTest::tokenEmptyBodyOnHttp400IsTransportError()
{
    const auto r = AuthHttpMapping::mapTokenResponse(400, QByteArray(), true,
                                                     QStringLiteral("proxy refused"));
    QVERIFY(!r.ok);
    QVERIFY(!r.data.hasToken);
    QCOMPARE(r.transportError, QStringLiteral("proxy refused"));
    QCOMPARE(r.httpStatus, 400);
}

void AuthHttpMappingTest::tokenMalformedBodyOnHttp400IsTransportError()
{
    const auto r = AuthHttpMapping::mapTokenResponse(400, QByteArray("<html>oops</html>"), true,
                                                     QStringLiteral("bad request"));
    QVERIFY(!r.ok);
    QCOMPARE(r.transportError, QStringLiteral("bad request"));
}

void AuthHttpMappingTest::tokenOfflineIsTransportError()
{
    // No HTTP status: genuine transport failure, body is meaningless.
    const auto r = AuthHttpMapping::mapTokenResponse(0, QByteArray(), true,
                                                     QStringLiteral("Cannot reach github.com."));
    QVERIFY(!r.ok);
    QVERIFY(r.transportError.contains(QStringLiteral("github.com"), Qt::CaseInsensitive));
}

void AuthHttpMappingTest::tokenRateLimitWithoutBodyMentionsLimit()
{
    const auto r = AuthHttpMapping::mapTokenResponse(429, QByteArray(), true,
                                                     QStringLiteral("server busy"));
    QVERIFY(!r.ok);
    QVERIFY(r.transportError.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive));
}

void AuthHttpMappingTest::deviceSuccessOnHttp200()
{
    const QByteArray body =
        R"({"device_code":"dev","user_code":"WDJB-MJHT","verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})";
    const auto r = AuthHttpMapping::mapDeviceCodeResponse(200, body, false, QString());
    QVERIFY(r.ok);
    QCOMPARE(r.data.userCode, QStringLiteral("WDJB-MJHT"));
}

void AuthHttpMappingTest::deviceOAuthErrorOnHttp401IsStructured()
{
    const QByteArray body =
        R"({"error":"incorrect_client_credentials","error_description":"Unknown client ID."})";
    const auto r = AuthHttpMapping::mapDeviceCodeResponse(401, body, true, QStringLiteral("unauthorized"));
    QVERIFY(!r.ok);
    QVERIFY(r.errorString.contains(QStringLiteral("Unknown client ID")));
    QCOMPARE(r.httpStatus, 401);
}

void AuthHttpMappingTest::deviceEmptyBodyOnHttp401IsTransportError()
{
    const auto r = AuthHttpMapping::mapDeviceCodeResponse(401, QByteArray(), true,
                                                          QStringLiteral("unauthorized"));
    QVERIFY(!r.ok);
    QCOMPARE(r.errorString, QStringLiteral("unauthorized"));
}

void AuthHttpMappingTest::protocolOAuthErrorBodyHelpers()
{
    DeviceFlowProtocol::TokenPollData out;
    QString error;
    QVERIFY(!DeviceFlowProtocol::parseOAuthErrorBody(QByteArray(), &out, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!DeviceFlowProtocol::parseOAuthErrorBody(QByteArray("<html>"), &out, &error));
    const QByteArray tokenBody = R"({"access_token":"ghu_x"})";
    QVERIFY(!DeviceFlowProtocol::parseOAuthErrorBody(tokenBody, &out, &error));
    const QByteArray errorBody = R"({"error":"access_denied","error_description":"no"})";
    QVERIFY(DeviceFlowProtocol::parseOAuthErrorBody(errorBody, &out, &error));
    QCOMPARE(out.errorCode, QStringLiteral("access_denied"));

    QVERIFY(DeviceFlowProtocol::httpFailureText(429, QStringLiteral("busy"))
                .contains(QStringLiteral("rate limit"), Qt::CaseInsensitive));
    QCOMPARE(DeviceFlowProtocol::httpFailureText(400, QStringLiteral("net down")),
             QStringLiteral("net down"));
    QVERIFY(!DeviceFlowProtocol::httpFailureText(500, QString()).isEmpty());
}

QTEST_MAIN(AuthHttpMappingTest)
#include "tst_authhttpmapping.moc"
