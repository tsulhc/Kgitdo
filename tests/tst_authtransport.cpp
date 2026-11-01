#include <QtTest/QtTest>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QQueue>
#include <QTimer>
#include <QUrlQuery>

#include <cstring>

#include "AuthSession.h"
#include "QNetworkAuthTransport.h"
#include "SecureStore.h"

// Integration of the real Qt POST/GET callbacks and AuthSession with synthetic
// HTTP traffic. Never contacts GitHub; no usable credentials or codes.
class FakeReply final : public QNetworkReply
{
public:
    FakeReply(const QNetworkRequest &request, int httpStatus, QByteArray body, QObject *parent)
        : QNetworkReply(parent)
        , m_body(std::move(body))
    {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, httpStatus);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        setOpenMode(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this, httpStatus]() {
            if (httpStatus >= 400) {
                setError(QNetworkReply::ContentAccessDenied, QStringLiteral("HTTP error"));
            }
            setFinished(true);
            Q_EMIT readyRead();
            Q_EMIT finished();
        });
    }

    void abort() override {}

protected:
    qint64 readData(char *destination, qint64 capacity) override
    {
        const qint64 remaining = m_body.size() - m_offset;
        if (remaining <= 0) {
            return -1;
        }
        const qint64 count = qMin(capacity, remaining);
        std::memcpy(destination, m_body.constData() + m_offset, size_t(count));
        m_offset += count;
        return count;
    }

private:
    QByteArray m_body;
    qint64 m_offset = 0;
};

class FakeManager final : public QNetworkAccessManager
{
public:
    struct Response { int httpStatus; QByteArray body; };
    struct RequestRecord {
        Operation operation;
        QNetworkRequest request;
        QByteArray body;
    };
    QQueue<Response> responses;
    QList<RequestRecord> requests;

protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        RequestRecord record{operation, request, outgoingData ? outgoingData->peek(4096) : QByteArray()};
        requests.append(record);
        if (responses.isEmpty()) {
            return new FakeReply(request, 500, QByteArrayLiteral(R"({"error":"unexpected_request"})"), this);
        }
        const Response reply = responses.dequeue();
        return new FakeReply(request, reply.httpStatus, reply.body, this);
    }
};

class AuthTransportTest final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void completeDeviceFlowThroughQtReplies();
    void pendingOnHttp400ContinuesThroughQtReplies();
};

void AuthTransportTest::completeDeviceFlowThroughQtReplies()
{
    FakeManager manager;
    manager.responses.enqueue({200, R"({"device_code":"synthetic-dev","user_code":"ABCD-EFGH","verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})"});
    manager.responses.enqueue({200, R"({"error":"authorization_pending"})"});
    manager.responses.enqueue({200, R"({"access_token":"synthetic-token","refresh_token":"synthetic-refresh","expires_in":28800})"});
    manager.responses.enqueue({200, R"({"login":"octocat"})"});

    QNetworkAuthTransport network(nullptr, &manager);
    InMemorySecureStore store;
    AuthSession session(&network, &store);
    session.setCopyHandler([](const QString &) { return true; });
    session.setOpenHandler([](const QUrl &) { return true; });
    session.setClientId(QStringLiteral("public-test-id"));

    session.startLogin();
    QTRY_COMPARE(session.status(), AuthSession::AwaitingAuthorization);
    QCOMPARE(manager.requests.size(), 1);
    const auto &deviceRequest = manager.requests.at(0);
    QCOMPARE(deviceRequest.operation, QNetworkAccessManager::PostOperation);
    QCOMPARE(deviceRequest.request.url().path(), QStringLiteral("/login/device/code"));
    QCOMPARE(deviceRequest.request.header(QNetworkRequest::ContentTypeHeader).toString(),
             QStringLiteral("application/x-www-form-urlencoded"));
    QCOMPARE(deviceRequest.request.rawHeader("Accept"), QByteArray("application/json"));
    QCOMPARE(QUrlQuery(QString::fromUtf8(deviceRequest.body)).queryItemValue(QStringLiteral("client_id")),
             QStringLiteral("public-test-id"));

    session.pollOnce();
    QTRY_COMPARE(session.lastPollResult(), QStringLiteral("authorization pending"));
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);

    session.pollOnce();
    QTRY_COMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(session.userLogin(), QStringLiteral("octocat"));
    QCOMPARE(manager.requests.size(), 4);
    QCOMPARE(manager.requests.at(1).request.url().path(), QStringLiteral("/login/oauth/access_token"));
    QCOMPARE(manager.requests.at(2).request.url().path(), QStringLiteral("/login/oauth/access_token"));
    const QUrlQuery tokenForm(QString::fromUtf8(manager.requests.at(2).body));
    QCOMPARE(tokenForm.queryItemValue(QStringLiteral("device_code")), QStringLiteral("synthetic-dev"));
    QCOMPARE(tokenForm.queryItemValue(QStringLiteral("grant_type")),
             QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
    QCOMPARE(manager.requests.at(3).request.url().path(), QStringLiteral("/user"));
    QCOMPARE(manager.requests.at(3).request.rawHeader("Authorization"), QByteArray("Bearer synthetic-token"));
    StoredTokens stored;
    QVERIFY(store.load(stored));
    QCOMPARE(stored.login, QStringLiteral("octocat"));
}

void AuthTransportTest::pendingOnHttp400ContinuesThroughQtReplies()
{
    FakeManager manager;
    manager.responses.enqueue({200, R"({"device_code":"synthetic-dev","user_code":"ABCD-EFGH","verification_uri":"https://github.com/login/device","expires_in":900,"interval":5})"});
    manager.responses.enqueue({400, R"({"error":"authorization_pending"})"});
    manager.responses.enqueue({200, R"({"access_token":"synthetic-token"})"});
    manager.responses.enqueue({200, R"({"login":"octocat"})"});

    QNetworkAuthTransport network(nullptr, &manager);
    InMemorySecureStore store;
    AuthSession session(&network, &store);
    session.setCopyHandler([](const QString &) { return true; });
    session.setOpenHandler([](const QUrl &) { return true; });
    session.setClientId(QStringLiteral("public-test-id"));

    session.startLogin();
    QTRY_COMPARE(session.status(), AuthSession::AwaitingAuthorization);
    session.pollOnce();
    QTRY_COMPARE(session.lastPollResult(), QStringLiteral("authorization pending"));
    QCOMPARE(session.lastPollHttpStatus(), 400);
    session.pollOnce();
    QTRY_COMPARE(session.status(), AuthSession::Authenticated);
    QCOMPARE(manager.requests.size(), 4);
}

QTEST_MAIN(AuthTransportTest)
#include "tst_authtransport.moc"
