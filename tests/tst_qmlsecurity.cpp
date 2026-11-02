#include <QtTest>
#include <QFile>
#include <QRegularExpression>

class QmlSecurityTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void issueTitlesHaveSingleRenderingPath()
    {
        QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/ChecklistView.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(file.readAll());
        // Never reintroduce a button with both a native text layer and a
        // custom contentItem label for the issue title.
        QVERIFY(qml.contains(QStringLiteral("objectName: \"issueTitleLabel\"")));
        QVERIFY(!qml.contains(QStringLiteral("id: issueTitleButton")));
        QVERIFY(qml.contains(QStringLiteral("text: \"#\" + model.number + \"  \" + model.title")));
        QVERIFY(qml.contains(QStringLiteral("textFormat: Text.PlainText")));
    }

    void responsiveIssueHeaderAndPlasmaColors()
    {
        QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/ChecklistView.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(file.readAll());
        QVERIFY(qml.contains(QStringLiteral("id: titleAndLabels")));
        QVERIFY(qml.contains(QStringLiteral("width: Math.min(implicitWidth, titleAndLabels.width)")));
        QVERIFY(qml.contains(QStringLiteral("color: Kirigami.Theme.textColor")));
        QVERIFY(qml.contains(QStringLiteral("Kirigami.Theme.highlightColor")));
        QVERIFY(!qml.contains(QStringLiteral("color: \"#253a4a\"")));
        QVERIFY(!qml.contains(QStringLiteral("color: \"#d2eaff\"")));
    }

    void externalIssueTextIsAlwaysPlainText()
    {
        // Defensive contract: every Controls.Label in the two views must
        // render user-controlled GitHub content as plain text, never as HTML
        // (which could attempt network image loads from issue titles).
        for (const QString &view : {QStringLiteral("Main.qml"), QStringLiteral("ChecklistView.qml")}) {
            QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/") + view);
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
            const QString qml = QString::fromUtf8(file.readAll());
            const QRegularExpression start(QStringLiteral(R"(Controls\.Label\s*\{)"));
            const QRegularExpression safe(QStringLiteral(R"(Controls\.Label\s*\{\s*textFormat:\s*Text\.PlainText)"));
            int count = 0;
            auto it = start.globalMatch(qml);
            while (it.hasNext()) {
                it.next();
                ++count;
            }
            int guarded = 0;
            auto safeIt = safe.globalMatch(qml);
            while (safeIt.hasNext()) {
                safeIt.next();
                ++guarded;
            }
            QVERIFY2(count > 0 && count == guarded,
                     qPrintable(QStringLiteral("%1: %2 of %3 labels are plain text").arg(view).arg(guarded).arg(count)));
        }
    }
};

QTEST_MAIN(QmlSecurityTest)
#include "tst_qmlsecurity.moc"
