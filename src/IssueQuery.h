#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

#include "Issue.h"

// Pure helpers for the issue-reading contract (plan §3.3): search query
// construction, page parsing, Link pagination, merge/dedup/sort. No network,
// no credentials. Deterministic so unit tests use synthetic fixtures.
//
// Queries (GET https://api.github.com/search/issues):
//   is:issue is:open    author:<login>
//   is:issue is:open    assignee:<login>
// (and the same pair with is:closed for the Closed view).
namespace IssueQuery
{

enum class View {
    All, // author: + assignee: merged
    Assigned, // assignee: only
    Created, // author: only
};

enum class State {
    Open,
    Closed
};

enum class QueryKind {
    Authored, // item arrived via author:<login>
    Assigned, // item arrived via assignee:<login>
};

// One search query string for the given view (two for View::All, in
// author-first order so merge keeps author results ahead on equal dates).
QList<QString> buildQueryStrings(View view, const QString &login, State state);

// Full search URL for one query string + page (per_page=100, newest first).
QUrl buildSearchUrl(const QString &queryString, int page);

// Parsed search page.
struct SearchPage {
    bool ok = false;
    QList<Issue> issues; // PR entries already excluded
    bool incompleteResults = false; // API could not compute the full set
    int totalCount = -1; // -1 when absent
    QString errorString; // parse failure, no secrets
};

// Parse one /search/issues JSON body. Flags issues according to kind.
bool parseSearchPage(const QByteArray &body, QueryKind kind, SearchPage *out);

// Merge query results: deduplicate by stable id (OR-ing the assigned/
// authored flags when both queries return the same issue), then sort by
// updatedAt descending (unknown dates last).
QList<Issue> mergeAndSort(const QList<QList<Issue>> &perQuery);

// Next-page URL from an HTTP Link header; empty when there is no rel="next"
// or when the target leaves the GitHub API origin (the Bearer token must
// never be sent to another host). Use extractNextLinkRaw to distinguish
// "no further page" from "further page refused".
QUrl parseNextLink(const QByteArray &linkHeader);

// Raw rel="next" URL from a Link header, without origin checks.
QUrl extractNextLinkRaw(const QByteArray &linkHeader);

// Bearer-token issue requests may only target the GitHub API origin.
bool isAllowedIssueApiUrl(const QUrl &url);

// PATCH https://api.github.com/repos/{owner}/{repo}/issues/{number}:
// the single-issue mutation endpoint (plan §3.3). Invalid (empty) URL when
// the coordinates are missing or the path cannot be encoded.
QUrl buildIssueUpdateUrl(const QString &owner, const QString &repo, int number);

// Minimal mutation body: only the state changes, nothing else is
// overwritten. Closing marks the issue completed; reopening sends no
// state_reason (it only applies when closing).
QByteArray buildIssueUpdateBody(bool closed);

// Parse the PATCH response: success requires the returned `state` to match
// the requested one. False on malformed bodies or unknown states.
bool parseUpdatedIssueState(const QByteArray &body, QString *stateOut);

// True when the search result set cannot be complete: the Search API caps
// every query at 1.000 results.
bool isCappedResult(int totalCount, bool hasNextPage, int loadedCount);

} // namespace IssueQuery
