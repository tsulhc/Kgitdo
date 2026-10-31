#include <QtTest>

#include "IssueListModel.h"

class IssueListModelTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void rolesAndData();
    void emptyModel();
    void resetReplaces();
    void pendingToggle();
    void removeIssue();
};

void IssueListModelTest::rolesAndData()
{
    Issue issue;
    issue.id = 42;
    issue.owner = QStringLiteral("o");
    issue.repo = QStringLiteral("r");
    issue.number = 7;
    issue.title = QStringLiteral("Title");
    issue.state = QStringLiteral("open");
    issue.labels = QStringList{QStringLiteral("bug")};
    issue.htmlUrl = QStringLiteral("https://github.com/o/r/issues/7");
    issue.updatedAt = QDateTime::fromString(QStringLiteral("2026-10-01T10:00:00Z"), Qt::ISODate);
    issue.isAssignedToMe = true;
    IssueListModel model;
    model.setIssues({issue});
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0)), 0); // no children
    const QModelIndex row = model.index(0);
    QCOMPARE(model.data(row, IssueListModel::IssueIdRole).toString(), QStringLiteral("42"));
    QCOMPARE(model.data(row, IssueListModel::QualifiedNumberRole).toString(), QStringLiteral("o/r#7"));
    QCOMPARE(model.data(row, IssueListModel::RepoFullNameRole).toString(), QStringLiteral("o/r"));
    QCOMPARE(model.data(row, IssueListModel::TitleRole).toString(), QStringLiteral("Title"));
    QCOMPARE(model.data(row, IssueListModel::AssignedToMeRole).toBool(), true);
    QCOMPARE(model.data(row, IssueListModel::AuthoredByMeRole).toBool(), false);
    QVERIFY(!model.data(row, 9999).isValid()); // unknown role
    QVERIFY(!model.data(model.index(5), IssueListModel::TitleRole).isValid()); // out of range
    // QML contract: ChecklistView binds these role names directly.
    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(IssueListModel::TitleRole), QByteArray("title"));
    QCOMPARE(names.value(IssueListModel::QualifiedNumberRole), QByteArray("qualifiedNumber"));
    QCOMPARE(names.value(IssueListModel::RepoFullNameRole), QByteArray("repoFullName"));
    QCOMPARE(names.value(IssueListModel::StateRole), QByteArray("state"));
    QCOMPARE(names.value(IssueListModel::LabelsRole), QByteArray("labels"));
    QCOMPARE(names.value(IssueListModel::HtmlUrlRole), QByteArray("htmlUrl"));
    QCOMPARE(names.value(IssueListModel::UpdatedAtRole), QByteArray("updatedAt"));
    QCOMPARE(names.value(IssueListModel::AssignedToMeRole), QByteArray("isAssignedToMe"));
    QCOMPARE(names.value(IssueListModel::AuthoredByMeRole), QByteArray("isAuthoredByMe"));
    QCOMPARE(names.value(IssueListModel::PendingRole), QByteArray("pending"));
}

void IssueListModelTest::emptyModel()
{
    IssueListModel model;
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.index(0).isValid() || !model.data(model.index(0)).isValid());
}

void IssueListModelTest::resetReplaces()
{
    IssueListModel model;
    Issue a;
    a.id = 1;
    model.setIssues({a, a});
    QCOMPARE(model.rowCount(), 2);
    model.setIssues({});
    QCOMPARE(model.rowCount(), 0);
}

void IssueListModelTest::pendingToggle()
{
    IssueListModel model;
    Issue a;
    a.id = 1;
    model.setIssues({a});
    QVERIFY(!model.isIssuePending(1));
    QVERIFY(!model.isIssuePending(999));
    QVERIFY(model.setIssuePending(1, true));
    QVERIFY(model.isIssuePending(1));
    QVERIFY(!model.setIssuePending(999, true)); // absent row: no-op
    QVERIFY(model.setIssuePending(1, false));
    QVERIFY(!model.isIssuePending(1));
    // The pending role reflects the flag for QML spinners.
    QCOMPARE(model.data(model.index(0), IssueListModel::PendingRole).toBool(), false);
    QVERIFY(model.setIssuePending(1, true));
    QCOMPARE(model.data(model.index(0), IssueListModel::PendingRole).toBool(), true);
}

void IssueListModelTest::removeIssue()
{
    IssueListModel model;
    Issue a;
    a.id = 1;
    Issue b;
    b.id = 2;
    model.setIssues({a, b});
    QVERIFY(model.removeIssue(1));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), IssueListModel::IssueIdRole).toString(),
             QStringLiteral("2"));
    QVERIFY(!model.removeIssue(999)); // absent row: no-op
    QCOMPARE(model.rowCount(), 1);
}

QTEST_MAIN(IssueListModelTest)
#include "tst_issuemodel.moc"
