#include <QtTest>

#include "AuthSession.h"
#include "GitHubClient.h"
#include "IssueController.h"
#include "SecureStore.h"

// Minimal IAuthNetwork: only fetchUserLogin is exercised (restore path).
class TinyAuthNetwork : public IAuthNetwork
{
public:
    explicit TinyAuthNetwork(QObject *parent = nullptr)
        : IAuthNetwork(parent)
    {
    };
public:
    QList<UserResult> userQueue;
    QList<TokenResult> refreshQueue;
    QList<DeviceCodeResult> deviceQueue;
    QList<TokenResult> pollQueue;
    bool deferRefresh = false;
    QList<std::function<void(TokenResult)>> refreshPending;

    void requestDeviceCode(const QString &, std::function<void(DeviceCodeResult)> callback) override
    {
        if (!deviceQueue.isEmpty()) {
            callback(deviceQueue.takeFirst());
            return;
        }
        callback(DeviceCodeResult());
    }
    void pollAccessToken(const QString &, const QString &,
                         std::function<void(TokenResult)> callback) override
    {
        if (!pollQueue.isEmpty()) {
            callback(pollQueue.takeFirst());
            return;
        }
        TokenResult result;
        result.transportError = QStringLiteral("unused");
        callback(result);
    }
    void refreshAccessToken(const QString &, const QString &,
                            std::function<void(TokenResult)> callback) override
    {
        if (deferRefresh) {
            refreshPending.append(callback);
            return;
        }
        if (!refreshQueue.isEmpty()) {
            callback(refreshQueue.takeFirst());
            return;
        }
        TokenResult result;
        result.transportError = QStringLiteral("unused");
        callback(result);
    }
    void fireRefresh()
    {
        while (!refreshPending.isEmpty()) {
            auto callback = refreshPending.takeFirst();
            if (!refreshQueue.isEmpty()) {
                callback(refreshQueue.takeFirst());
            } else {
                TokenResult result;
                result.transportError = QStringLiteral("unused");
                callback(result);
            }
        }
    }
    void fetchUserLogin(const QString &, std::function<void(UserResult)> callback) override
    {
        if (!userQueue.isEmpty()) {
            callback(userQueue.takeFirst());
        } else {
            UserResult result;
            result.errorString = QStringLiteral("no queued user");
            callback(result);
        }
    }
};

class FakeIssueApi : public IIssueApi
{
public:
    QList<IssueHttpResult> queue;
    QList<QUrl> requestedUrls;
    bool defer = false;
    QList<std::function<void(IssueHttpResult)>> pending;
    QList<IssueUpdateRequest> updateRequests;
    QList<IssueHttpResult> updateQueue;
    bool deferUpdate = false;
    QList<std::function<void(IssueHttpResult)>> updatePending;

    void searchIssues(const QUrl &url, const QString &,
                      std::function<void(IssueHttpResult)> callback) override
    {
        requestedUrls.append(url);
        if (defer) {
            pending.append(callback);
            return;
        }
        IssueHttpResult result;
        if (!queue.isEmpty()) {
            result = queue.takeFirst();
        } else {
            result.transportError = QStringLiteral("no queued response");
        }
        callback(result);
    }

    void updateIssueState(const IssueUpdateRequest &request, const QString &,
                          std::function<void(IssueHttpResult)> callback) override
    {
        updateRequests.append(request);
        if (deferUpdate) {
            updatePending.append(callback);
            return;
        }
        IssueHttpResult result;
        if (!updateQueue.isEmpty()) {
            result = updateQueue.takeFirst();
        } else {
            result.transportError = QStringLiteral("no queued update response");
        }
        callback(result);
    }

    void fireAll()
    {
        while (!pending.isEmpty()) {
            auto callback = pending.takeFirst();
            IssueHttpResult result;
            if (!queue.isEmpty()) {
                result = queue.takeFirst();
            } else {
                result.transportError = QStringLiteral("no queued response");
            }
            callback(result);
        }
    }

    void fireUpdates()
    {
        while (!updatePending.isEmpty()) {
            auto callback = updatePending.takeFirst();
            IssueHttpResult result;
            if (!updateQueue.isEmpty()) {
                result = updateQueue.takeFirst();
            } else {
                result.transportError = QStringLiteral("no queued update response");
            }
            callback(result);
        }
    }

    void fireOneUpdate()
    {
        // Complete only the oldest deferred PATCH: lets a test interleave a
        // stale (fenced) callback with a newer account's live mutation.
        if (updatePending.isEmpty()) {
            return;
        }
        auto callback = updatePending.takeFirst();
        IssueHttpResult result;
        if (!updateQueue.isEmpty()) {
            result = updateQueue.takeFirst();
        } else {
            result.transportError = QStringLiteral("no queued update response");
        }
        callback(result);
    }

    void fireUpdateAt(int index)
    {
        // Complete one deferred PATCH out of order: models network
        // responses arriving in a different order than requests were sent.
        // Queues stay index-aligned (responses are pre-queued in dispatch
        // order), so the indexed request meets its own response.
        if (index < 0 || index >= updatePending.size() || index >= updateQueue.size()) {
            return;
        }
        auto callback = updatePending.takeAt(index);
        const IssueHttpResult result = updateQueue.takeAt(index);
        callback(result);
    }
};

namespace
{
QByteArray itemJson(qlonglong id, int number, const char *title, const char *repo,
                    const char *updated = "2026-10-01T10:00:00Z")
{
    return QByteArray("{\"id\":") + QByteArray::number(id) + ",\"number\":" + QByteArray::number(number)
        + ",\"title\":\"" + title + "\",\"state\":\"open\",\"html_url\":\"https://github.com/" + repo
        + "/issues/" + QByteArray::number(number) + "\",\"updated_at\":\"" + updated + "\","
        + "\"repository_url\":\"https://api.github.com/repos/" + repo + "\",\"labels\":[]}";
}

QByteArray pageJson(std::initializer_list<QByteArray> items, bool incomplete = false)
{
    QByteArray json = incomplete ? "{\"incomplete_results\":true,\"items\":["
                                 : "{\"incomplete_results\":false,\"items\":[";
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

IssueHttpResult httpOk(const QByteArray &body)
{
    IssueHttpResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

IssueHttpResult httpRateLimited(int retryAfterSecs)
{
    IssueHttpResult result;
    result.httpStatus = 429;
    result.headers.insert(QStringLiteral("retry-after"), QString::number(retryAfterSecs));
    result.body = QByteArray("{\"message\":\"API rate limit exceeded\"}");
    return result;
}

IssueHttpResult httpUpdate(const char *state)
{
    IssueHttpResult result;
    result.httpStatus = 200;
    result.body = QByteArray("{\"state\":\"") + state + "\"}";
    return result;
}

QByteArray closedItemJson(qlonglong id, int number, const char *title, const char *repo)
{
    return QByteArray("{\"id\":") + QByteArray::number(id) + ",\"number\":" + QByteArray::number(number)
        + ",\"title\":\"" + title + "\",\"state\":\"closed\",\"html_url\":\"https://github.com/" + repo
        + "/issues/" + QByteArray::number(number) + "\",\"updated_at\":\"2026-10-01T10:00:00Z\","
        + "\"repository_url\":\"https://api.github.com/repos/" + repo + "\",\"labels\":[]}";
}

// Row id as passed to closeIssue()/reopenIssue() (QString form).
QString rowId(IssueController &controller, int row)
{
    return controller.model()
        ->data(controller.model()->index(row), IssueListModel::IssueIdRole)
        .toString();
}

// Builds an Authenticated session via restore(): preloaded store +
// queued /user response. Never touches the network.
AuthSession *authenticatedSession(const QString &login, QObject *parent)
{
    auto *network = new TinyAuthNetwork(parent);
    auto *store = new InMemorySecureStore(parent);
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("tok");
    tokens.login = login;
    store->save(tokens);
    IAuthNetwork::UserResult user;
    user.ok = true;
    user.login = login;
    network->userQueue.append(user);
    auto *session = new AuthSession(network, store, parent);
    session->setClientId(QStringLiteral("cid"));
    session->restore();
    return session;
}
} // namespace

class IssueControllerTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void reloadLoadsAndPopulates();
    void viewChangeReloadsSingleQuery();
    void stateChangeReloads();
    void repoFilterIsClientSide();
    void unknownRepoShowsEmptyButKeepsLoaded();
    void errorPropagates();
    void unauthorizedMessage();
    void rateLimitPropagatesWithRetryAfter();
    void partialDataWarns();
    void incompleteWarns();
    void concurrentReloadGuarded();
    void viewChangeMidLoadRefetches();
    void logoutMidLoadDiscardsResult();
    void logoutClearsList();
    void logoutClearsRepoFilter();
    void refreshPreservesIssues();
    void reloginAsDifferentUserClearsAndReloads();
    void signedOutReloadErrors();
    void closeIssueRemovesRowAndEnablesUndo();
    void closeIssueDoubleSendGuarded();
    void closeIssueFailureWarnsAndClearsPending();
    void mutationRateLimitFailureThenSuccessClearsCountdown();
    void mutationRateLimitSurvivesFailedFetch();
    void partialLoadRateLimitSurvivesMutationSuccess();
    void overlappingMutationKeepsOtherRateOutcome();
    void mutationNonRateFailurePreservesOtherRateOutcome();
    void nonRateFailureThenSameMutationSuccessClearsOwnWarning();
    void siblingWarningSurvivesUnrelatedMutationSuccess();
    void mutationInFlightThenFailedReloadKeepsGuard();
    void undoInFlightThenSuccessfulReloadHasNoDuplicate();
    void secondMutationBlockedAcrossReload();
    void closeSuccessAfterViewSwitchUpsertsIntoClosedView();
    void reopenSuccessAfterViewSwitchUpsertsIntoOpenView();
    void undoSuccessInClosedViewDoesNotAddOpenRow();
    void lateUndoSuccessKeepsNewerUndo();
    void staleCallbackDoesNotReleaseNewAccountGuard();
    void overlappingUndoRejectedThenFailureKeepsUndoUsable();
    void closeSuccessAfterScopeSwitchOmitsOutOfScopeIssue();
    void closeSuccessAfterScopeSwitchUpsertsInScopeIssue();
    void undoAfterScopeSwitchRestoresAssignedIssue();
    void undoAfterScopeSwitchRestoresCreatedIssue();
    void staleUndoCallbackDoesNotClearNewAccountUndo();
    void reopenIssueRemovesRow();
    void undoLastCloseReopensAndRestores();
    void logoutDiscardsInFlightMutation();
    void logoutClearsPendingAndUndo();
    void reloginDiscardsInFlightMutation();
};

void IssueControllerTest::reloadLoadsAndPopulates()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/other")})));
    api.queue.append(httpOk(pageJson({itemJson(2, 8, "B", "o/other")})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    QVERIFY(auth->isAuthenticated());
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(!controller.loading());
    QCOMPARE(controller.loadedCount(), 2);
    QCOMPARE(controller.visibleCount(), 2);
    QCOMPARE(controller.model()->rowCount(), 2);
    QCOMPARE(controller.availableRepos(), QStringList({QStringLiteral("o/other"), QStringLiteral("o/r")}));
    QVERIFY(controller.errorMessage().isEmpty());
    QVERIFY(!controller.hasWarning());
    QVERIFY(controller.lastRefresh().isValid());
    // Newest-first is by date here (equal dates keep merge order; id 1 first).
    QCOMPARE(controller.model()->data(controller.model()->index(0), IssueListModel::TitleRole).toString(),
             QStringLiteral("A"));
}

void IssueControllerTest::viewChangeReloadsSingleQuery()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({itemJson(2, 2, "B", "o/r")})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(api.requestedUrls.size(), 2); // All: author + assignee
    controller.setView(IssueController::ViewAssigned);
    QCOMPARE(api.requestedUrls.size(), 3); // Assigned: assignee only
    QVERIFY(api.requestedUrls.at(2).query().contains(QStringLiteral("assignee")));
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.model()
                ->data(controller.model()->index(0), IssueListModel::AssignedToMeRole)
                .toBool());
}

void IssueControllerTest::stateChangeReloads()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.queue.append(httpOk(pageJson({itemJson(9, 3, "Old", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    controller.setIssueState(IssueController::StateClosed);
    QCOMPARE(api.requestedUrls.size(), 4);
    QVERIFY(api.requestedUrls.at(2).query().contains(QStringLiteral("is%3Aclosed"))
        || api.requestedUrls.at(2).query().contains(QStringLiteral("is:closed")));
    QCOMPARE(controller.model()
                 ->data(controller.model()->index(0), IssueListModel::TitleRole)
                 .toString(),
             QStringLiteral("Old"));
}

void IssueControllerTest::repoFilterIsClientSide()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r"), itemJson(2, 2, "B", "o/other")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.visibleCount(), 2);
    controller.setRepoFilter(QStringLiteral("o/r"));
    QCOMPARE(controller.visibleCount(), 1);
    QCOMPARE(controller.loadedCount(), 2); // loaded set untouched
    QCOMPARE(api.requestedUrls.size(), 2); // no refetch
    controller.setRepoFilter(QString());
    QCOMPARE(controller.visibleCount(), 2);
}

void IssueControllerTest::unknownRepoShowsEmptyButKeepsLoaded()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    // A filter matching nothing is an explicit empty choice, not a claim
    // that the account has no tasks: loadedCount stays visible to the UI.
    controller.setRepoFilter(QStringLiteral("o/unknown"));
    QCOMPARE(controller.visibleCount(), 0);
    QCOMPARE(controller.loadedCount(), 1);
}

void IssueControllerTest::errorPropagates()
{
    FakeIssueApi api;
    IssueHttpResult failed;
    failed.transportError = QStringLiteral("offline");
    api.queue.append(failed);
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(!controller.errorMessage().isEmpty());
    QVERIFY(!controller.hasWarning());
    QVERIFY(!controller.loading());
}

void IssueControllerTest::unauthorizedMessage()
{
    FakeIssueApi api;
    IssueHttpResult denied;
    denied.httpStatus = 401;
    denied.body = QByteArray("{\"message\":\"Bad credentials\"}");
    api.queue.append(denied);
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(controller.errorMessage().contains(QStringLiteral("sign in"), Qt::CaseInsensitive));
}

void IssueControllerTest::rateLimitPropagatesWithRetryAfter()
{
    // Both All-view queries hit 429 with Retry-After: the controller must
    // surface the blocking error plus the machine-readable countdown the
    // QML banner renders, and keep no stale issues.
    FakeIssueApi api;
    api.queue.append(httpRateLimited(45));
    api.queue.append(httpRateLimited(45));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(!controller.loading());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
    QVERIFY(!controller.errorMessage().isEmpty());
    QCOMPARE(controller.loadedCount(), 0);
    QCOMPARE(controller.visibleCount(), 0);
}

void IssueControllerTest::mutationRateLimitFailureThenSuccessClearsCountdown()
{
    // A rate-limited mutation arms the retry countdown; a later success
    // proves the server condition over and must retire it, keeping the
    // undo offer the success created.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpRateLimited(45));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
    QCOMPARE(controller.loadedCount(), 1); // row stays on failure
    controller.closeIssue(id); // retry allowed: failure released the guard
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(!controller.hasWarning());
    QVERIFY(!controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 0);
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::mutationRateLimitSurvivesFailedFetch()
{
    // A mutation-owned rate-limit outcome must survive an unrelated
    // non-rate fetch failure: the fetch says nothing about the
    // mutation's server condition, so flags and owner are preserved.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpRateLimited(45));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
    // Unrelated fetch failure (offline, not a rate limit): the error
    // surfaces but the mutation's countdown and owner survive.
    IssueHttpResult failed;
    failed.transportError = QStringLiteral("offline");
    api.queue.append(failed);
    api.queue.append(failed);
    controller.reload();
    QVERIFY(!controller.errorMessage().isEmpty());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
    QCOMPARE(controller.loadedCount(), 1); // failed load keeps the old list
    // The preserved owner still retires on the retry success.
    controller.closeIssue(id);
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(!controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 0);
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::partialLoadRateLimitSurvivesMutationSuccess()
{
    // Partial load: author query parsed, assignee query rate-limited. The
    // list shows one issue with a rate-limit warning. A later successful
    // close must not retire the load-owned outcome: the list is still
    // incomplete, so the countdown stays next to the fresh undo offer.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpRateLimited(45));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.loadedCount(), 1);
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(controller.canUndoClose());
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
}

void IssueControllerTest::overlappingMutationKeepsOtherRateOutcome()
{
    // Mutation A hits the rate limit (outcome owned by A); mutation B then
    // succeeds. B's success must not retire A's still-relevant warning.
    // A's own retry success retires it and moves the undo offer to A.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpRateLimited(45));
    api.updateQueue.append(httpUpdate("closed"));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString idA = rowId(controller, 0);
    const QString idB = rowId(controller, 1);
    controller.closeIssue(idA);
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    controller.closeIssue(idB);
    QCOMPARE(controller.loadedCount(), 1); // B converged, A kept
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
    controller.closeIssue(idA); // A's own retry
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(!controller.hasWarning());
    QVERIFY(!controller.rateLimited());
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::siblingWarningSurvivesUnrelatedMutationSuccess()
{
    // A rate-limited mutation arms the countdown with its own warning; a
    // sibling mutation's unrelated failure replaces the message but keeps
    // the flags; the first mutation's own retry success retires the
    // countdown yet preserves the sibling's still-unresolved warning.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpRateLimited(45));
    IssueHttpResult denied;
    denied.httpStatus = 403;
    denied.body = QByteArray("{\"message\":\"Must have push access\"}");
    api.updateQueue.append(denied);
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString idA = rowId(controller, 0);
    const QString idB = rowId(controller, 1);
    controller.closeIssue(idA); // 429: warning + countdown owned by A
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    controller.closeIssue(idB); // 403: own message, A's countdown preserved
    QVERIFY(controller.warningMessage().contains(QStringLiteral("installation"),
                                                  Qt::CaseInsensitive));
    QVERIFY(controller.rateLimited());
    controller.closeIssue(idA); // A's retry succeeds
    QCOMPARE(controller.loadedCount(), 1); // A converged, B kept
    QVERIFY(!controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 0);
    QVERIFY(controller.hasWarning()); // B's 403 message survives
    QVERIFY(controller.warningMessage().contains(QStringLiteral("installation"),
                                                  Qt::CaseInsensitive));
    QVERIFY(controller.canUndoClose()); // A's snapshot moved to A
}

void IssueControllerTest::mutationNonRateFailurePreservesOtherRateOutcome()
{
    // Mutation A hits the rate limit (outcome owned by A); an unrelated
    // mutation B then fails without a rate limit. B's failure updates the
    // warning to its own message but must preserve A's countdown and
    // ownership; A's own retry success retires the rate outcome while
    // keeping B's still-unresolved warning (ownership applies to the
    // warning too: one mutation's success must not retire another's).
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpRateLimited(45));
    IssueHttpResult denied;
    denied.httpStatus = 403;
    denied.body = QByteArray("{\"message\":\"Must have push access\"}");
    api.updateQueue.append(denied);
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString idA = rowId(controller, 0);
    const QString idB = rowId(controller, 1);
    controller.closeIssue(idA);
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 45);
    controller.closeIssue(idB);
    QCOMPARE(controller.loadedCount(), 2); // both rows stay
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.warningMessage().contains(QStringLiteral("installation"),
                                                  Qt::CaseInsensitive));
    QVERIFY(controller.rateLimited()); // A's outcome preserved
    QCOMPARE(controller.retryAfterSecs(), 45);
    controller.closeIssue(idA); // A's own retry
    QCOMPARE(controller.loadedCount(), 1); // A converged, B kept
    // A's rate outcome retired, but B's still-unresolved warning survives
    // A's success: resolving one mutation must not retire another's.
    QVERIFY(!controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 0);
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.warningMessage().contains(QStringLiteral("installation"),
                                                  Qt::CaseInsensitive));
    QVERIFY(controller.canUndoClose()); // undo moved to A
}

void IssueControllerTest::nonRateFailureThenSameMutationSuccessClearsOwnWarning()
{
    // A non-rate failure (403) arms no countdown but owns the warning; the
    // same mutation's retry success must retire its own message even though
    // the rate gate is uninvolved (the warning retire is independent of it).
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    IssueHttpResult denied;
    denied.httpStatus = 403;
    denied.body = QByteArray("{\"message\":\"Must have push access\"}");
    api.updateQueue.append(denied);
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QVERIFY(controller.hasWarning());
    QVERIFY(!controller.rateLimited());
    QCOMPARE(controller.loadedCount(), 1); // row stays on failure
    controller.closeIssue(id); // own retry; failure released the guard
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(!controller.hasWarning());
    QVERIFY(!controller.rateLimited());
    QCOMPARE(controller.retryAfterSecs(), 0);
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::partialDataWarns()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    IssueHttpResult failed;
    failed.transportError = QStringLiteral("cut");
    api.queue.append(failed);
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.errorMessage().isEmpty()); // warning, not error
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.truncatedResults());
}

void IssueControllerTest::incompleteWarns()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")}, true)));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.incompleteResults());
}

void IssueControllerTest::concurrentReloadGuarded()
{
    FakeIssueApi api;
    api.defer = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(controller.loading());
    const int requests = api.requestedUrls.size();
    QCOMPARE(requests, 1);
    controller.reload(); // ignored while loading
    QCOMPARE(api.requestedUrls.size(), 1);
    api.fireAll();
    QVERIFY(!controller.loading());
    QCOMPARE(controller.loadedCount(), 1);
}

void IssueControllerTest::viewChangeMidLoadRefetches()
{
    // Switching view while a fetch is in flight must not leave data from
    // the old selection on screen: the stale result is discarded and the
    // current selection is fetched once it completes.
    FakeIssueApi api;
    api.defer = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "Old", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.queue.append(httpOk(pageJson({itemJson(2, 2, "New", "o/r")})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload(); // All: author query in flight
    QVERIFY(controller.loading());
    controller.setView(IssueController::ViewAssigned); // dropped: loading
    api.fireAll();
    QVERIFY(!controller.loading());
    QCOMPARE(api.requestedUrls.size(), 3); // author + assignee + assigned
    QVERIFY(api.requestedUrls.at(2).query().contains(QStringLiteral("assignee")));
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.model()
                 ->data(controller.model()->index(0), IssueListModel::TitleRole)
                 .toString(),
             QStringLiteral("New"));
}

void IssueControllerTest::logoutMidLoadDiscardsResult()
{
    // Logging out while a fetch is in flight must invalidate it: the stale
    // callback (authorized with the pre-logout token) must not repopulate
    // the list after logout, even when the list was still empty.
    FakeIssueApi api;
    api.defer = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(controller.loading());
    auth->logout();
    QVERIFY(!controller.loading());
    api.fireAll(); // stale callbacks arrive here
    QCOMPARE(controller.loadedCount(), 0);
    QCOMPARE(controller.visibleCount(), 0);
    QVERIFY(controller.errorMessage().isEmpty());
    QVERIFY(!controller.loading());
    // The loading guard was released: a later reload runs the normal
    // signed-out path instead of wedging.
    controller.reload();
    QVERIFY(!controller.errorMessage().isEmpty());
    QCOMPARE(controller.loadedCount(), 0);
}

void IssueControllerTest::logoutClearsList()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    auth->logout();
    QCOMPARE(controller.loadedCount(), 0);
    QCOMPARE(controller.visibleCount(), 0);
    QVERIFY(controller.errorMessage().isEmpty());
}

void IssueControllerTest::logoutClearsRepoFilter()
{
    // A repository filter left over from a previous account would silently
    // hide the next account's issues after re-login, so logout resets it.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r1"), itemJson(2, 2, "B", "o/r2")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 2);
    controller.setRepoFilter(QStringLiteral("o/r1"));
    QCOMPARE(controller.visibleCount(), 1);
    auth->logout();
    QVERIFY(controller.repoFilter().isEmpty());
    QCOMPARE(controller.loadedCount(), 0);
    QCOMPARE(controller.visibleCount(), 0);
}

void IssueControllerTest::refreshPreservesIssues()
{
    // A token refresh is transient: neither the Refreshing state nor the
    // return to Authenticated may discard valid issue state, and success
    // must not trigger a redundant reload.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 1, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    auto *network = new TinyAuthNetwork(this);
    auto *store = new InMemorySecureStore(this);
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("tok");
    tokens.refreshToken = QStringLiteral("ref");
    tokens.login = QStringLiteral("octocat");
    store->save(tokens);
    IAuthNetwork::UserResult user;
    user.ok = true;
    user.login = QStringLiteral("octocat");
    network->userQueue.append(user);
    network->userQueue.append(user);
    IAuthNetwork::TokenResult refreshed;
    refreshed.ok = true;
    refreshed.data.hasToken = true;
    refreshed.data.accessToken = QStringLiteral("tok2");
    refreshed.data.refreshToken = QStringLiteral("ref2");
    refreshed.data.expiresInSeconds = 3600;
    network->refreshQueue.append(refreshed);
    network->deferRefresh = true;
    auto *auth = new AuthSession(network, store, this);
    auth->setClientId(QStringLiteral("cid"));
    auth->restore();
    QVERIFY(auth->isAuthenticated());
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    const int requestsAfterLoad = api.requestedUrls.size();
    auth->refreshSession();
    QCOMPARE(auth->status(), AuthSession::Refreshing);
    // Transient renewal: the loaded list survives untouched.
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.errorMessage().isEmpty());
    network->fireRefresh();
    QVERIFY(auth->isAuthenticated());
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.errorMessage().isEmpty());
    QCOMPARE(api.requestedUrls.size(), requestsAfterLoad);
}

void IssueControllerTest::reloginAsDifferentUserClearsAndReloads()
{
    // Refresh failure keeps the session (and the list) alive; a fresh
    // device flow that authenticates a DIFFERENT login without an
    // intermediate sign-out must drop the prior account's issues and load
    // the new account's — never show alice's tasks to bob.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "AliceIssue", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    auto *network = new TinyAuthNetwork(this);
    auto *store = new InMemorySecureStore(this);
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("tok-alice");
    tokens.refreshToken = QStringLiteral("ref-alice");
    tokens.login = QStringLiteral("alice");
    store->save(tokens);
    IAuthNetwork::UserResult aliceUser;
    aliceUser.ok = true;
    aliceUser.login = QStringLiteral("alice");
    network->userQueue.append(aliceUser);
    auto *auth = new AuthSession(network, store, this);
    auth->setClientId(QStringLiteral("cid"));
    auth->restore();
    QVERIFY(auth->isAuthenticated());
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    // Refresh fails but the session (and the list) survives it.
    IAuthNetwork::TokenResult refreshFail;
    refreshFail.ok = false;
    refreshFail.transportError = QStringLiteral("timeout");
    network->refreshQueue.append(refreshFail);
    auth->refreshSession();
    QCOMPARE(auth->status(), AuthSession::Failed);
    QCOMPARE(controller.loadedCount(), 1);
    // Fresh device flow authenticating bob; bob's issues are queued behind
    // alice's (ViewAll runs the Authored then Assigned query).
    IAuthNetwork::DeviceCodeResult device;
    device.ok = true;
    device.data.deviceCode = QStringLiteral("dev-code-2");
    device.data.userCode = QStringLiteral("AAAA-BBBB");
    device.data.verificationUri = QStringLiteral("https://github.com/login/device");
    device.data.expiresInSeconds = 900;
    device.data.intervalSeconds = 3600;
    network->deviceQueue.append(device);
    IAuthNetwork::TokenResult bobToken;
    bobToken.ok = true;
    bobToken.data.hasToken = true;
    bobToken.data.accessToken = QStringLiteral("tok-bob");
    bobToken.data.refreshToken = QStringLiteral("ref-bob");
    bobToken.data.expiresInSeconds = 3600;
    network->pollQueue.append(bobToken);
    IAuthNetwork::UserResult bobUser;
    bobUser.ok = true;
    bobUser.login = QStringLiteral("bob");
    network->userQueue.append(bobUser);
    api.queue.append(httpOk(pageJson({itemJson(2, 3, "BobIssue", "o/b")})));
    api.queue.append(httpOk(pageJson({})));
    auth->startLogin();
    QCOMPARE(auth->status(), AuthSession::AwaitingAuthorization);
    // Alice's issues stay visible while the new login is pending.
    QCOMPARE(controller.loadedCount(), 1);
    auth->pollOnce();
    QVERIFY(auth->isAuthenticated());
    QCOMPARE(auth->userLogin(), QStringLiteral("bob"));
    // The controller cleared alice's state and reloaded for bob.
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.model()
                 ->data(controller.model()->index(0), IssueListModel::TitleRole)
                 .toString(),
             QStringLiteral("BobIssue"));
    QVERIFY(controller.errorMessage().isEmpty());
}

void IssueControllerTest::signedOutReloadErrors()
{
    FakeIssueApi api;
    auto *network = new TinyAuthNetwork(this);
    auto *store = new InMemorySecureStore(this);
    auto *auth = new AuthSession(network, store, this);
    GitHubClient client(&api);
    IssueController controller(auth, &client);
    controller.reload();
    QVERIFY(!controller.errorMessage().isEmpty());
    QVERIFY(api.requestedUrls.isEmpty());
}

void IssueControllerTest::closeIssueRemovesRowAndEnablesUndo()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 2);
    const QString id = rowId(controller, 0);
    QVERIFY(!controller.canUndoClose());
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    QVERIFY(api.updateRequests.at(0).closed);
    QCOMPARE(api.updateRequests.at(0).number, 7);
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(controller.errorMessage().isEmpty());
    QVERIFY(!controller.hasWarning());
    QVERIFY(controller.canUndoClose());
    QVERIFY(controller.undoMessage().contains(QStringLiteral("o/r#7")));
}

void IssueControllerTest::closeIssueDoubleSendGuarded()
{
    // A second click while the PATCH is in flight sends nothing: the
    // per-issue pending flag guards the double dispatch.
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    QVERIFY(controller.model()->isIssuePending(1));
    controller.closeIssue(id); // duplicate while pending: no second request
    QCOMPARE(api.updateRequests.size(), 1);
    api.fireUpdates();
    QVERIFY(!controller.model()->isIssuePending(1));
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::mutationInFlightThenFailedReloadKeepsGuard()
{
    // A failed reload keeps the old list, and the outstanding mutation is
    // still live (plain reloads never fence it): the row must stay pending
    // and a second dispatch for the same issue must send nothing. When the
    // late result arrives it still converges (row dropped, undo offered).
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QVERIFY(controller.model()->isIssuePending(1));
    IssueHttpResult failed;
    failed.transportError = QStringLiteral("offline");
    api.queue.append(failed);
    controller.reload(); // fails; old list preserved, mutation still live
    QVERIFY(controller.model()->isIssuePending(1));
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(!controller.errorMessage().isEmpty());
    controller.closeIssue(id); // duplicate while outstanding: no second PATCH
    QCOMPARE(api.updateRequests.size(), 1);
    // The orphaned mutation result arrives late and must still converge:
    // the server confirmed the close, so the row drops and undo is offered.
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::secondMutationBlockedAcrossReload()
{
    // A successful reload rebuilds the model (losing visual pending flags)
    // while the PATCH is still outstanding: the controller-side guard must
    // still block a second dispatch, and the row is re-marked pending.
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    controller.reload(); // succeeds; model rebuilt, mutation still outstanding
    QVERIFY(controller.errorMessage().isEmpty());
    QVERIFY(controller.model()->isIssuePending(1));
    controller.closeIssue(rowId(controller, 0)); // still guarded: no second PATCH
    QCOMPARE(api.updateRequests.size(), 1);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(controller.canUndoClose());
}

void IssueControllerTest::closeSuccessAfterViewSwitchUpsertsIntoClosedView()
{
    // Close dispatched in the Open view; the user switches to Closed before
    // the PATCH lands. Success must converge into the CURRENT view: the
    // now-closed issue is upserted, never dropped, never wrong-state.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    api.deferUpdate = true;
    api.defer = true;
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    controller.setIssueState(IssueController::StateClosed);
    api.queue.append(httpOk(pageJson({}))); // closed list fetched pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(controller.canUndoClose());
    // The upserted row is interactive, not stuck pending: reopening it
    // dispatches a fresh PATCH.
    QVERIFY(!controller.model()->isIssuePending(1));
    controller.reopenIssue(rowId(controller, 0));
    QCOMPARE(api.updateRequests.size(), 2);
}

void IssueControllerTest::reopenSuccessAfterViewSwitchUpsertsIntoOpenView()
{
    // Mirror: reopen dispatched in the Closed view; the user switches to
    // Open before the PATCH lands. Success upserts the now-open issue.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({closedItemJson(1, 7, "A", "o/r")})));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.setIssueState(IssueController::StateClosed);
    QCOMPARE(controller.loadedCount(), 1);
    api.deferUpdate = true;
    api.defer = true;
    const QString id = rowId(controller, 0);
    controller.reopenIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    QVERIFY(!api.updateRequests.at(0).closed);
    controller.setIssueState(IssueController::StateOpen);
    api.queue.append(httpOk(pageJson({}))); // open list fetched pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    api.updateQueue.append(httpUpdate("open"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(!controller.canUndoClose()); // reopens offer no undo
    // The upserted row is interactive, not stuck pending: closing it again
    // dispatches a fresh PATCH.
    QVERIFY(!controller.model()->isIssuePending(1));
    controller.closeIssue(rowId(controller, 0));
    QCOMPARE(api.updateRequests.size(), 2);
}

void IssueControllerTest::undoSuccessInClosedViewDoesNotAddOpenRow()
{
    // Undo reopens the issue, but the user already switched to the Closed
    // view: the confirmed-open issue must NOT be appended to the closed
    // list. The undo is still spent.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    controller.closeIssue(rowId(controller, 0));
    QVERIFY(controller.canUndoClose());
    api.deferUpdate = true;
    api.defer = true;
    controller.setIssueState(IssueController::StateClosed);
    api.queue.append(httpOk(pageJson({})));
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    controller.undoLastClose();
    QCOMPARE(api.updateRequests.size(), 2);
    api.updateQueue.append(httpUpdate("open"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 0);
    QCOMPARE(controller.visibleCount(), 0);
    QVERIFY(!controller.canUndoClose());
}

void IssueControllerTest::lateUndoSuccessKeepsNewerUndo()
{
    // Undo for A is in flight when B is closed successfully, replacing the
    // snapshot. A's late undo success must restore A but keep B's offer —
    // and B's offer must still work afterwards.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed")); // A's close
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 2);
    controller.closeIssue(rowId(controller, 0)); // closes A (id 1)
    QVERIFY(controller.canUndoClose());
    QVERIFY(controller.undoMessage().contains(QStringLiteral("o/r#7")));
    api.deferUpdate = true;
    controller.undoLastClose(); // undo A held in flight
    QCOMPARE(api.updateRequests.size(), 2);
    api.updateQueue.append(httpUpdate("closed")); // B's close
    api.deferUpdate = false;
    controller.closeIssue(rowId(controller, 0)); // closes B (id 2)
    QCOMPARE(api.updateRequests.size(), 3);
    QVERIFY(controller.undoMessage().contains(QStringLiteral("o/r#8")));
    // A's late undo success: A returns, B's offer survives.
    api.updateQueue.append(httpUpdate("open")); // A's undo
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(controller.canUndoClose());
    QVERIFY(controller.undoMessage().contains(QStringLiteral("o/r#8")));
    // B's offer still works: undo reopens B and spends the snapshot.
    api.updateQueue.append(httpUpdate("open")); // B's undo
    controller.undoLastClose();
    QCOMPARE(controller.loadedCount(), 2);
    QVERIFY(!controller.canUndoClose());
}

void IssueControllerTest::staleCallbackDoesNotReleaseNewAccountGuard()
{
    // Alice's close is outstanding when she signs out and bob signs in on
    // the same controller and closes the SAME issue id. Alice's late
    // (fenced) callback must not release bob's guard: a duplicate close for
    // bob dispatches nothing, and bob's own callback still converges.
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    auto *network = new TinyAuthNetwork(this);
    auto *store = new InMemorySecureStore(this);
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("tok-alice");
    tokens.login = QStringLiteral("alice");
    store->save(tokens);
    IAuthNetwork::UserResult aliceUser;
    aliceUser.ok = true;
    aliceUser.login = QStringLiteral("alice");
    network->userQueue.append(aliceUser);
    auto *auth = new AuthSession(network, store, this);
    auth->setClientId(QStringLiteral("cid"));
    auth->restore();
    QVERIFY(auth->isAuthenticated());
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    controller.closeIssue(rowId(controller, 0)); // alice's PATCH outstanding
    QCOMPARE(api.updateRequests.size(), 1);
    auth->logout(); // wipes guard + list; alice's callback is now stale
    QCOMPARE(controller.loadedCount(), 0);
    // Bob signs in via device flow; his reload returns the same issue id.
    IAuthNetwork::DeviceCodeResult device;
    device.ok = true;
    device.data.deviceCode = QStringLiteral("dev-code-2");
    device.data.userCode = QStringLiteral("AAAA-BBBB");
    device.data.verificationUri = QStringLiteral("https://github.com/login/device");
    device.data.expiresInSeconds = 900;
    device.data.intervalSeconds = 3600;
    network->deviceQueue.append(device);
    IAuthNetwork::TokenResult bobToken;
    bobToken.ok = true;
    bobToken.data.hasToken = true;
    bobToken.data.accessToken = QStringLiteral("tok-bob");
    bobToken.data.refreshToken = QStringLiteral("ref-bob");
    bobToken.data.expiresInSeconds = 3600;
    network->pollQueue.append(bobToken);
    IAuthNetwork::UserResult bobUser;
    bobUser.ok = true;
    bobUser.login = QStringLiteral("bob");
    network->userQueue.append(bobUser);
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    auth->startLogin();
    QCOMPARE(auth->status(), AuthSession::AwaitingAuthorization);
    auth->pollOnce();
    QVERIFY(auth->isAuthenticated());
    QCOMPARE(auth->userLogin(), QStringLiteral("bob"));
    QCOMPARE(controller.loadedCount(), 1);
    controller.closeIssue(rowId(controller, 0)); // bob's PATCH outstanding
    QCOMPARE(api.updateRequests.size(), 2);
    // Alice's late success is fenced: the guard stays, so a duplicate close
    // for bob dispatches nothing.
    api.updateQueue.append(httpUpdate("closed")); // alice's late result
    api.fireOneUpdate();
    QCOMPARE(api.updateRequests.size(), 2);
    controller.closeIssue(rowId(controller, 0)); // still guarded: nothing
    QCOMPARE(api.updateRequests.size(), 2);
    // Bob's own callback converges: closed leaves the Open view, undo offered.
    api.updateQueue.append(httpUpdate("closed")); // bob's result
    api.fireOneUpdate();
    QCOMPARE(api.updateRequests.size(), 2);
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(controller.canUndoClose());
    QVERIFY(controller.undoMessage().contains(QStringLiteral("o/r#7")));
}

void IssueControllerTest::overlappingUndoRejectedThenFailureKeepsUndoUsable()
{
    // Close A (undo offered). A regular reopen is dispatched with its PATCH
    // deferred; clicking undo while it is outstanding is rejected WITHOUT
    // arming the undo-in-flight flag. When the reopen FAILS, the undo offer
    // is still usable and dispatches a fresh PATCH.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed")); // A's close
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    controller.closeIssue(rowId(controller, 0));
    QVERIFY(controller.canUndoClose());
    QCOMPARE(controller.loadedCount(), 0);
    // The reopen runs from the Closed view with a deferred PATCH.
    api.queue.append(httpOk(pageJson({closedItemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    controller.setIssueState(IssueController::StateClosed);
    QCOMPARE(controller.loadedCount(), 1);
    api.deferUpdate = true;
    controller.reopenIssue(rowId(controller, 0)); // regular reopen in flight
    QCOMPARE(api.updateRequests.size(), 2);
    controller.undoLastClose(); // rejected: guard held, flag untouched
    QCOMPARE(api.updateRequests.size(), 2);
    // The reopen fails: the undo offer survives AND still dispatches.
    IssueHttpResult fail;
    fail.transportError = QStringLiteral("timeout");
    api.updateQueue.append(fail);
    api.fireUpdates();
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose();
    QCOMPARE(api.updateRequests.size(), 3);
    api.updateQueue.append(httpUpdate("open")); // undo succeeds
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 0); // Closed view: open row absent
    QVERIFY(!controller.canUndoClose());
}

void IssueControllerTest::undoInFlightThenSuccessfulReloadHasNoDuplicate()
{
    // Undo re-adds the reopened row on success, but a racing reload may
    // already have fetched it (post-commit read): the late undo must patch
    // the existing row, not append a duplicate.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.deferUpdate = true;
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates(); // close confirmed: row dropped, undo offered
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose(); // reopen in flight
    // Racing reload fetches the already-reopened issue from the server.
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    api.updateQueue.append(httpUpdate("open"));
    api.fireUpdates(); // undo confirmed: still exactly one row, undo spent
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(!controller.canUndoClose());
}

void IssueControllerTest::closeIssueFailureWarnsAndClearsPending()
{
    // Mutation failures are warnings, not errors: the list stays visible
    // and the row returns to its interactive state.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    IssueHttpResult denied;
    denied.httpStatus = 403;
    denied.body = QByteArray("{\"message\":\"Must have push access\"}");
    api.updateQueue.append(denied);
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(controller.loadedCount(), 1); // row stays
    QVERIFY(!controller.model()->isIssuePending(1));
    QVERIFY(controller.errorMessage().isEmpty());
    QVERIFY(controller.hasWarning());
    QVERIFY(controller.warningMessage().contains(QStringLiteral("installation"),
                                                  Qt::CaseInsensitive));
    QVERIFY(!controller.canUndoClose());
}

void IssueControllerTest::reopenIssueRemovesRow()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({closedItemJson(3, 9, "Old", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("open"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.setIssueState(IssueController::StateClosed);
    QCOMPARE(controller.loadedCount(), 1);
    const QString id = rowId(controller, 0);
    controller.reopenIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    QVERIFY(!api.updateRequests.at(0).closed);
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(!controller.canUndoClose()); // undo is close-only
}

void IssueControllerTest::undoLastCloseReopensAndRestores()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r"), itemJson(2, 8, "B", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    api.updateQueue.append(httpUpdate("open"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    controller.closeIssue(rowId(controller, 0));
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose();
    QCOMPARE(api.updateRequests.size(), 2);
    QVERIFY(!api.updateRequests.at(1).closed);
    QCOMPARE(controller.loadedCount(), 2);
    QVERIFY(!controller.canUndoClose());
    QVERIFY(controller.undoMessage().isEmpty());
    QVERIFY(controller.errorMessage().isEmpty());
    controller.undoLastClose(); // no snapshot: no request
    QCOMPARE(api.updateRequests.size(), 2);
}

void IssueControllerTest::logoutDiscardsInFlightMutation()
{
    // A mutation completing after logout must change nothing: the stale
    // callback is fenced by the logout generation bump.
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    controller.closeIssue(rowId(controller, 0));
    QCOMPARE(api.updateRequests.size(), 1);
    auth->logout();
    api.fireUpdates(); // stale callbacks arrive here
    QCOMPARE(controller.loadedCount(), 0);
    QVERIFY(!controller.canUndoClose());
    QVERIFY(controller.errorMessage().isEmpty());
}

void IssueControllerTest::logoutClearsPendingAndUndo()
{
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    controller.closeIssue(rowId(controller, 0));
    QVERIFY(controller.canUndoClose());
    auth->logout();
    QVERIFY(!controller.canUndoClose());
    QVERIFY(controller.undoMessage().isEmpty());
    QVERIFY(!controller.hasWarning());
}

void IssueControllerTest::reloginDiscardsInFlightMutation()
{
    // Alice's close is in flight when bob authenticates: the stale success
    // must not drop bob's rows or arm an undo for the wrong account.
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "AliceIssue", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    api.updateQueue.append(httpUpdate("closed"));
    GitHubClient client(&api);
    auto *network = new TinyAuthNetwork(this);
    auto *store = new InMemorySecureStore(this);
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("tok-alice");
    tokens.refreshToken = QStringLiteral("ref-alice");
    tokens.login = QStringLiteral("alice");
    store->save(tokens);
    IAuthNetwork::UserResult aliceUser;
    aliceUser.ok = true;
    aliceUser.login = QStringLiteral("alice");
    network->userQueue.append(aliceUser);
    auto *auth = new AuthSession(network, store, this);
    auth->setClientId(QStringLiteral("cid"));
    auth->restore();
    QVERIFY(auth->isAuthenticated());
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    controller.closeIssue(rowId(controller, 0)); // in flight, alice's token
    QCOMPARE(api.updateRequests.size(), 1);
    // Alice's session degrades to Failed (keeps tokens and the list): the
    // only state from which a fresh device flow may start.
    IAuthNetwork::TokenResult refreshFail;
    refreshFail.ok = false;
    refreshFail.transportError = QStringLiteral("timeout");
    network->refreshQueue.append(refreshFail);
    auth->refreshSession();
    QCOMPARE(auth->status(), AuthSession::Failed);
    QCOMPARE(controller.loadedCount(), 1);
    // Bob's pages are queued before his login completes (controller
    // reloads for the new identity synchronously inside the handler).
    api.queue.append(httpOk(pageJson({itemJson(2, 3, "BobIssue", "o/b")})));
    api.queue.append(httpOk(pageJson({})));
    IAuthNetwork::DeviceCodeResult device;
    device.ok = true;
    device.data.deviceCode = QStringLiteral("dev-code-2");
    device.data.userCode = QStringLiteral("AAAA-BBBB");
    device.data.verificationUri = QStringLiteral("https://github.com/login/device");
    device.data.expiresInSeconds = 900;
    device.data.intervalSeconds = 3600;
    network->deviceQueue.append(device);
    IAuthNetwork::TokenResult bobToken;
    bobToken.ok = true;
    bobToken.data.hasToken = true;
    bobToken.data.accessToken = QStringLiteral("tok-bob");
    bobToken.data.refreshToken = QStringLiteral("ref-bob");
    bobToken.data.expiresInSeconds = 3600;
    network->pollQueue.append(bobToken);
    IAuthNetwork::UserResult bobUser;
    bobUser.ok = true;
    bobUser.login = QStringLiteral("bob");
    network->userQueue.append(bobUser);
    auth->startLogin();
    auth->pollOnce();
    QVERIFY(auth->isAuthenticated());
    QCOMPARE(auth->userLogin(), QStringLiteral("bob"));
    api.fireUpdates(); // alice's stale mutation success
    QCOMPARE(controller.loadedCount(), 1);
    QCOMPARE(controller.model()
                 ->data(controller.model()->index(0), IssueListModel::TitleRole)
                 .toString(),
             QStringLiteral("BobIssue"));
    QVERIFY(!controller.canUndoClose());
}

void IssueControllerTest::closeSuccessAfterScopeSwitchOmitsOutOfScopeIssue()
{
    // Close dispatched in All; the user switches scope to Assigned and
    // state to Closed before the PATCH lands. The issue is authored, not
    // assigned: success must NOT append it to the Assigned list, though
    // the close undo is still offered and functional.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")}))); // All/authored
    api.queue.append(httpOk(pageJson({}))); // All/assigned
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    // Pin the fixture premise for the scope gate below: this row arrived
    // via the All/authored query, so it must be authored-only. If this
    // ever flips, the out-of-scope expectation below is the fixture's
    // fault, not the production gate's.
    QVERIFY(!controller.model()
                 ->data(controller.model()->index(0), IssueListModel::AssignedToMeRole)
                 .toBool());
    QVERIFY(controller.model()
                ->data(controller.model()->index(0), IssueListModel::AuthoredByMeRole)
                .toBool());
    api.deferUpdate = true;
    api.defer = true;
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    controller.setView(IssueController::ViewAssigned);
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")}))); // assigned/open, pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 1);
    controller.setIssueState(IssueController::StateClosed);
    api.queue.append(httpOk(pageJson({}))); // assigned/closed, pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 0); // out of scope: not appended
    QCOMPARE(controller.visibleCount(), 0);
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose(); // the offer still works
    QCOMPARE(api.updateRequests.size(), 2);
}

void IssueControllerTest::closeSuccessAfterScopeSwitchUpsertsInScopeIssue()
{
    // Mirror: the issue IS assigned to the user, so the Closed/Assigned
    // list legitimately gains it when the close succeeds mid-switch.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({}))); // All/authored
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")}))); // All/assigned
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    api.deferUpdate = true;
    api.defer = true;
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    controller.setView(IssueController::ViewAssigned);
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")}))); // assigned/open, pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 1);
    controller.setIssueState(IssueController::StateClosed);
    api.queue.append(httpOk(pageJson({}))); // assigned/closed, pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 1); // in scope: upserted
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(controller.canUndoClose());
    QVERIFY(!controller.model()->isIssuePending(1));
}

void IssueControllerTest::undoAfterScopeSwitchRestoresAssignedIssue()
{
    // Close dispatched in All; the user switches scope to Assigned before
    // the undo lands. The issue IS assigned: the undo reopen must restore
    // it — the snapshot must carry scope provenance, not just coordinates.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({}))); // All/authored
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")}))); // All/assigned
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.model()
                ->data(controller.model()->index(0), IssueListModel::AssignedToMeRole)
                .toBool());
    api.deferUpdate = true;
    api.defer = true;
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    controller.setView(IssueController::ViewAssigned);
    api.queue.append(httpOk(pageJson({}))); // assigned/open, pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose();
    QCOMPARE(api.updateRequests.size(), 2);
    api.updateQueue.append(httpUpdate("open"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 1); // in scope: restored
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(!controller.canUndoClose()); // undo spent
    QVERIFY(!controller.model()->isIssuePending(1));
}

void IssueControllerTest::undoAfterScopeSwitchRestoresCreatedIssue()
{
    // Mirror for the Created view with an authored-only issue.
    FakeIssueApi api;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "A", "o/r")}))); // All/authored
    api.queue.append(httpOk(pageJson({}))); // All/assigned
    GitHubClient client(&api);
    AuthSession *auth = authenticatedSession(QStringLiteral("octocat"), this);
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(controller.model()
                ->data(controller.model()->index(0), IssueListModel::AuthoredByMeRole)
                .toBool());
    api.deferUpdate = true;
    api.defer = true;
    const QString id = rowId(controller, 0);
    controller.closeIssue(id);
    QCOMPARE(api.updateRequests.size(), 1);
    controller.setView(IssueController::ViewCreated);
    api.queue.append(httpOk(pageJson({}))); // created/open, pre-commit
    api.fireAll();
    QCOMPARE(controller.loadedCount(), 0);
    api.updateQueue.append(httpUpdate("closed"));
    api.fireUpdates();
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose();
    QCOMPARE(api.updateRequests.size(), 2);
    api.updateQueue.append(httpUpdate("open"));
    api.fireUpdates();
    QCOMPARE(controller.loadedCount(), 1); // in scope: restored
    QCOMPARE(controller.visibleCount(), 1);
    QVERIFY(!controller.canUndoClose());
    QVERIFY(!controller.model()->isIssuePending(1));
}

void IssueControllerTest::staleUndoCallbackDoesNotClearNewAccountUndo()
{
    // Alice's close succeeds, then her undo is in flight at logout; bob
    // signs in and starts his own undo for the same issue id. Alice's late
    // fenced callback must touch no current-account state — bob's in-flight
    // undo stays protected, so a further undo attempt is still rejected
    // until bob's lands.
    FakeIssueApi api;
    api.deferUpdate = true;
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "AliceIssue", "o/r")})));
    api.queue.append(httpOk(pageJson({})));
    GitHubClient client(&api);
    auto *network = new TinyAuthNetwork(this);
    auto *store = new InMemorySecureStore(this);
    StoredTokens tokens;
    tokens.accessToken = QStringLiteral("tok-alice");
    tokens.refreshToken = QStringLiteral("ref-alice");
    tokens.login = QStringLiteral("alice");
    store->save(tokens);
    IAuthNetwork::UserResult aliceUser;
    aliceUser.ok = true;
    aliceUser.login = QStringLiteral("alice");
    network->userQueue.append(aliceUser);
    auto *auth = new AuthSession(network, store, this);
    auth->setClientId(QStringLiteral("cid"));
    auth->restore();
    QVERIFY(auth->isAuthenticated());
    IssueController controller(auth, &client);
    controller.reload();
    QCOMPARE(controller.loadedCount(), 1);
    api.updateQueue.append(httpUpdate("closed")); // alice's close
    controller.closeIssue(rowId(controller, 0)); // req 1
    QCOMPARE(api.updateRequests.size(), 1);
    api.fireOneUpdate(); // alice's close succeeds: undo armed
    QVERIFY(controller.canUndoClose());
    controller.undoLastClose(); // req 2, deferred in flight
    QCOMPARE(api.updateRequests.size(), 2);
    auth->logout();
    QCOMPARE(controller.loadedCount(), 0);
    // Bob signs in on the same controller; his list carries the same issue
    // id, and his own close+undo go in flight. Responses are queued in
    // dispatch order; fireUpdateAt lands them out of order to model the
    // stale response arriving late.
    api.updateQueue.append(httpUpdate("closed")); // alice's undo (fenced)
    api.queue.append(httpOk(pageJson({itemJson(1, 7, "BobIssue", "o/b")})));
    api.queue.append(httpOk(pageJson({})));
    IAuthNetwork::UserResult bobUser;
    bobUser.ok = true;
    bobUser.login = QStringLiteral("bob");
    network->userQueue.append(bobUser);
    IAuthNetwork::DeviceCodeResult device;
    device.ok = true;
    device.data.deviceCode = QStringLiteral("dev-code-3");
    device.data.userCode = QStringLiteral("CCCC-DDDD");
    device.data.verificationUri = QStringLiteral("https://github.com/login/device");
    device.data.expiresInSeconds = 900;
    device.data.intervalSeconds = 3600;
    network->deviceQueue.append(device);
    IAuthNetwork::TokenResult bobToken;
    bobToken.ok = true;
    bobToken.data.hasToken = true;
    bobToken.data.accessToken = QStringLiteral("tok-bob");
    bobToken.data.refreshToken = QStringLiteral("ref-bob");
    bobToken.data.expiresInSeconds = 3600;
    network->pollQueue.append(bobToken);
    auth->startLogin();
    auth->pollOnce();
    QVERIFY(auth->isAuthenticated());
    QCOMPARE(auth->userLogin(), QStringLiteral("bob"));
    QCOMPARE(controller.loadedCount(), 1);
    api.updateQueue.append(httpUpdate("closed")); // bob's close
    controller.closeIssue(rowId(controller, 0)); // req 3
    QCOMPARE(api.updateRequests.size(), 3);
    // Bob's close response overtakes alice's stale one (network
    // reordering): fire it out of order so his undo offer exists before
    // the stale callback lands.
    api.fireUpdateAt(1); // bob's close succeeds: undo armed
    QVERIFY(controller.canUndoClose()); // bob's offer
    api.updateQueue.append(httpUpdate("open")); // bob's undo
    controller.undoLastClose(); // req 4, deferred in flight
    QCOMPARE(api.updateRequests.size(), 4);
    QVERIFY(controller.undoInFlightForTesting());
    // Alice's stale undo lands fenced: it must touch no current-account
    // state — bob's in-flight flag and offer stay intact.
    api.fireUpdateAt(0);
    QVERIFY(controller.undoInFlightForTesting());
    QVERIFY(controller.canUndoClose()); // bob's offer intact
    controller.undoLastClose(); // still guarded: no 5th PATCH
    QCOMPARE(api.updateRequests.size(), 4);
    api.fireUpdates(); // bob's undo succeeds
    QCOMPARE(controller.loadedCount(), 1);
    QVERIFY(!controller.canUndoClose()); // bob's undo spent
}

QTEST_MAIN(IssueControllerTest)
#include "tst_issuecontroller.moc"
