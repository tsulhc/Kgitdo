#include "IssueListModel.h"

IssueListModel::IssueListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int IssueListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_issues.size();
}

QVariant IssueListModel::data(const QModelIndex &index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_issues.size()) {
        return QVariant();
    }
    const Issue &issue = m_issues.at(row);
    switch (role) {
    case IssueIdRole:
        return QString::number(issue.id);
    case OwnerRole:
        return issue.owner;
    case RepoRole:
        return issue.repo;
    case RepoFullNameRole:
        return issue.repoFullName();
    case NumberRole:
        return issue.number;
    case QualifiedNumberRole:
        return issue.qualifiedNumber();
    case TitleRole:
        return issue.title;
    case StateRole:
        return issue.state;
    case LabelsRole:
        return issue.labels;
    case HtmlUrlRole:
        return issue.htmlUrl;
    case UpdatedAtRole:
        return issue.updatedAt;
    case AssignedToMeRole:
        return issue.isAssignedToMe;
    case AuthoredByMeRole:
        return issue.isAuthoredByMe;
    case PendingRole:
        return issue.pending;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> IssueListModel::roleNames() const
{
    return {
        {IssueIdRole, "issueId"},
        {OwnerRole, "owner"},
        {RepoRole, "repo"},
        {RepoFullNameRole, "repoFullName"},
        {NumberRole, "number"},
        {QualifiedNumberRole, "qualifiedNumber"},
        {TitleRole, "title"},
        {StateRole, "state"},
        {LabelsRole, "labels"},
        {HtmlUrlRole, "htmlUrl"},
        {UpdatedAtRole, "updatedAt"},
        {AssignedToMeRole, "isAssignedToMe"},
        {AuthoredByMeRole, "isAuthoredByMe"},
        {PendingRole, "pending"},
    };
}

void IssueListModel::setIssues(const QList<Issue> &issues)
{
    beginResetModel();
    m_issues = issues;
    endResetModel();
}
