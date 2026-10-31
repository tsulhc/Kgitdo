#include "GitHubClient.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{

int rateLimitResetInSecs(const QMap<QString, QString> &headers)
{
    bool ok = false;
    const qint64 reset = headers.value(QStringLiteral("x-ratelimit-reset")).toLongLong(&ok);
    if (!ok || reset <= 0) {
        return 0;
    }
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    return static_cast<int>(qMax<qint64>(0, reset - now));
}

int retryAfterSecs(const QMap<QString, QString> &headers)
{
    bool ok = false;
    const int secs = headers.value(QStringLiteral("retry-after")).toInt(&ok);
    return (ok && secs > 0) ? secs : 0;
}

QString apiMessage(const QByteArray &body)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    return doc.object().value(QStringLiteral("message")).toString();
}

} // namespace

GitHubClient::MappedResponse GitHubClient::mapSearchResponse(const IssueHttpResult &http)
{
    MappedResponse out;
    if (!http.transportError.isEmpty() || http.httpStatus == 0) {
        out.errorMessage = http.transportError.isEmpty()
            ? QStringLiteral("Network request failed. Check the connection and retry.")
            : http.transportError;
        return out;
    }
    const int status = http.httpStatus;
    if (status == 401) {
        out.unauthorized = true;
        out.errorMessage = QStringLiteral("Session expired or revoked. Please sign in again.");
        return out;
    }
    if (status == 429) {
        out.rateLimited = true;
        out.retryAfterSecs = qMax(retryAfterSecs(http.headers), rateLimitResetInSecs(http.headers));
        out.errorMessage = QStringLiteral("GitHub rate limit reached. Wait a little and retry.");
        return out;
    }
    if (status == 403) {
        const bool exhausted = http.headers.value(QStringLiteral("x-ratelimit-remaining"), QStringLiteral("1"))
            == QStringLiteral("0");
        const QString message = apiMessage(http.body);
        if (exhausted || message.contains(QStringLiteral("rate limit"), Qt::CaseInsensitive)) {
            out.rateLimited = true;
            out.retryAfterSecs = qMax(retryAfterSecs(http.headers), rateLimitResetInSecs(http.headers));
            out.errorMessage = QStringLiteral("GitHub rate limit reached. Wait a little and retry.");
            return out;
        }
        out.errorMessage = message.isEmpty()
            ? QStringLiteral("GitHub refused the request. Check the app installation and retry.")
            : QStringLiteral("GitHub refused the request: %1").arg(message);
        return out;
    }
    if (status == 404) {
        out.errorMessage = QStringLiteral("GitHub did not find the requested resource.");
        return out;
    }
    if (status == 422) {
        const QString message = apiMessage(http.body);
        out.errorMessage = message.isEmpty()
            ? QStringLiteral("GitHub rejected the search. Please retry.")
            : QStringLiteral("GitHub rejected the search: %1").arg(message);
        return out;
    }
    if (status < 200 || status >= 300) {
        const QString message = apiMessage(http.body);
        out.errorMessage = message.isEmpty()
            ? QStringLiteral("GitHub request failed. Please retry.")
            : QStringLiteral("GitHub request failed: %1").arg(message);
        return out;
    }
    IssueQuery::SearchPage page;
    if (!IssueQuery::parseSearchPage(http.body, IssueQuery::QueryKind::Assigned, &page)) {
        out.errorMessage = page.errorString;
        return out;
    }
    out.ok = true;
    out.page = page;
    return out;
}

GitHubClient::GitHubClient(IIssueApi *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
}

// One fetch = sequential queries, each with sequential Link pagination.
// Sequential (not parallel) to stay well under the tight search rate
// limits. Each fetch owns a small Job so overlapping calls cannot mix
// pages or fire a callback twice.
class IssueFetchJob : public QObject
{
public:
    struct Shared {
        QList<QList<Issue>> perQuery;
        int pagesParsed = 0;
        bool incomplete = false;
        bool truncated = false;
        int maxTotalCount = -1;
        bool sawNextAtCap = false;
        bool unauthorized = false;
        bool rateLimited = false;
        int retryAfterSecs = 0;
        QString errorMessage;
    };

    IssueFetchJob(IIssueApi *api, const QList<QString> &queries,
                  const QList<IssueQuery::QueryKind> &kinds, const QString &token,
                  GitHubClient::FetchCallback callback, QObject *parent)
        : QObject(parent)
        , m_api(api)
        , m_queries(queries)
        , m_kinds(kinds)
        , m_token(token)
        , m_callback(std::move(callback))
    {
    }

    void start() { startQuery(0); }

private:
    void finish()
    {
        GitHubClient::FetchResult result;
        result.incomplete = m_shared.incomplete;
        result.truncated = m_shared.truncated;
        result.unauthorized = m_shared.unauthorized;
        result.rateLimited = m_shared.rateLimited;
        result.retryAfterSecs = m_shared.retryAfterSecs;
        result.errorMessage = m_shared.errorMessage;
        result.issues = IssueQuery::mergeAndSort(m_shared.perQuery);
        result.capped = IssueQuery::isCappedResult(m_shared.maxTotalCount, m_shared.sawNextAtCap,
                                                   result.issues.size());
        result.ok = m_shared.pagesParsed > 0;
        if (result.ok && !m_shared.errorMessage.isEmpty()) {
            // Any failure during a successful fetch means a query or page
            // is missing: the data is partial even when the failure struck
            // before the first page (the other query still succeeded).
            result.truncated = true;
        }
        m_callback(result);
        deleteLater();
    }

    void flagProvenance(QList<Issue> &issues) const
    {
        for (Issue &issue : issues) {
            if (m_queryKind == IssueQuery::QueryKind::Assigned) {
                issue.isAssignedToMe = true;
            } else {
                issue.isAuthoredByMe = true;
            }
        }
    }

    void startQuery(int queryIndex)
    {
        if (queryIndex >= m_queries.size()) {
            finish();
            return;
        }
        m_queryKind = m_kinds.value(queryIndex, IssueQuery::QueryKind::Authored);
        fetchPage(m_queries.at(queryIndex), 1, queryIndex);
    }

    void fetchPage(const QString &query, int page, int queryIndex)
    {
        m_api->searchIssues(IssueQuery::buildSearchUrl(query, page), m_token,
                            [this, query, page, queryIndex](const IssueHttpResult &http) {
                                GitHubClient::MappedResponse mapped = GitHubClient::mapSearchResponse(http);
                                if (!mapped.ok) {
                                    noteFailure(mapped);
                                    // A failed query never aborts the fetch:
                                    // the other query may still succeed and
                                    // partial data beats no data (truncated).
                                    if (queryIndex + 1 < m_queries.size()) {
                                        startQuery(queryIndex + 1);
                                    } else {
                                        finish();
                                    }
                                    return;
                                }
                                // Re-flag provenance: the mapper parses
                                // neutrally; the query decides the flag.
                                flagProvenance(mapped.page.issues);
                                m_shared.pagesParsed++;
                                if (mapped.page.incompleteResults) {
                                    m_shared.incomplete = true;
                                }
                                if (mapped.page.totalCount > m_shared.maxTotalCount) {
                                    m_shared.maxTotalCount = mapped.page.totalCount;
                                }
                                m_shared.perQuery.append(mapped.page.issues);
                                const QUrl next = IssueQuery::parseNextLink(
                                    http.headers.value(QStringLiteral("link")).toUtf8());
                                if (!next.isEmpty() && page < GitHubClient::kMaxPagesPerQuery) {
                                    fetchUrl(next, queryIndex, page + 1);
                                } else {
                                    if (!next.isEmpty()) {
                                        m_shared.sawNextAtCap = true;
                                    }
                                    startQuery(queryIndex + 1);
                                }
                            });
    }

    void fetchUrl(const QUrl &url, int queryIndex, int page)
    {
        m_api->searchIssues(url, m_token, [this, queryIndex, page](const IssueHttpResult &http) {
            GitHubClient::MappedResponse mapped = GitHubClient::mapSearchResponse(http);
            if (!mapped.ok) {
                noteFailure(mapped);
                if (queryIndex + 1 < m_queries.size()) {
                    startQuery(queryIndex + 1);
                } else {
                    finish();
                }
                return;
            }
            m_shared.pagesParsed++;
            if (mapped.page.incompleteResults) {
                m_shared.incomplete = true;
            }
            if (mapped.page.totalCount > m_shared.maxTotalCount) {
                m_shared.maxTotalCount = mapped.page.totalCount;
            }
            // Provenance for continuation pages matches the query kind.
            flagProvenance(mapped.page.issues);
            m_shared.perQuery.append(mapped.page.issues);
            const QUrl next = IssueQuery::parseNextLink(
                http.headers.value(QStringLiteral("link")).toUtf8());
            if (!next.isEmpty() && page < GitHubClient::kMaxPagesPerQuery) {
                fetchUrl(next, queryIndex, page + 1);
            } else {
                if (!next.isEmpty()) {
                    m_shared.sawNextAtCap = true;
                }
                startQuery(queryIndex + 1);
            }
        });
    }

    void noteFailure(const GitHubClient::MappedResponse &mapped)
    {
        if (mapped.unauthorized) {
            m_shared.unauthorized = true;
        }
        if (mapped.rateLimited) {
            m_shared.rateLimited = true;
            if (mapped.retryAfterSecs > m_shared.retryAfterSecs) {
                m_shared.retryAfterSecs = mapped.retryAfterSecs;
            }
        }
        if (m_shared.errorMessage.isEmpty() && !mapped.errorMessage.isEmpty()) {
            m_shared.errorMessage = mapped.errorMessage;
        }
        if (m_shared.pagesParsed > 0) {
            m_shared.truncated = true;
        }
    }

    Shared m_shared;
    IIssueApi *m_api = nullptr;
    QList<QString> m_queries;
    QList<IssueQuery::QueryKind> m_kinds;
    QString m_token;
    GitHubClient::FetchCallback m_callback;
    IssueQuery::QueryKind m_queryKind = IssueQuery::QueryKind::Authored;
};

void GitHubClient::fetchIssues(IssueQuery::View view, IssueQuery::State state, const QString &token,
                               const QString &login, FetchCallback callback)
{
    const QList<QString> queries = IssueQuery::buildQueryStrings(view, login, state);
    if (queries.isEmpty() || token.isEmpty()) {
        FetchResult result;
        result.errorMessage = QStringLiteral("Sign in to load issues.");
        callback(result);
        return;
    }
    QList<IssueQuery::QueryKind> kinds;
    for (int i = 0; i < queries.size(); ++i) {
        // Author-first order (see buildQueryStrings): index 0 of a
        // two-query fetch is the author query; single-query views flag
        // uniformly by view.
        if (queries.size() == 1) {
            kinds.append(view == IssueQuery::View::Assigned ? IssueQuery::QueryKind::Assigned
                                                            : IssueQuery::QueryKind::Authored);
        } else {
            kinds.append(i == 0 ? IssueQuery::QueryKind::Authored : IssueQuery::QueryKind::Assigned);
        }
    }
    auto *job = new IssueFetchJob(m_api, queries, kinds, token, std::move(callback), this);
    job->start();
}
