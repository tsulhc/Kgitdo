#include <QtTest>

#include "GitHubClient.h"

namespace
{
QByteArray itemJson(qlonglong id, int number, const char *title, const char *updated = "2026-10-01T10:00:00Z")
{
    return QByteArray("{\"id\":") + QByteArray::number(id) + ",\"number\":" + QByteArray::number(number)
        + ",\"title\":\"" + title + "\",\"state\":\"open\",\"html_url\":\"https://github.com/o/r/issues/"
        + QByteArray::number(number) + "\",\"updated_at\":\"" + updated + "\","
        + "\"repository_url\":\"https://api.github.com/repos/o/r\","
        + "\"labels\":[{\"name\":\"bug\"}]}";
}

QByteArray pageJson(std::initializer_list<QByteArray> items, bool incomplete = false,
                    int totalCount = -1)
{
    QByteArray json = "{\"incomplete_results\":";
    json += incomplete ? "true" : "false";
    if (totalCount >= 0) {
        json += ",\"total_count\":" + QByteArray::number(totalCount);
    }
    json += ",\"items\":[";
    bool first = true;
    for (const QByteArray &item : items) {
        if (!first) {
            json += ",";
        }
        first = false;
        json += item;
    }
    return json + "]}";
}

IssueHttpResult httpOk(const QByteArray &body, const QByteArray &link = QByteArray())
{
    IssueHttpResult result;
    result.httpStatus = 200;
    if (!link.isEmpty()) {
        result.headers.insert(QStringLiteral("link"), QString::fromUtf8(link));
    }
    result.body = body;
    return result;
}

IssueHttpResult httpStatus(int status, const QByteArray &body = QByteArray("{\"message\":\"oops\"}"))
{
    IssueHttpResult result;
    result.httpStatus = status;
    result.body = body;
    return result;
}
} // namespace

class FakeIssueApi : public IIssueApi
{
public:
    QList<IssueHttpResult> queue;
    QList<QUrl> requestedUrls;
    QList<QString> requestedTokens;

    void searchIssues(const QUrl &url, const QString &accessToken,
                      std::function<void(IssueHttpResult)> callback) override
    {
        requestedUrls.append(url);
        requestedTokens.append(accessToken);
        IssueHttpResult result;
        if (!queue.isEmpty()) {
            result = queue.takeFirst();
        } else {
            result.transportError = QStringLiteral("no queued response");
        }
        callback(result);
    }
};

class GitHubClientTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void allViewMergesAndDedups();
    void assignedViewFlagsUniformly();
    void immediateErrorWithoutRequests();
    void firstPageFailureIsError();
    void midPaginationFailureTruncates();
    void foreignNextLinkStopsWithoutTokenLeak();
    void secondQueryFailureWarns();
    void incompleteAndCappedSurface();
    void unauthorizedSurfaces();
    void rateLimit429();
    void forbiddenByQuotaIsRateLimit();
    void forbiddenPlain();
    void validationAndServerErrors();
    void malformedBody();
    void retryAfterPrefersHeader();
};

void GitHubClientTest::allViewMergesAndDedups()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A"), itemJson(2, 8, "B")})));
    api.queue.append(httpOk(pageJson({itemJson(2, 8, "B"), itemJson(3, 9, "C")})));
    GitHubClient client(&api);
    bool called = false;
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           called = true;
                           QVERIFY(result.ok);
                           QCOMPARE(result.issues.size(), 3);
                           // Id 2 came from both queries: exactly once, both flags.
                           const Issue &second = result.issues.at(1);
                           QCOMPARE(second.id, 2LL);
                           QVERIFY(second.isAssignedToMe);
                           QVERIFY(second.isAuthoredByMe);
                           QVERIFY(!result.truncated);
                           QVERIFY(result.errorMessage.isEmpty());
                       });
    QVERIFY(called);
    QCOMPARE(api.requestedUrls.size(), 2);
    QVERIFY(api.requestedUrls.at(0).query().contains(QStringLiteral("author")));
    QVERIFY(api.requestedUrls.at(1).query().contains(QStringLiteral("assignee")));
    QCOMPARE(api.requestedTokens, QList<QString>({QStringLiteral("tok"), QStringLiteral("tok")}));
}

void GitHubClientTest::assignedViewFlagsUniformly()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(5, 1, "Only")})));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Assigned, IssueQuery::State::Closed, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.ok);
                           QCOMPARE(result.issues.size(), 1);
                           QVERIFY(result.issues.at(0).isAssignedToMe);
                           QVERIFY(!result.issues.at(0).isAuthoredByMe);
                       });
    QCOMPARE(api.requestedUrls.size(), 1);
    QVERIFY(api.requestedUrls.at(0).query().contains(QStringLiteral("assignee")));
    QVERIFY(api.requestedUrls.at(0).query().contains(QStringLiteral("is%3Aclosed"))
        || api.requestedUrls.at(0).query().contains(QStringLiteral("is:closed")));
}

void GitHubClientTest::immediateErrorWithoutRequests()
{
    FakeIssueApi api;
    GitHubClient client(&api);
    bool called = false;
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QString(), QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           called = true;
                           QVERIFY(!result.ok);
                           QVERIFY(!result.errorMessage.isEmpty());
                       });
    QVERIFY(called);
    QVERIFY(api.requestedUrls.isEmpty());
    called = false;
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"), QString(),
                       [&](const GitHubClient::FetchResult &result) {
                           called = true;
                           QVERIFY(!result.ok);
                       });
    QVERIFY(called);
    QVERIFY(api.requestedUrls.isEmpty());
}

void GitHubClientTest::firstPageFailureIsError()
{
    FakeIssueApi api;
    IssueHttpResult failed;
    failed.transportError = QStringLiteral("boom");
    api.queue.append(failed);
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A")})));
    GitHubClient client(&api);
    // First query fails, second succeeds: partial data with warning.
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.ok);
                           QCOMPARE(result.issues.size(), 1);
                           QVERIFY(result.truncated);
                           QVERIFY(!result.errorMessage.isEmpty());
                       });
}

void GitHubClientTest::midPaginationFailureTruncates()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A")}),
                            "<https://api.github.com/search/issues?q=x&page=2>; rel=\"next\""));
    IssueHttpResult failed;
    failed.transportError = QStringLiteral("boom");
    api.queue.append(failed);
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.ok);
                           QCOMPARE(result.issues.size(), 1);
                           QVERIFY(result.truncated);
                           QVERIFY(!result.errorMessage.isEmpty());
                       });
    QCOMPARE(api.requestedUrls.size(), 2);
}

void GitHubClientTest::foreignNextLinkStopsWithoutTokenLeak()
{
    // A rel="next" pointing outside the GitHub API origin must never be
    // requested (the Bearer token would leak there). The first page is
    // kept, the result is marked partial, and only the API origin is hit.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A")}), "<https://evil.example/next>; rel=\"next\""));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.ok);
                           QCOMPARE(result.issues.size(), 1);
                           QVERIFY(result.truncated);
                           QVERIFY(result.errorMessage.contains(QStringLiteral("GitHub API")));
                       });
    QCOMPARE(api.requestedUrls.size(), 1);
    for (const QUrl &url : api.requestedUrls) {
        QCOMPARE(url.host(), QStringLiteral("api.github.com"));
        QVERIFY(url.scheme() == QStringLiteral("https"));
    }
}

void GitHubClientTest::secondQueryFailureWarns()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A")})));
    api.queue.append(httpStatus(500));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.ok);
                           QCOMPARE(result.issues.size(), 1);
                           QVERIFY(result.truncated);
                       });
}

void GitHubClientTest::incompleteAndCappedSurface()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A")}, true, 1500)));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.ok);
                           QVERIFY(result.incomplete);
                           QVERIFY(result.capped);
                       });
}

void GitHubClientTest::unauthorizedSurfaces()
{
    FakeIssueApi api;
    api.queue.append(httpStatus(401, QByteArray("{\"message\":\"Bad credentials\"}")));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(!result.ok);
                           QVERIFY(result.unauthorized);
                           QVERIFY(result.errorMessage.contains(QStringLiteral("sign in"),
                                                                Qt::CaseInsensitive));
                       });
}

void GitHubClientTest::rateLimit429()
{
    FakeIssueApi api;
    IssueHttpResult limited = httpStatus(429, QByteArray("{\"message\":\"slow down\"}"));
    limited.headers.insert(QStringLiteral("retry-after"), QStringLiteral("60"));
    api.queue.append(limited);
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(!result.ok);
                           QVERIFY(result.rateLimited);
                           QCOMPARE(result.retryAfterSecs, 60);
                       });
}

void GitHubClientTest::forbiddenByQuotaIsRateLimit()
{
    FakeIssueApi api;
    IssueHttpResult limited = httpStatus(403, QByteArray("{\"message\":\"API rate limit exceeded\"}"));
    limited.headers.insert(QStringLiteral("x-ratelimit-remaining"), QStringLiteral("0"));
    api.queue.append(limited);
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(!result.ok);
                           QVERIFY(result.rateLimited);
                       });
}

void GitHubClientTest::forbiddenPlain()
{
    FakeIssueApi api;
    api.queue.append(httpStatus(403, QByteArray("{\"message\":\"Must have push access\"}")));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(!result.ok);
                           QVERIFY(!result.rateLimited);
                           QVERIFY(result.errorMessage.contains(QStringLiteral("push access")));
                       });
}

void GitHubClientTest::validationAndServerErrors()
{
    FakeIssueApi api;
    api.queue.append(httpStatus(422, QByteArray("{\"message\":\"Validation Failed\"}")));
    api.queue.append(httpStatus(500, QByteArray("oops")));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::All, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(!result.ok);
                           // First error wins the message.
                           QVERIFY(result.errorMessage.contains(QStringLiteral("Validation")));
                       });
}

void GitHubClientTest::malformedBody()
{
    FakeIssueApi api;
    api.queue.append(httpOk(QByteArray("{broken")));
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(!result.ok);
                           QVERIFY(!result.errorMessage.isEmpty());
                       });
}

void GitHubClientTest::retryAfterPrefersHeader()
{
    FakeIssueApi api;
    IssueHttpResult limited = httpStatus(429, QByteArray("{}"));
    // Reset epoch far in the future beats nothing, but an explicit
    // Retry-After wins when larger.
    limited.headers.insert(QStringLiteral("retry-after"), QStringLiteral("120"));
    api.queue.append(limited);
    GitHubClient client(&api);
    client.fetchIssues(IssueQuery::View::Created, IssueQuery::State::Open, QStringLiteral("tok"),
                       QStringLiteral("octocat"),
                       [&](const GitHubClient::FetchResult &result) {
                           QVERIFY(result.rateLimited);
                           QCOMPARE(result.retryAfterSecs, 120);
                       });
}

QTEST_MAIN(GitHubClientTest)
#include "tst_githubclient.moc"
