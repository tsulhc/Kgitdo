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

void QNetworkIssueApi::searchIssues(const QUrl &url, const QString &accessToken,
                                    std::function<void(IssueHttpResult)> callback)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("Authorization", QByteArray("Bearer ") + accessToken.toUtf8());

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
