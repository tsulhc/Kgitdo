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

QTEST_MAIN(IssueControllerTest)
#include "tst_issuecontroller.moc"
