#include <QtTest>

#include "IssueListModel.h"

class IssueListModelTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void rolesAndData();
    void emptyModel();
    void resetReplaces();
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
    QVERIFY(model.roleNames().value(IssueListModel::TitleRole) == QByteArray("title"));
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

QTEST_MAIN(IssueListModelTest)
#include "tst_issuemodel.moc"
