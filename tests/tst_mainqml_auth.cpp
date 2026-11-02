#include <QtTest/QtTest>

#include <QApplication>
#include <QQuickItem>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include <KLocalizedContext>
#include <KLocalizedString>

#include "AuthSession.h"
#include "IssueController.h"

class FakeAuthNetwork final : public IAuthNetwork
{
public:
    using IAuthNetwork::IAuthNetwork;
    bool userResponseDeferred = false;
    std::function<void(UserResult)> userCallback;

    void requestDeviceCode(const QString &, std::function<void(DeviceCodeResult)> cb) override
    {
        DeviceCodeResult result;
        result.ok = true;
        result.data.deviceCode = QStringLiteral("fake-device");
        result.data.userCode = QStringLiteral("AAAA-BBBB");
        result.data.verificationUri = QStringLiteral("https://github.com/login/device");
        result.data.intervalSeconds = 30;
        result.data.expiresInSeconds = 900;
        cb(result);
    }
    void pollAccessToken(const QString &, const QString &, std::function<void(TokenResult)> cb) override
    {
        TokenResult result;
        result.ok = true;
        result.httpStatus = 200;
        result.data.hasToken = true;
        result.data.accessToken = QStringLiteral("synthetic-only");
        cb(result);
    }
    void refreshAccessToken(const QString &, const QString &, std::function<void(TokenResult)> cb) override
    {
        cb({});
    }
    void fetchUserLogin(const QString &, std::function<void(UserResult)> cb) override
    {
        userCallback = std::move(cb);
        if (!userResponseDeferred) {
            completeUser();
        }
    }
    void completeUser()
    {
        if (!userCallback) return;
        auto cb = std::move(userCallback);
        userCallback = {};
        UserResult result;
        result.ok = true;
        result.login = QStringLiteral("octocat");
        result.httpStatus = 200;
        cb(result);
    }
};

class MainQmlAuthTest final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void pageReflectsEveryRealAuthState();
};

void MainQmlAuthTest::pageReflectsEveryRealAuthState()
{
    FakeAuthNetwork network;
    InMemorySecureStore wallet;
    AuthSession session(&network, &wallet);
    session.setClientId(QStringLiteral("public-id"));
    session.setCopyHandler([](const QString &) { return true; });
    session.setOpenHandler([](const QUrl &) { return true; });
    IssueController issues(&session, nullptr);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("authSession"), &session);
    engine.rootContext()->setContextProperty(QStringLiteral("issueController"), &issues);
    engine.rootContext()->setContextProperty(QStringLiteral("buildRevision"), QStringLiteral("test"));
    engine.rootContext()->setContextProperty(QStringLiteral("manageInstallationsUrl"),
                                             QStringLiteral("https://github.com/settings/installations"));
    engine.load(QUrl::fromLocalFile(QStringLiteral(KGITDO_SOURCE_DIR "/src/Main.qml")));
    QCOMPARE(engine.rootObjects().size(), 1);
    QObject *window = engine.rootObjects().constFirst();

    auto *login = window->findChild<QQuickItem *>(QStringLiteral("signInContent"));
    auto *waiting = window->findChild<QQuickItem *>(QStringLiteral("authWaitSection"));
    auto *busy = window->findChild<QQuickItem *>(QStringLiteral("authBusySection"));
    auto *entry = window->findChild<QQuickItem *>(QStringLiteral("authEntrySection"));
    auto *checklist = window->findChild<QQuickItem *>(QStringLiteral("checklistContent"));
    QVERIFY(login && waiting && busy && entry && checklist);

    QTRY_VERIFY(login->isVisible());
    QVERIFY(entry->isVisible());
    QVERIFY(!waiting->isVisible());
    QVERIFY(!busy->isVisible());
    QVERIFY(!checklist->isVisible());

    network.userResponseDeferred = true;
    session.startLogin();
    QCOMPARE(session.status(), AuthSession::AwaitingAuthorization);
    QTRY_VERIFY(waiting->isVisible());
    QVERIFY(!entry->isVisible());
    QVERIFY(!busy->isVisible());

    session.pollOnce();
    QCOMPARE(session.lastPollResult(), QStringLiteral("authorized"));
    QCOMPARE(session.status(), AuthSession::Refreshing);
    QTRY_VERIFY(busy->isVisible());
    QVERIFY(!waiting->isVisible());
    QVERIFY(!entry->isVisible());
    QVERIFY(!checklist->isVisible());

    network.completeUser();
    QCOMPARE(session.status(), AuthSession::Authenticated);
    QTRY_VERIFY(checklist->isVisible());
    QVERIFY(!login->isVisible());

    session.logout();
    QCOMPARE(session.status(), AuthSession::SignedOut);
    QTRY_VERIFY(login->isVisible());
    QVERIFY(entry->isVisible());
    QVERIFY(!waiting->isVisible());
    QVERIFY(!busy->isVisible());
    QVERIFY(!checklist->isVisible());
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kgitdo");
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    MainQmlAuthTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_mainqml_auth.moc"
