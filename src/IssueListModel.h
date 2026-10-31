#pragma once

#include <QAbstractListModel>
#include <QList>

#include "Issue.h"

// QML-facing list of issues. Holds the visible (repo-filtered) slice;
// IssueController owns the full loaded set and pushes filtered lists here.
class IssueListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IssueIdRole = Qt::UserRole + 1,
        OwnerRole,
        RepoRole,
        RepoFullNameRole,
        NumberRole,
        QualifiedNumberRole,
        TitleRole,
        StateRole,
        LabelsRole,
        HtmlUrlRole,
        UpdatedAtRole,
        AssignedToMeRole,
        AuthoredByMeRole,
        PendingRole
    };
    Q_ENUM(Roles)

    explicit IssueListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QList<Issue> issues() const { return m_issues; }
    void setIssues(const QList<Issue> &issues);

    // W4: flip one row's pending flag / confirmed state without a full
    // model reset (preserves selection and focus). False when the id is
    // not in the visible slice.
    bool setIssuePending(qlonglong id, bool pending);
    bool isIssuePending(qlonglong id) const;
    bool applyIssueState(qlonglong id, const QString &state);
    bool removeIssue(qlonglong id);

private:
    QList<Issue> m_issues;
};
