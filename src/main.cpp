#include <QApplication>
#include <QByteArray>
#include <QIcon>

#include "AppIcon.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtQml>
#include <QQuickStyle>
#include <QUrl>

#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>

#include "AuthSession.h"
#include "GitHubAppConfig.h"
#include "GitHubClient.h"
#include "IssueController.h"
#include "AsyncKWalletStore.h"
#include "QNetworkAuthTransport.h"
#include "QNetworkIssueApi.h"

#include "BuildRevision.h"

int main(int argc, char *argv[])
{
    KIconTheme::initTheme();

    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain(QByteArrayLiteral("kgitdo"));

    QApplication::setOrganizationName(QStringLiteral("KDE"));
    QApplication::setOrganizationDomain(QStringLiteral("kde.org"));
    QApplication::setApplicationName(QStringLiteral("Kgitdo"));
    QApplication::setDesktopFileName(QStringLiteral("org.kde.kgitdo"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setStyle(QStringLiteral("breeze"));
    // AppImages do not register icon theme paths in the host desktop.
    // Render the bundled SVG into pixmaps so the window/taskbar icon works
    // without depending on a system icon theme or SVG imageformat plugin.
    QIcon icon = makeAppIcon();
    if (icon.isNull()) {
        icon = QIcon::fromTheme(QStringLiteral("org.kde.kgitdo"));
    }
    app.setWindowIcon(icon);

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    auto *network = new QNetworkAuthTransport(&app);
    auto *store = new AsyncKWalletStore(&app);
    auto *authSession = new AuthSession(network, store, &app);
    authSession->setClientId(GitHubAppConfig::effectiveClientId());

    // W2: issue reading. The controller owns no UI; W3 builds the
    // checklist view on top of its model and filter properties.
    auto *issueApi = new QNetworkIssueApi(&app);
    auto *gitHubClient = new GitHubClient(issueApi, &app);
    auto *issueController = new IssueController(authSession, gitHubClient, &app);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.rootContext()->setContextProperty(QStringLiteral("authSession"), authSession);
    engine.rootContext()->setContextProperty(QStringLiteral("buildRevision"), QString::fromLatin1(KGITDO_BUILD_SHA));
    engine.rootContext()->setContextProperty(QStringLiteral("issueController"), issueController);
    engine.rootContext()->setContextProperty(QStringLiteral("manageInstallationsUrl"),
                                             GitHubAppConfig::manageInstallationsUrl());
    engine.loadFromModule(QStringLiteral("org.kde.kgitdo"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    authSession->restore();

    return app.exec();
}
