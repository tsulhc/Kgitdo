#include "QNetworkIssueApi.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace
{
constexpr int kTimeoutMs = 30000;
}

QNetworkIssueApi::QNetworkIssueApi(QObject *parent)
    : IIssueApi(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QNetworkRequest QNetworkIssueApi::makeApiRequest(const QUrl &url, const QString &accessToken)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());
    // Never forward the Bearer token to another origin: only redirects that
    // stay on the same scheme/host/port are followed automatically. Anything
    // else surfaces as the 3xx response and is treated as an API error.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    return request;
}

void QNetworkIssueApi::searchIssues(const QUrl &url, const QString &accessToken,
                                    std::function<void(IssueHttpResult)> callback)
{
    QNetworkRequest request = makeApiRequest(url, accessToken);

    QNetworkReply *reply = m_nam->get(request);
    auto *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setInterval(kTimeoutMs);
    connect(timer, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    timer->start();

    connect(reply, &QNetworkReply::finished, reply, [reply, callback]() {
        IssueHttpResult result;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.httpStatus = status;
        const auto headers = reply->rawHeaderPairs();
        for (const auto &pair : headers) {
            result.headers.insert(QString::fromUtf8(pair.first).toLower(), QString::fromUtf8(pair.second));
        }
        result.body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && result.body.isEmpty() && status == 0) {
            result.transportError = QStringLiteral("Network request failed. Check the connection and retry.");
        }
        reply->deleteLater();
        callback(result);
    });
}
