#include <QApplication>
#include <QByteArray>
#include <QQmlApplicationEngine>
#include <QtQml>
#include <QQuickStyle>
#include <QUrl>

#include <KIconTheme>
#include <KLocalizedContext>
#include <KLocalizedString>

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

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextObject(new KLocalizedContext(&engine));
    engine.loadFromModule(QStringLiteral("org.kde.kgitdo"), QStringLiteral("Main"));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    return app.exec();
}
