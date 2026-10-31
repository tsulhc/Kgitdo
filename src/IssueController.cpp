#include "IssueController.h"

#include <QSet>

IssueController::IssueController(AuthSession *authSession, GitHubClient *client, QObject *parent)
    : QObject(parent)
    , m_auth(authSession)
    , m_client(client)
    , m_model(new IssueListModel(this))
{
    if (m_auth) {
        // Signing out invalidates the token the list was loaded with:
        // drop everything rather than showing another user's tasks.
        connect(m_auth, &AuthSession::statusChanged, this, [this]() {
            if (m_auth->isAuthenticated()) {
                return;
            }
            // Invalidate any in-flight fetch first: its callback must not
            // repopulate the list with the pre-logout token's data, even
            // when the list is currently empty. Bump the generation so the
            // stale callback is ignored, and release the loading guard so a
            // later sign-in can reload immediately.
            ++m_generation;
            setLoading(false);
            m_loaded.clear();
            m_model->setIssues({});
            m_availableRepos.clear();
            Q_EMIT availableReposChanged();
            // A leftover filter would silently hide the next account's
            // issues after re-login, so reset it and notify observers.
            if (!m_repoFilter.isEmpty()) {
                m_repoFilter.clear();
                Q_EMIT repoFilterChanged();
            }
            m_errorMessage.clear();
            Q_EMIT errorMessageChanged();
            m_warningMessage.clear();
            Q_EMIT warningChanged();
            clearOutcomeFlags();
            m_lastRefresh = QDateTime();
            Q_EMIT lastRefreshChanged();
            Q_EMIT countsChanged();
        });
    }
}

void IssueController::setView(int view)
{
    if (view < ViewAll || view > ViewCreated || view == m_view) {
        return;
    }
    m_view = view;
    Q_EMIT viewChanged();
    reload();
}

void IssueController::setIssueState(int state)
{
    if ((state != StateOpen && state != StateClosed) || state == m_issueState) {
        return;
    }
    m_issueState = state;
    Q_EMIT issueStateChanged();
    reload();
}

void IssueController::setRepoFilter(const QString &repoFullName)
{
    const QString trimmed = repoFullName.trimmed();
    if (trimmed == m_repoFilter) {
        return;
    }
    m_repoFilter = trimmed;
    Q_EMIT repoFilterChanged();
    applyRepoFilter();
}

void IssueController::reload()
{
    if (m_loading || !m_client) {
        return; // Double activation guard: one fetch at a time.
    }
    if (!m_auth || !m_auth->isAuthenticated() || m_auth->userLogin().isEmpty()
        || m_auth->currentAccessToken().isEmpty()) {
        m_errorMessage = QStringLiteral("Sign in to load issues.");
        Q_EMIT errorMessageChanged();
        return;
    }
    const IssueQuery::View view = m_view == ViewAssigned
        ? IssueQuery::View::Assigned
        : (m_view == ViewCreated ? IssueQuery::View::Created : IssueQuery::View::All);
    const IssueQuery::State state = m_issueState == StateClosed ? IssueQuery::State::Closed
                                                                : IssueQuery::State::Open;
    const QString token = m_auth->currentAccessToken();
    const QString login = m_auth->userLogin();
    const int viewAtStart = m_view;
    const int stateAtStart = m_issueState;
    const int generation = ++m_generation;
    setLoading(true);
    m_errorMessage.clear();
    Q_EMIT errorMessageChanged();
    m_client->fetchIssues(view, state, token, login,
                          [this, viewAtStart, stateAtStart, generation](
                              const GitHubClient::FetchResult &result) {
                              if (generation != m_generation) {
                                  // Superseded by logout (or a newer fetch):
                                  // touch nothing, not even the loading flag.
                                  return;
                              }
                              setLoading(false);
                              if (m_view != viewAtStart || m_issueState != stateAtStart) {
                                  // Selection changed mid-load: this data
                                  // belongs to the old selection. Discard it
                                  // and fetch once for the current one.
                                  reload();
                                  return;
                              }
                              m_incomplete = result.incomplete;
                              m_capped = result.capped;
                              m_truncated = result.truncated;
                              m_rateLimited = result.rateLimited;
                              m_retryAfterSecs = result.retryAfterSecs;
                              Q_EMIT outcomeChanged();
                              if (!result.ok) {
                                  clearOutcomeFlags();
                                  m_warningMessage.clear();
                                  Q_EMIT warningChanged();
                                  m_errorMessage = result.errorMessage.isEmpty()
                                      ? QStringLiteral("Could not load issues. Please retry.")
                                      : result.errorMessage;
                                  Q_EMIT errorMessageChanged();
                                  return;
                              }
                              m_loaded = result.issues;
                              QSet<QString> repos;
                              for (const Issue &issue : m_loaded) {
                                  const QString full = issue.repoFullName();
                                  if (!full.isEmpty()) {
                                      repos.insert(full);
                                  }
                              }
                              m_availableRepos = QStringList(repos.begin(), repos.end());
                              m_availableRepos.sort();
                              Q_EMIT availableReposChanged();
                              // Keep a repo filter that no longer matches
                              // anything (stale after reload) visible as an
                              // explicit empty choice rather than silently
                              // dropping it: the counts tell the story.
                              applyRepoFilter();
                              m_lastRefresh = QDateTime::currentDateTimeUtc();
                              Q_EMIT lastRefreshChanged();
                              if (!result.errorMessage.isEmpty()) {
                                  // Partial data: list what loaded, warn.
                                  m_warningMessage = result.errorMessage;
                              } else if (result.incomplete || result.capped || result.truncated) {
                                  m_warningMessage = result.capped
                                      ? QStringLiteral("GitHub caps search results at 1,000: the list may be partial.")
                                      : QStringLiteral(
                                            "GitHub could not return the complete list: it may be partial.");
                              } else {
                                  m_warningMessage.clear();
                              }
                              Q_EMIT warningChanged();
                          });
}

void IssueController::setLoading(bool loading)
{
    if (m_loading == loading) {
        return;
    }
    m_loading = loading;
    Q_EMIT loadingChanged();
}

void IssueController::applyRepoFilter()
{
    if (m_repoFilter.isEmpty()) {
        m_model->setIssues(m_loaded);
    } else {
        QList<Issue> filtered;
        for (const Issue &issue : m_loaded) {
            if (issue.repoFullName() == m_repoFilter) {
                filtered.append(issue);
            }
        }
        m_model->setIssues(filtered);
    }
    Q_EMIT countsChanged();
}

void IssueController::clearOutcomeFlags()
{
    m_incomplete = false;
    m_capped = false;
    m_truncated = false;
    m_rateLimited = false;
    m_retryAfterSecs = 0;
    Q_EMIT outcomeChanged();
}
