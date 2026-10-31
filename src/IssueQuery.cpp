#include "IssueQuery.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrlQuery>
#include <algorithm>

namespace IssueQuery
{

namespace
{

QString stateToken(State state)
{
    return state == State::Closed ? QStringLiteral("is:closed") : QStringLiteral("is:open");
}

// "owner/repo" from a ".../repos/OWNER/REPO" repository_url.
bool splitRepositoryUrl(const QString &repositoryUrl, QString *owner, QString *repo)
{
    const QString marker = QStringLiteral("/repos/");
    const int at = repositoryUrl.indexOf(marker);
    if (at < 0) {
        return false;
    }
    const QString tail = repositoryUrl.mid(at + marker.size());
    const int slash = tail.indexOf(QLatin1Char('/'));
    if (slash <= 0 || slash == tail.size() - 1) {
        return false;
    }
    *owner = tail.left(slash);
    *repo = tail.mid(slash + 1);
    if (owner->contains(QLatin1Char('/')) || repo->contains(QLatin1Char('/'))) {
        return false;
    }
    return true;
}

QDateTime parseUpdatedAt(const QJsonValue &value)
{
    if (!value.isString()) {
        return QDateTime();
    }
    QDateTime dt = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (!dt.isValid()) {
        dt = QDateTime::fromString(value.toString(), Qt::ISODate);
    }
    if (dt.isValid()) {
        dt.setTimeSpec(Qt::UTC);
    }
    return dt;
}

} // namespace

QList<QString> buildQueryStrings(View view, const QString &login, State state)
{
    QList<QString> queries;
    if (login.isEmpty()) {
        return queries;
    }
    const QString base = QStringLiteral("is:issue ") + stateToken(state) + QLatin1Char(' ');
    if (view == View::All || view == View::Created) {
        queries.append(base + QStringLiteral("author:") + login);
    }
    if (view == View::All || view == View::Assigned) {
        queries.append(base + QStringLiteral("assignee:") + login);
    }
    return queries;
}

QUrl buildSearchUrl(const QString &queryString, int page)
{
    QUrl url(QStringLiteral("https://api.github.com/search/issues"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("q"), queryString);
    params.addQueryItem(QStringLiteral("per_page"), QStringLiteral("100"));
    params.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
    params.addQueryItem(QStringLiteral("sort"), QStringLiteral("updated"));
    params.addQueryItem(QStringLiteral("order"), QStringLiteral("desc"));
    url.setQuery(params);
    return url;
}

QUrl buildIssueUpdateUrl(const QString &owner, const QString &repo, int number)
{
    if (owner.isEmpty() || repo.isEmpty() || number <= 0) {
        return QUrl();
    }
    QUrl url(QStringLiteral("https://api.github.com"));
    // Single segments: slashes in names would escape the path, so reject
    // them rather than building a URL that targets something else.
    if (owner.contains(QLatin1Char('/')) || repo.contains(QLatin1Char('/'))) {
        return QUrl();
    }
    url.setPath(QStringLiteral("/repos/") + owner + QLatin1Char('/') + repo + QStringLiteral("/issues/")
                + QString::number(number));
    return url;
}

QByteArray buildIssueUpdateBody(bool closed)
{
    if (closed) {
        return QByteArray("{\"state\":\"closed\",\"state_reason\":\"completed\"}");
    }
    return QByteArray("{\"state\":\"open\"}");
}

bool parseUpdatedIssueState(const QByteArray &body, QString *stateOut)
{
    if (!stateOut) {
        return false;
    }
    stateOut->clear();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QString state = doc.object().value(QStringLiteral("state")).toString();
    if (state != QStringLiteral("open") && state != QStringLiteral("closed")) {
        return false;
    }
    *stateOut = state;
    return true;
}

bool parseSearchPage(const QByteArray &body, QueryKind kind, SearchPage *out)
{
    if (!out) {
        return false;
    }
    *out = SearchPage();
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        out->errorString = QStringLiteral("Could not read the GitHub response. Please retry.");
        return false;
    }
    const QJsonObject root = doc.object();
    out->incompleteResults = root.value(QStringLiteral("incomplete_results")).toBool(false);
    if (root.contains(QStringLiteral("total_count")) && root.value(QStringLiteral("total_count")).isDouble()) {
        out->totalCount = root.value(QStringLiteral("total_count")).toInt(-1);
    }
    const QJsonValue itemsValue = root.value(QStringLiteral("items"));
    if (!itemsValue.isArray()) {
        out->errorString = QStringLiteral("Could not read the GitHub response. Please retry.");
        return false;
    }
    const QJsonArray items = itemsValue.toArray();
    for (const QJsonValue &itemValue : items) {
        if (!itemValue.isObject()) {
            continue;
        }
        const QJsonObject item = itemValue.toObject();
        // Pull requests are not tasks: the search index returns them for
        // is:issue-free queries, and defensively we skip any entry that
        // carries the pull_request marker.
        if (item.contains(QStringLiteral("pull_request"))) {
            continue;
        }
        Issue issue;
        issue.id = item.value(QStringLiteral("id")).toVariant().toLongLong();
        if (issue.id == 0) {
            continue; // No stable identity: cannot dedup, must not show.
        }
        issue.number = item.value(QStringLiteral("number")).toInt(0);
        issue.title = item.value(QStringLiteral("title")).toString();
        issue.state = item.value(QStringLiteral("state")).toString();
        issue.htmlUrl = item.value(QStringLiteral("html_url")).toString();
        issue.updatedAt = parseUpdatedAt(item.value(QStringLiteral("updated_at")));
        QString owner;
        QString repo;
        if (splitRepositoryUrl(item.value(QStringLiteral("repository_url")).toString(), &owner, &repo)) {
            issue.owner = owner;
            issue.repo = repo;
        }
        const QJsonValue labelsValue = item.value(QStringLiteral("labels"));
        if (labelsValue.isArray()) {
            for (const QJsonValue &labelValue : labelsValue.toArray()) {
                if (!labelValue.isObject()) {
                    continue;
                }
                const QString name = labelValue.toObject().value(QStringLiteral("name")).toString();
                if (!name.isEmpty()) {
                    issue.labels.append(name);
                }
            }
        }
        if (kind == QueryKind::Assigned) {
            issue.isAssignedToMe = true;
        } else if (kind == QueryKind::Authored) {
            issue.isAuthoredByMe = true;
        }
        // QueryKind::Unknown sets neither flag: provenance is decided by
        // the caller (see GitHubClient flagProvenance), never by the parse.
        out->issues.append(issue);
    }
    out->ok = true;
    return true;
}

QList<Issue> mergeAndSort(const QList<QList<Issue>> &perQuery)
{
    // Dedup by stable id; when both queries return the same issue the
    // assigned/authored flags are OR-ed so no provenance is lost.
    QHash<qlonglong, int> indexById;
    QList<Issue> merged;
    merged.reserve(256);
    for (const QList<Issue> &list : perQuery) {
        for (const Issue &issue : list) {
            const auto it = indexById.constFind(issue.id);
            if (it == indexById.constEnd()) {
                indexById.insert(issue.id, merged.size());
                merged.append(issue);
            } else {
                Issue &existing = merged[*it];
                existing.isAssignedToMe = existing.isAssignedToMe || issue.isAssignedToMe;
                existing.isAuthoredByMe = existing.isAuthoredByMe || issue.isAuthoredByMe;
            }
        }
    }
    std::stable_sort(merged.begin(), merged.end(), [](const Issue &a, const Issue &b) {
        if (a.updatedAt.isValid() != b.updatedAt.isValid()) {
            return a.updatedAt.isValid();
        }
        return a.updatedAt > b.updatedAt;
    });
    return merged;
}

QUrl extractNextLinkRaw(const QByteArray &linkHeader)
{
    // Link: <url>; rel="next", <url>; rel="last", ...
    const QList<QByteArray> parts = linkHeader.split(',');
    for (const QByteArray &part : parts) {
        const int open = part.indexOf('<');
        const int close = part.indexOf('>', open + 1);
        if (open < 0 || close < 0) {
            continue;
        }
        const QByteArray remainder = part.mid(close + 1);
        // rel value may be quoted with single/double quotes or bare; other
        // link params (type=, title=) must not confuse the match.
        bool isNext = false;
        int cursor = 0;
        while ((cursor = remainder.indexOf("rel", cursor)) >= 0) {
            int p = cursor + 3;
            while (p < remainder.size() && remainder.at(p) == ' ') {
                ++p;
            }
            if (p < remainder.size() && remainder.at(p) == '=') {
                ++p;
                while (p < remainder.size() && remainder.at(p) == ' ') {
                    ++p;
                }
                if (p < remainder.size()
                    && (remainder.at(p) == '"' || remainder.at(p) == '\'')) {
                    ++p;
                }
                if (remainder.mid(p, 4) == "next"
                    && (p + 4 >= remainder.size() || remainder.at(p + 4) == '"'
                        || remainder.at(p + 4) == '\'' || remainder.at(p + 4) == ' '
                        || remainder.at(p + 4) == ';')) {
                    isNext = true;
                    break;
                }
            }
            cursor += 3;
        }
        if (isNext) {
            const QUrl url(QString::fromUtf8(part.mid(open + 1, close - open - 1)));
            if (url.isValid() && !url.isRelative() && !url.scheme().isEmpty()
                && !url.host().isEmpty()) {
                return url;
            }
        }
    }
    return QUrl();
}

bool isAllowedIssueApiUrl(const QUrl &url)
{
    // The token travels as a Bearer header on every issue request, so the
    // pagination target must stay inside the GitHub API origin. Anything
    // else — another host, a non-HTTPS scheme, an unusual port — is
    // refused and the list is reported partial instead.
    return url.scheme() == QLatin1String("https") && url.host() == QLatin1String("api.github.com")
        && url.port(443) == 443;
}

QUrl parseNextLink(const QByteArray &linkHeader)
{
    const QUrl raw = extractNextLinkRaw(linkHeader);
    if (raw.isEmpty() || !isAllowedIssueApiUrl(raw)) {
        return QUrl();
    }
    return raw;
}

bool isCappedResult(int totalCount, bool hasNextPage, int loadedCount)
{
    // The Search API returns at most 1.000 results per query: beyond that
    // the list is necessarily partial and the UI must say so.
    if (totalCount > 1000) {
        return true;
    }
    // Defensive: a next page exists but we stopped (page guard). The known
    // total fits the cap, yet we did not load everything.
    return hasNextPage && loadedCount < totalCount;
}

} // namespace IssueQuery
