#pragma once

#include <QNetworkAccessManager>
#include <QObject>

#include "IssueApi.h"

// Production IIssueApi over QNetworkAccessManager: Bearer user token,
// GitHub API headers, 30s timeout, lower-cased headers for the client.
class QNetworkIssueApi : public IIssueApi
{
public:
    explicit QNetworkIssueApi(QObject *parent = nullptr);

private:
    void searchIssues(const QUrl &url, const QString &accessToken,
                      std::function<void(IssueHttpResult)> callback) override;

    QNetworkAccessManager *m_nam = nullptr;
};
