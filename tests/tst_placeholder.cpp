#include <QtTest/QtTest>

class PlaceholderTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void smokePasses();
};

void PlaceholderTest::smokePasses()
{
    // W0: proves that the QtTest harness, CTest integration, and CI
    // run automated tests. Real parser/dedup/auth coverage arrives
    // with W1/W2/W5.
    QVERIFY(true);
    QCOMPARE(QStringLiteral("kgitdo"), QStringLiteral("kgitdo"));
}

QTEST_MAIN(PlaceholderTest)
#include "tst_placeholder.moc"
