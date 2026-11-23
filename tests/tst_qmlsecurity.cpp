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

    void labelFilterHasAccessibleClientSideControls()
    {
        // Static QML contract check only; this does not execute Qt Quick.
        QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/ChecklistView.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(file.readAll());
        QVERIFY(qml.contains(QStringLiteral("issueController.availableLabels")));
        QVERIFY(qml.contains(QStringLiteral("issueController.labelFilter")));
        QVERIFY(qml.contains(QStringLiteral("Labels from loaded issues")));
        QVERIFY(qml.contains(QStringLiteral("id: issueCard")));
        QVERIFY(qml.contains(QStringLiteral("model: issueCard.issueLabels")));
        QVERIFY(qml.contains(QStringLiteral("Controls.ToolTip.visible: hovered")));
        QVERIFY(qml.contains(QStringLiteral("property bool staleFilter")));
        QVERIFY(qml.contains(QStringLiteral("Clear filters")));
        QVERIFY(qml.contains(QStringLiteral("Accessible.name: selected")));
    }

    void savedFilterTabsHaveAccessiblePlainTextControls()
    {
        QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/ChecklistView.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(file.readAll());
        QVERIFY(qml.contains(QStringLiteral("issueController.savedFilters")));
        QVERIFY(qml.contains(QStringLiteral("issueController.activeSavedFilter")));
        QVERIFY(qml.contains(QStringLiteral("issueController.applySavedFilter(index)")));
        QVERIFY(qml.contains(QStringLiteral("issueController.removeSavedFilter(index)")));
        QVERIFY(qml.contains(QStringLiteral("Accessible.name: i18n(\"Apply saved filter: %1\"")));
        QVERIFY(qml.contains(QStringLiteral("Accessible.name: i18n(\"Remove saved filter: %1\"")));
        QVERIFY(qml.contains(QStringLiteral("text: i18n(\"Rename…\")")));
        QVERIFY(qml.contains(QStringLiteral("issueController.renameSavedFilter(checklist.editingSavedFilterIndex")));
        QVERIFY(qml.contains(QStringLiteral("icon.name: \"tab-close\"")));
        QVERIFY(qml.contains(QStringLiteral("icon.name: \"overflow-menu\"")));
        QVERIFY(qml.contains(QStringLiteral("display: Controls.AbstractButton.IconOnly")));
        QVERIFY(qml.contains(QStringLiteral("maximumLength: issueController.maxSavedFilterNameLength")));
        QVERIFY(qml.contains(QStringLiteral("issueController.maxSavedFilters")));
        QVERIFY(qml.contains(QStringLiteral("Accessible.name: text")));
        QVERIFY(qml.contains(QStringLiteral("textFormat: Text.PlainText")));
    }

    void automaticRefreshHasAccessibleControls()
    {
        QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/ChecklistView.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(file.readAll());
        QVERIFY(qml.contains(QStringLiteral("issueController.autoRefreshMinutes")));
        QVERIFY(qml.contains(QStringLiteral("Automatic refresh interval")));
        QVERIFY(qml.contains(QStringLiteral("i18n(\"Off\")")));
        QVERIFY(qml.contains(QStringLiteral("issueController.lastRefresh")));
        QVERIFY(qml.contains(QStringLiteral("onBackgroundRefreshStarted")));
        QVERIFY(qml.contains(QStringLiteral("issueList.contentY = checklist.savedIssueListContentY")));
    }

    void lastRefreshUsesJavaScriptDateFormatting()
    {
        QFile file(QStringLiteral(KGITDO_SOURCE_DIR) + QStringLiteral("/src/ChecklistView.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString qml = QString::fromUtf8(file.readAll());
        // QDateTime properties are exposed as JavaScript Date objects in QML.
        // Keep this static check because the runtime auth-page test doesn't
        // load a GitHub issue response to make lastRefresh valid.
        QVERIFY(qml.contains(QStringLiteral("issueController.lastRefresh.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)")));
        QVERIFY(!qml.contains(QStringLiteral("toLocalTime(")));
    }
};

QTEST_MAIN(QmlSecurityTest)
#include "tst_qmlsecurity.moc"
