#include <QtTest>

#include "IssueQuery.h"

class IssueQueryTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void buildAllOpen();
    void buildAssignedClosed();
    void buildCreatedOpen();
    void buildEmptyLogin();
    void buildSearchUrl();
    void parseValidPageAuthored();
    void parseUnknownKindSetsNeitherFlag();
    void parseSkipsPullRequestsAndIdless();
    void parseMalformed();
    void parseMissingItems();
    void parseIncompleteAndTotal();
    void mergeDedupsAndFlags();
    void mergeSortsNewestFirst();
    void nextLinkFound();
    void nextLinkAbsent();
    void nextLinkRejectsNonHttps();
    void nextLinkRejectsForeignHttpsHost();
    void nextLinkAllowsApiOriginOnly();
    void cappedResults();
    void updateUrlValid();
    void updateUrlRejectsBadCoords();
    void updateBodyCloseHasReason();
    void updateBodyReopenHasNoReason();
    void parseUpdatedStateStrict();
};

void IssueQueryTest::buildAllOpen()
{
    const QList<QString> queries = IssueQuery::buildQueryStrings(IssueQuery::View::All,
                                                                 QStringLiteral("octocat"),
                                                                 IssueQuery::State::Open);
    QCOMPARE(queries.size(), 2);
    QVERIFY(queries.at(0).contains(QStringLiteral("author:octocat")));
    QVERIFY(queries.at(0).contains(QStringLiteral("is:issue")));
    QVERIFY(queries.at(0).contains(QStringLiteral("is:open")));
    QVERIFY(queries.at(1).contains(QStringLiteral("assignee:octocat")));
}

void IssueQueryTest::buildAssignedClosed()
{
    const QList<QString> queries = IssueQuery::buildQueryStrings(IssueQuery::View::Assigned,
                                                                 QStringLiteral("octocat"),
                                                                 IssueQuery::State::Closed);
    QCOMPARE(queries.size(), 1);
    QVERIFY(queries.at(0).contains(QStringLiteral("assignee:octocat")));
    QVERIFY(queries.at(0).contains(QStringLiteral("is:closed")));
    QVERIFY(!queries.at(0).contains(QStringLiteral("author:")));
}

void IssueQueryTest::buildCreatedOpen()
{
    const QList<QString> queries = IssueQuery::buildQueryStrings(IssueQuery::View::Created,
                                                                 QStringLiteral("octocat"),
                                                                 IssueQuery::State::Open);
    QCOMPARE(queries.size(), 1);
    QVERIFY(queries.at(0).contains(QStringLiteral("author:octocat")));
}

void IssueQueryTest::buildEmptyLogin()
{
    QVERIFY(IssueQuery::buildQueryStrings(IssueQuery::View::All, QString(), IssueQuery::State::Open).isEmpty());
}

void IssueQueryTest::buildSearchUrl()
{
    const QUrl url = IssueQuery::buildSearchUrl(QStringLiteral("is:issue is:open author:octocat"), 3);
    QCOMPARE(url.host(), QStringLiteral("api.github.com"));
    QCOMPARE(url.path(), QStringLiteral("/search/issues"));
    QVERIFY(url.query().contains(QStringLiteral("per_page=100")));
    QVERIFY(url.query().contains(QStringLiteral("page=3")));
    QVERIFY(url.query().contains(QStringLiteral("sort=updated")));
    QVERIFY(url.query().contains(QStringLiteral("order=desc")));
}

namespace
{
QByteArray itemJson(qlonglong id, int number, const char *title, const char *repo, bool isPr)
{
    QByteArray json = QByteArray("{\"id\":") + QByteArray::number(id) + ",\"number\":"
        + QByteArray::number(number) + ",\"title\":\"" + title + "\",\"state\":\"open\","
        + "\"html_url\":\"https://github.com/" + repo + "/issues/" + QByteArray::number(number) + "\","
        + "\"updated_at\":\"2026-10-01T10:00:00Z\","
        + "\"repository_url\":\"https://api.github.com/repos/" + repo + "\","
        + "\"labels\":[{\"name\":\"bug\"}]";
    if (isPr) {
        json += ",\"pull_request\":{\"url\":\"https://api.github.com/x\"}";
    }
    return json + "}";
}
}

void IssueQueryTest::parseValidPageAuthored()
{
    const QByteArray body = "{\"total_count\":2,\"incomplete_results\":false,\"items\":["
        + itemJson(101, 7, "Fix bug", "o/r", false) + "," + itemJson(102, 8, "Docs", "o/r", false) + "]}";
    IssueQuery::SearchPage page;
    QVERIFY(IssueQuery::parseSearchPage(body, IssueQuery::QueryKind::Authored, &page));
    QVERIFY(page.ok);
    QCOMPARE(page.issues.size(), 2);
    QCOMPARE(page.totalCount, 2);
    QVERIFY(!page.incompleteResults);
    const Issue &first = page.issues.at(0);
    QCOMPARE(first.id, 101LL);
    QCOMPARE(first.owner, QStringLiteral("o"));
    QCOMPARE(first.repo, QStringLiteral("r"));
    QCOMPARE(first.number, 7);
    QCOMPARE(first.qualifiedNumber(), QStringLiteral("o/r#7"));
    QCOMPARE(first.labels, QStringList{QStringLiteral("bug")});
    QVERIFY(first.updatedAt.isValid());
    QVERIFY(first.isAuthoredByMe);
    QVERIFY(!first.isAssignedToMe);
}

void IssueQueryTest::parseUnknownKindSetsNeitherFlag()
{
    // The fetcher parses neutrally; provenance is applied per query by the
    // caller. Unknown must never imply assignment.
    const QByteArray body = "{\"total_count\":1,\"incomplete_results\":false,\"items\":["
        + itemJson(101, 7, "Fix bug", "o/r", false) + "]}";
    IssueQuery::SearchPage page;
    QVERIFY(IssueQuery::parseSearchPage(body, IssueQuery::QueryKind::Unknown, &page));
    QVERIFY(page.ok);
    QCOMPARE(page.issues.size(), 1);
    QVERIFY(!page.issues.at(0).isAuthoredByMe);
    QVERIFY(!page.issues.at(0).isAssignedToMe);
}

void IssueQueryTest::parseSkipsPullRequestsAndIdless()
{
    const QByteArray body = "{\"total_count\":3,\"incomplete_results\":false,\"items\":["
        + itemJson(101, 7, "Real", "o/r", false) + "," + itemJson(103, 9, "PR", "o/r", true) + ","
        + QByteArray("{\"number\":1,\"title\":\"noid\"}") + "]}";
    IssueQuery::SearchPage page;
    QVERIFY(IssueQuery::parseSearchPage(body, IssueQuery::QueryKind::Assigned, &page));
    QCOMPARE(page.issues.size(), 1);
    QVERIFY(page.issues.at(0).isAssignedToMe);
}

void IssueQueryTest::parseMalformed()
{
    IssueQuery::SearchPage page;
    QVERIFY(!IssueQuery::parseSearchPage(QByteArray("{nope"), IssueQuery::QueryKind::Authored, &page));
    QVERIFY(!page.errorString.isEmpty());
}

void IssueQueryTest::parseMissingItems()
{
    IssueQuery::SearchPage page;
    QVERIFY(!IssueQuery::parseSearchPage(QByteArray("{\"total_count\":0}"), IssueQuery::QueryKind::Authored,
                                         &page));
}

void IssueQueryTest::parseIncompleteAndTotal()
{
    const QByteArray body = "{\"total_count\":1500,\"incomplete_results\":true,\"items\":[]}";
    IssueQuery::SearchPage page;
    QVERIFY(IssueQuery::parseSearchPage(body, IssueQuery::QueryKind::Authored, &page));
    QVERIFY(page.incompleteResults);
    QCOMPARE(page.totalCount, 1500);
}

void IssueQueryTest::mergeDedupsAndFlags()
{
    Issue a;
    a.id = 1;
    a.isAuthoredByMe = true;
    Issue b;
    b.id = 1;
    b.isAssignedToMe = true;
    Issue c;
    c.id = 2;
    c.isAssignedToMe = true;
    const QList<Issue> merged = IssueQuery::mergeAndSort({{a}, {b, c}});
    QCOMPARE(merged.size(), 2);
    // First occurrence position wins; flags OR across queries.
    QVERIFY(merged.at(0).isAuthoredByMe);
    QVERIFY(merged.at(0).isAssignedToMe);
    QVERIFY(!merged.at(1).isAuthoredByMe);
}

void IssueQueryTest::mergeSortsNewestFirst()
{
    Issue oldIssue;
    oldIssue.id = 1;
    oldIssue.updatedAt = QDateTime::fromString(QStringLiteral("2026-01-01T00:00:00Z"), Qt::ISODate);
    Issue newIssue;
    newIssue.id = 2;
    newIssue.updatedAt = QDateTime::fromString(QStringLiteral("2026-10-01T00:00:00Z"), Qt::ISODate);
    Issue dateless;
    dateless.id = 3;
    const QList<Issue> merged = IssueQuery::mergeAndSort({{oldIssue, dateless, newIssue}});
    QCOMPARE(merged.size(), 3);
    QCOMPARE(merged.at(0).id, 2LL);
    QCOMPARE(merged.at(1).id, 1LL);
    QCOMPARE(merged.at(2).id, 3LL); // unknown dates last
}

void IssueQueryTest::nextLinkFound()
{
    const QByteArray link = "<https://api.github.com/search/issues?q=x&page=2>; rel=\"next\", "
                            "<https://api.github.com/search/issues?q=x&page=5>; rel=\"last\"";
    const QUrl next = IssueQuery::parseNextLink(link);
    QVERIFY(next.isValid());
    QVERIFY(next.toString().contains(QStringLiteral("page=2")));
}

void IssueQueryTest::nextLinkAbsent()
{
    const QByteArray link = "<https://api.github.com/search/issues?q=x&page=5>; rel=\"last\"";
    QVERIFY(!IssueQuery::parseNextLink(link).isValid());
    QVERIFY(!IssueQuery::parseNextLink(QByteArray()).isValid());
    QVERIFY(!IssueQuery::parseNextLink(QByteArray("garbage")).isValid());
}

void IssueQueryTest::nextLinkRejectsNonHttps()
{
    const QByteArray link = "<http://evil.example/page>; rel=\"next\"";
    QVERIFY(!IssueQuery::parseNextLink(link).isValid());
}

void IssueQueryTest::nextLinkRejectsForeignHttpsHost()
{
    // A foreign HTTPS host is syntactically a next page, but following it
    // with the Bearer token would leak credentials: the safe parser must
    // refuse it while the raw extractor still reports its presence so the
    // caller can mark the list partial instead of silently complete.
    const QByteArray link = "<https://evil.example/next>; rel=\"next\"";
    QVERIFY(!IssueQuery::parseNextLink(link).isValid());
    const QUrl raw = IssueQuery::extractNextLinkRaw(link);
    QVERIFY(raw.isValid());
    QCOMPARE(raw.host(), QStringLiteral("evil.example"));
    QVERIFY(!IssueQuery::isAllowedIssueApiUrl(raw));
}

void IssueQueryTest::nextLinkAllowsApiOriginOnly()
{
    QVERIFY(IssueQuery::isAllowedIssueApiUrl(QUrl(QStringLiteral("https://api.github.com/search/issues"))));
    QVERIFY(!IssueQuery::isAllowedIssueApiUrl(QUrl(QStringLiteral("http://api.github.com/search/issues"))));
    QVERIFY(!IssueQuery::isAllowedIssueApiUrl(QUrl(QStringLiteral("https://api.github.com:8443/search/issues"))));
    QVERIFY(!IssueQuery::isAllowedIssueApiUrl(QUrl(QStringLiteral("https://github.com/search/issues"))));
    QVERIFY(!IssueQuery::isAllowedIssueApiUrl(QUrl()));
}

void IssueQueryTest::cappedResults()
{
    QVERIFY(IssueQuery::isCappedResult(1500, false, 200));
    QVERIFY(IssueQuery::isCappedResult(500, true, 100));
    QVERIFY(!IssueQuery::isCappedResult(500, false, 500));
    QVERIFY(!IssueQuery::isCappedResult(-1, false, 0));
}

void IssueQueryTest::updateUrlValid()
{
    const QUrl url = IssueQuery::buildIssueUpdateUrl(QStringLiteral("o"), QStringLiteral("r"), 7);
    QVERIFY(url.isValid());
    QCOMPARE(url.host(), QStringLiteral("api.github.com"));
    QVERIFY(url.path().contains(QStringLiteral("/repos/o/r/issues/7")));
}

void IssueQueryTest::updateUrlRejectsBadCoords()
{
    QVERIFY(!IssueQuery::buildIssueUpdateUrl(QString(), QStringLiteral("r"), 7).isValid());
    QVERIFY(!IssueQuery::buildIssueUpdateUrl(QStringLiteral("o"), QString(), 7).isValid());
    QVERIFY(!IssueQuery::buildIssueUpdateUrl(QStringLiteral("o"), QStringLiteral("r"), 0).isValid());
    QVERIFY(!IssueQuery::buildIssueUpdateUrl(QStringLiteral("o/x"), QStringLiteral("r"), 7).isValid());
    QVERIFY(!IssueQuery::buildIssueUpdateUrl(QStringLiteral("o"), QStringLiteral("r/x"), 7).isValid());
}

void IssueQueryTest::updateBodyCloseHasReason()
{
    const QByteArray body = IssueQuery::buildIssueUpdateBody(true);
    QVERIFY(body.contains("\"state\""));
    QVERIFY(body.contains("\"closed\""));
    QVERIFY(body.contains("\"completed\""));
}

void IssueQueryTest::updateBodyReopenHasNoReason()
{
    const QByteArray body = IssueQuery::buildIssueUpdateBody(false);
    QVERIFY(body.contains("\"open\""));
    QVERIFY(!body.contains("\"completed\""));
}

void IssueQueryTest::parseUpdatedStateStrict()
{
    QString state;
    QVERIFY(IssueQuery::parseUpdatedIssueState(QByteArray("{\"state\":\"closed\"}"), &state));
    QCOMPARE(state, QStringLiteral("closed"));
    QVERIFY(IssueQuery::parseUpdatedIssueState(QByteArray("{\"state\":\"open\"}"), &state));
    QCOMPARE(state, QStringLiteral("open"));
    QVERIFY(!IssueQuery::parseUpdatedIssueState(QByteArray("{\"state\":\"bogus\"}"), &state));
    QVERIFY(!IssueQuery::parseUpdatedIssueState(QByteArray("{broken"), &state));
}

QTEST_MAIN(IssueQueryTest)
#include "tst_issuequery.moc"
