#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

// One GitHub issue as shown in the checklist. Identity is the stable
// GitHub node id (never title/number): the same issue can arrive from both
// the author: and assignee: queries and must appear exactly once.
// `pending` is reserved for W4 mutations (close/reopen).
struct Issue {
    qlonglong id = 0;
    QString owner;
    QString repo;
    int number = 0;
    QString title;
    QString state; // "open" or "closed"
    QStringList labels;
    QString htmlUrl;
    QDateTime updatedAt;
    bool isAssignedToMe = false;
    bool isAuthoredByMe = false;
    bool pending = false;

    bool isValid() const { return id != 0; }
    QString repoFullName() const
    {
        if (owner.isEmpty() || repo.isEmpty()) {
            return QString();
        }
        return owner + QLatin1Char('/') + repo;
    }
    QString qualifiedNumber() const
    {
        const QString full = repoFullName();
        if (full.isEmpty() || number <= 0) {
            return QString();
        }
        return full + QLatin1Char('#') + QString::number(number);
    }
};
