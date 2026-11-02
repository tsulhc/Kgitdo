#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

#include "GitHubClient.h"
#include "IssueQuery.h"
#include "QNetworkIssueApi.h"

class StubApi final : public IIssueApi
{
public:
    IssueCreateRequest lastRequest;
    QString lastToken;
    IssueHttpResult nextResult;
    int calls = 0;

    void searchIssues(const QUrl &, const QString &, std::function<void(IssueHttpResult)> done) override
    { done({}); }
    void updateIssueState(const IssueUpdateRequest &, const QString &,
                          std::function<void(IssueHttpResult)> done) override
    { done({}); }
    void createIssue(const IssueCreateRequest &request, const QString &token,
                     std::function<void(IssueHttpResult)> done) override
    {
        ++calls;
        lastRequest = request;
        lastToken = token;
        done(nextResult);
    }
};

class IssueCreateTest final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void validatesRepoAndEncodesJson();
    void successAndFailureAreExplicit();
    void doesNotSendInvalidRequests();
};

void IssueCreateTest::validatesRepoAndEncodesJson()
{
    QCOMPARE(IssueQuery::buildIssueCreateUrl(QStringLiteral("tsulhc"), QStringLiteral("sample")).toString(),
             QStringLiteral("https://api.github.com/repos/tsulhc/sample/issues"));
    QVERIFY(IssueQuery::buildIssueCreateUrl(QStringLiteral("tsulhc"), QStringLiteral("../bad")).isEmpty());
    QVERIFY(IssueQuery::buildIssueCreateUrl(QStringLiteral("bad/name"), QStringLiteral("repo")).isEmpty());
    const QJsonObject body = QJsonDocument::fromJson(
        IssueQuery::buildIssueCreateBody(QStringLiteral("Title \"quoted\""), QStringLiteral("Body\nnew line"))
    ).object();
    QCOMPARE(body.value(QStringLiteral("title")).toString(), QStringLiteral("Title \"quoted\""));
    QCOMPARE(body.value(QStringLiteral("body")).toString(), QStringLiteral("Body\nnew line"));

    const auto request = QNetworkIssueApi::makeApiRequest(
        IssueQuery::buildIssueCreateUrl(QStringLiteral("tsulhc"), QStringLiteral("sample")),
        QStringLiteral("synthetic"));
    QCOMPARE(request.rawHeader("Authorization"), QByteArray("Bearer synthetic"));
    QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).value<QNetworkRequest::RedirectPolicy>(),
             QNetworkRequest::SameOriginRedirectPolicy);
}

void IssueCreateTest::successAndFailureAreExplicit()
{
    StubApi api;
    GitHubClient client(&api);
    api.nextResult.httpStatus = 201;
    api.nextResult.body = R"({"number":42,"title":"One issue","html_url":"https://github.com/tsulhc/sample/issues/42"})";
    GitHubClient::CreateResult outcome;
    client.createIssue(QStringLiteral("tsulhc"), QStringLiteral("sample"),
                       QStringLiteral("One issue"), QStringLiteral("A body"),
                       QStringLiteral("synthetic"), [&](const auto &r) { outcome = r; });
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.number, 42);
    QCOMPARE(api.calls, 1);
    QCOMPARE(api.lastRequest.body, QStringLiteral("A body"));
    QCOMPARE(api.lastToken, QStringLiteral("synthetic"));

    for (int status : {401, 403, 404, 422, 429, 500}) {
        api.nextResult.httpStatus = status;
        api.nextResult.body = R"({"message":"error"})";
        client.createIssue(QStringLiteral("tsulhc"), QStringLiteral("sample"),
                           QStringLiteral("One"), {}, QStringLiteral("synthetic"),
                           [&](const auto &r) { outcome = r; });
        QVERIFY(!outcome.ok);
        QVERIFY(!outcome.errorMessage.isEmpty());
    }
    api.nextResult.httpStatus = 200;
    QVERIFY(!GitHubClient::mapCreateResponse(api.nextResult).ok);
    api.nextResult.httpStatus = 201;
    api.nextResult.body = "{}";
    QVERIFY(!GitHubClient::mapCreateResponse(api.nextResult).ok);
}

void IssueCreateTest::doesNotSendInvalidRequests()
{
    StubApi api;
    GitHubClient client(&api);
    GitHubClient::CreateResult outcome;
    client.createIssue(QStringLiteral("tsulhc"), QStringLiteral("bad/repo"),
                       QStringLiteral("Title"), {}, QStringLiteral("synthetic"),
                       [&](const auto &r) { outcome = r; });
    QVERIFY(!outcome.ok);
    QCOMPARE(api.calls, 0);
    client.createIssue(QStringLiteral("tsulhc"), QStringLiteral("repo"),
                       QStringLiteral(""), {}, QStringLiteral("synthetic"),
                       [&](const auto &r) { outcome = r; });
    QVERIFY(!outcome.ok);
    QCOMPARE(api.calls, 0);
}

QTEST_MAIN(IssueCreateTest)
#include "tst_issuecreate.moc"
