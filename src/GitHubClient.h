#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "IssueApi.h"
#include "IssueQuery.h"

// Reads the issue checklist from the GitHub REST search API using the
// W1 user token (Bearer). One fetch runs the view's queries (one or two)
// with Link pagination each, then merges, dedups by stable id, and sorts
// newest-first.
//
// Failure policy (plan §3.3): a fetch succeeds when at least one page
// parsed — interruptions after that set truncated=true with a warning in
// errorMessage rather than discarding data. Nothing is retried
// automatically; rate limits surface retryAfterSecs for the UI.
// (No Q_OBJECT: results travel through callbacks, like IAuthNetwork.)
class GitHubClient : public QObject
{
public:
    struct FetchResult {
        bool ok = false; // at least one page parsed
        QList<Issue> issues; // merged, deduplicated, newest-first
        bool incomplete = false; // API incomplete_results on any page
        bool truncated = false; // pagination/query interrupted: partial data
        bool capped = false; // search 1.000-result cap hit
        bool unauthorized = false; // HTTP 401: token revoked/expired
        bool rateLimited = false;
        int retryAfterSecs = 0;
        // Error when !ok, warning when ok (partial data). No secrets.
        QString errorMessage;
    };
    using FetchCallback = std::function<void(FetchResult)>;

    // Maps one raw HTTP search response to a fetch-level outcome. Pure
    // except for reading the clock (rate-limit reset math); covered by
    // unit tests with synthetic responses.
    struct MappedResponse {
        bool ok = false;
        IssueQuery::SearchPage page;
        bool unauthorized = false;
        bool rateLimited = false;
        int retryAfterSecs = 0;
        QString errorMessage;
    };
    static MappedResponse mapSearchResponse(const IssueHttpResult &http);

    explicit GitHubClient(IIssueApi *api, QObject *parent = nullptr);

    // view/state select the queries; token is the W1 Bearer token, login
    // the signed-in user. The callback always fires exactly once.
    void fetchIssues(IssueQuery::View view, IssueQuery::State state, const QString &token,
                     const QString &login, FetchCallback callback);

private:
    static constexpr int kMaxPagesPerQuery = 10; // 10 x 100 = search cap

    IIssueApi *m_api = nullptr;
};
