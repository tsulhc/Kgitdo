#include <QtTest/QtTest>

#include <QApplication>
#include <QFile>
#include "AppIcon.h"

class AppIconTest final : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void rendersBundledIcon()
    {
        QVERIFY(QFile::exists(QStringLiteral(":/kgitdo/icon.svg")));
        const QIcon icon = makeAppIcon();
        QVERIFY(!icon.isNull());
        for (const int size : {32, 64, 128}) {
            const QPixmap px = icon.pixmap(size, size);
            QVERIFY2(!px.isNull(), "Bundled application icon did not render");
            QVERIFY(px.toImage().pixelColor(size / 2, size / 2).alpha() > 0);
        }
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    AppIconTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_appicon.moc"
