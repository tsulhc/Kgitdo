#include "IssueController.h"

#include <QSet>
#include <algorithm>

IssueController::IssueController(AuthSession *authSession, GitHubClient *client, QObject *parent)
    : QObject(parent)
    , m_auth(authSession)
    , m_client(client)
    , m_model(new IssueListModel(this))
{
    if (m_auth) {
        // Seed the identity the controller is constructed under: restore()
        // may have completed (and emitted Authenticated) before this
        // controller connected, so without seeding the next return to
        // Authenticated — e.g. a routine token refresh — would look like a
        // new identity and wipe + needlessly reload a valid list.
        if (m_auth->isAuthenticated()) {
            m_lastLogin = m_auth->userLogin();
        }
        // Signing out invalidates the token the list was loaded with:
        // drop everything rather than showing another user's tasks.
        // Only an actual sign-out clears: Refreshing is a transient
        // renewal (manual refresh or restore) and Failed keeps the prior
        // tokens in the session, so neither must discard valid issue
        // state. Terminal revocations (401/bad refresh token) already
        // transition to SignedOut, which is where cleanup happens.
        // A fresh sign-in as a different login without an intermediate
        // sign-out (refresh failure followed by a new device flow) lands
        // on Authenticated with a changed login: that is a new identity
        // and must also drop the prior account's state, then reload.
        connect(m_auth, &AuthSession::statusChanged, this, [this]() {
            if (m_auth->status() == AuthSession::SignedOut) {
                clearAllState();
                m_lastLogin.clear();
                return;
            }
            if (m_auth->status() == AuthSession::Authenticated
                && m_auth->userLogin() != m_lastLogin) {
                clearAllState();
                m_lastLogin = m_auth->userLogin();
                reload();
            }
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
                              // Fetch-owned outcome: a later mutation success
                              // must not retire it (partial-list or load
                              // warnings stay until the next load resolves).
                              m_rateLimitedMutationId = 0;
                              Q_EMIT outcomeChanged();
                              if (!result.ok) {
                                  // A failed load carries no data, so the
                                  // partial-data flags are meaningless — but
                                  // the server condition still stands: a rate
                                  // limit governs when retry is allowed, and
                                  // the outcome card counts down next to the
                                  // error. Preserve rateLimited/retryAfterSecs
                                  // (assigned from result above).
                                  m_incomplete = false;
                                  m_capped = false;
                                  m_truncated = false;
                                  Q_EMIT outcomeChanged();
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
                              // The model was just rebuilt, losing its visual
                              // pending flags, but outstanding mutations are
                              // still live (plain reloads never fence them):
                              // re-mark their rows so a second activation
                              // stays visibly busy until the PATCH lands.
                              for (const qlonglong guarded : m_mutationGuard) {
                                  m_model->setIssuePending(guarded, true);
                              }
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
    m_rateLimitedMutationId = 0;
    Q_EMIT outcomeChanged();
}

void IssueController::closeIssue(const QString &issueId)
{
    bool ok = false;
    const qlonglong id = issueId.toLongLong(&ok);
    if (ok) {
        sendStateChange(id, true, false);
    }
}

void IssueController::reopenIssue(const QString &issueId)
{
    bool ok = false;
    const qlonglong id = issueId.toLongLong(&ok);
    if (ok) {
        sendStateChange(id, false, false);
    }
}

void IssueController::undoLastClose()
{
    if (m_undo.id == 0) {
        return;
    }
    // Keep the snapshot until the reopen succeeds: a failed undo must stay
    // retryable via the banner rather than losing the issue coordinates.
    sendStateChange(m_undo.id, false, true);
}

void IssueController::dismissUndo()
{
    clearUndo();
}

void IssueController::sendStateChange(qlonglong id, bool closed, bool isUndo)
{
    if (!m_client || !m_auth || !m_auth->isAuthenticated()
        || m_auth->currentAccessToken().isEmpty()) {
        return;
    }
    // Authoritative double-send guard: the model's pending flag is visual
    // state that fetches rebuild, so only this set can prove no PATCH is
    // outstanding for the id. It is checked BEFORE any state mutation (the
    // undo-in-flight flag below, the model pending flag further down): a
    // rejected overlapping request must leave no flag behind. Entries are
    // added just before dispatch and released when the PATCH callback of
    // the same account completes — never by a plain reload; clearAllState
    // wipes them on logout/account change.
    if (m_mutationGuard.contains(id)) {
        return; // PATCH already outstanding for this issue.
    }
    if (isUndo && (m_undo.id == 0 || m_undo.id != id || m_undoInFlight)) {
        return; // No snapshot, stale target, or undo already in flight.
    }
    // Copy before any list mutation: references into m_loaded dangle after
    // removeAt/append/sort.
    Issue target;
    QString targetNumber;
    if (isUndo) {
        // Undo reopens an issue already dropped from the list: the row
        // guards below cannot apply, so rebuild from the snapshot.
        target.id = m_undo.id;
        target.owner = m_undo.owner;
        target.repo = m_undo.repo;
        target.number = m_undo.number;
        target.state = QStringLiteral("closed");
        // Restore scope provenance: without it an undo landing in the
        // Assigned/Created view would always look out of scope.
        target.isAssignedToMe = m_undo.isAssignedToMe;
        target.isAuthoredByMe = m_undo.isAuthoredByMe;
        targetNumber = target.qualifiedNumber();
        m_undoInFlight = true;
    } else {
        const int row = loadedRow(id);
        if (row < 0) {
            return;
        }
        const Issue &issue = m_loaded.at(row);
        // Only forward transitions make sense: closing an open issue or
        // reopening a closed one. Anything else is a stale UI event.
        if ((closed && issue.state != QLatin1String("open"))
            || (!closed && issue.state != QLatin1String("closed"))) {
            return;
        }
        if (m_model->isIssuePending(issue.id)) {
            return; // Already in flight: double-activation guard.
        }
        if (!m_model->setIssuePending(issue.id, true)) {
            return; // Absent from the visible slice: stale UI event.
        }
        target = issue;
        targetNumber = target.qualifiedNumber();
    }
    // The guard was checked before any state mutation above; the entry is
    // added only now that every row guard passed and dispatch is certain.
    m_mutationGuard.insert(id);
    const QString token = m_auth->currentAccessToken();
    const QString login = m_auth->userLogin();
    const int accountGeneration = m_accountGeneration;
    m_client->updateIssueState(target.owner, target.repo, target.number, closed, token,
        [this, id, closed, isUndo, login, accountGeneration, target, targetNumber](
            const GitHubClient::UpdateResult &result) {
            if (accountGeneration != m_accountGeneration) {
                // Superseded by logout or a new-identity sign-in: clearAllState
                // already wiped this callback's guard entry, and the new
                // account may have started its own mutation for the same
                // issue id — so remove nothing here, or the stale callback
                // would release the new account's guard and allow a
                // duplicate PATCH. The same holds for the undo flag: the new
                // account owns it now, so a fenced callback must touch no
                // current-account state at all.
                return;
            }
            m_model->setIssuePending(id, false);
            if (!result.ok) {
                // Keep the list visible: mutation failures are warnings,
                // not load errors, and the row stays with its old state.
                // A rate limit still governs retry timing, so propagate it
                // for the outcome countdown like the load path does.
                m_mutationGuard.remove(id);
                if (isUndo) {
                    m_undoInFlight = false;
                }
                m_rateLimited = result.rateLimited;
                m_retryAfterSecs = result.retryAfterSecs;
                // This mutation owns the outcome it just set (or cleared by
                // failing without a rate limit): a later success retires it
                // only when it is the same mutation's retry.
                m_rateLimitedMutationId = result.rateLimited ? id : 0;
                Q_EMIT outcomeChanged();
                m_warningMessage = result.errorMessage.isEmpty()
                    ? QStringLiteral("Could not update the issue. Please retry.")
                    : result.errorMessage;
                Q_EMIT warningChanged();
                return;
            }
            if (m_auth && m_auth->userLogin() != login) {
                // Identity changed mid-mutation: drop the result rather
                // than mixing accounts. The new identity's reload (or the
                // next refresh) reconverges with the server. The guard entry
                // was already wiped by clearAllState and may have been
                // re-taken by the new account, so remove nothing here.
                return;
            }
            // Success: the server echoed the requested state.
            m_mutationGuard.remove(id);
            // A success proves the server condition behind a previous
            // mutation failure is over — but only retire the countdown when
            // this success is that failure's retry. A load-owned outcome
            // (partial list still on screen) or another mutation's warning
            // stays until its own owner resolves it.
            if (m_rateLimited && m_rateLimitedMutationId == id) {
                m_rateLimited = false;
                m_retryAfterSecs = 0;
                m_rateLimitedMutationId = 0;
                Q_EMIT outcomeChanged();
                m_warningMessage.clear();
                Q_EMIT warningChanged();
            }
            if (closed && !isUndo) {
                // Snapshot the coordinates for one-shot undo.
                m_undo.id = id;
                m_undo.owner = target.owner;
                m_undo.repo = target.repo;
                m_undo.number = target.number;
                m_undo.isAssignedToMe = target.isAssignedToMe;
                m_undo.isAuthoredByMe = target.isAuthoredByMe;
                m_undoMessage = QStringLiteral("Closed %1 — undo?").arg(targetNumber);
                Q_EMIT undoChanged();
            } else if (!closed && isUndo) {
                m_undoInFlight = false;
                // Retire the snapshot only if it still belongs to this
                // undo: a newer close may have replaced it while this
                // reopen was in flight, and that offer must survive.
                if (m_undo.id == id) {
                    clearUndo();
                }
            } else {
                if (isUndo) {
                    m_undoInFlight = false;
                }
                clearUndo();
            }
            // Converge the row against the CURRENT filter, which may have
            // changed while the PATCH was in flight. When the confirmed
            // state belongs in this view, ensure the row is listed with
            // that state (append, or patch a raced fetch's row while
            // keeping its fresher server fields); when it left this view,
            // drop the row if still listed. Either way no extra fetch is
            // needed and no wrong-state row can appear.
            const bool confirmedInView = (closed && m_issueState == StateClosed)
                || (!closed && m_issueState == StateOpen);
            // Scope view matters too: the state filter alone cannot prove
            // membership in Assigned/Created. A close started in All may
            // succeed after the user switched to Assigned, and an
            // unassigned issue must not be appended to that list.
            const bool inScopeView = (m_view == ViewAll)
                || (m_view == ViewAssigned && target.isAssignedToMe)
                || (m_view == ViewCreated && target.isAuthoredByMe);
            if (confirmedInView && inScopeView) {
                Issue converged = target;
                converged.state = closed ? QStringLiteral("closed") : QStringLiteral("open");
                // The row must be interactive: target may predate the latest
                // fetch cycle, so never carry a stale pending flag into the
                // list (the guard, not the flag, proves no PATCH is out).
                converged.pending = false;
                const int existing = loadedRow(converged.id);
                if (existing < 0) {
                    m_loaded.append(converged);
                } else {
                    m_loaded[existing].state = converged.state;
                }
                std::sort(m_loaded.begin(), m_loaded.end(), [](const Issue &a, const Issue &b) {
                    if (a.updatedAt.isValid() != b.updatedAt.isValid()) {
                        return a.updatedAt.isValid();
                    }
                    return a.updatedAt > b.updatedAt;
                });
                applyRepoFilter();
            } else {
                const int current = loadedRow(id);
                if (current >= 0) {
                    m_loaded.removeAt(current);
                    m_model->removeIssue(id);
                    Q_EMIT countsChanged();
                }
            }
        });
}

void IssueController::clearUndo()
{
    m_undoInFlight = false;
    if (m_undo.id == 0 && m_undoMessage.isEmpty()) {
        return;
    }
    m_undo = UndoClose();
    m_undoMessage.clear();
    Q_EMIT undoChanged();
}

int IssueController::loadedRow(qlonglong id) const
{
    for (int i = 0; i < m_loaded.size(); ++i) {
        if (m_loaded.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

void IssueController::clearAllState()
{
    // Invalidate any in-flight fetch first: its callback must not
    // repopulate the list with the previous token's data, even when the
    // list is currently empty. Bump the generation so the stale callback
    // is ignored, and release the loading guard so a later sign-in can
    // reload immediately.
    ++m_generation;
    ++m_accountGeneration; // mutations in flight belong to the old account
    m_mutationGuard.clear(); // outstanding PATCHes are fenced: their guard dies here
    setLoading(false);
    m_loaded.clear();
    m_model->setIssues({});
    m_availableRepos.clear();
    Q_EMIT availableReposChanged();
    // A leftover filter would silently hide the next account's issues
    // after re-login, so reset it and notify observers.
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
    clearUndo();
    Q_EMIT countsChanged();
}
