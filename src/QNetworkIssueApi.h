#pragma once

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>

#include "IssueApi.h"

// Production IIssueApi over QNetworkAccessManager: Bearer user token,
// GitHub API headers, 30s timeout, lower-cased headers for the client.
// Redirects are same-origin-only so the Bearer token can never be forwarded
// to another host: pagination targets are additionally restricted to the
// GitHub API origin before any request is issued.
class QNetworkIssueApi : public IIssueApi
{
public:
    explicit QNetworkIssueApi(QObject *parent = nullptr);

    // Fully configured API request (headers + same-origin redirect policy).
    // Exposed for unit coverage of the redirect/credential invariant.
    static QNetworkRequest makeApiRequest(const QUrl &url, const QString &accessToken);

private:
    void searchIssues(const QUrl &url, const QString &accessToken,
                      std::function<void(IssueHttpResult)> callback) override;

    QNetworkAccessManager *m_nam = nullptr;
};
