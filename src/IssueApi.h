#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>

// Raw HTTP contract for the issue search API. Production uses
// QNetworkAccessManager; tests inject a fake. Callbacks always fire.
struct IssueHttpResult {
    int httpStatus = 0; // 0 when no HTTP response was received
    QMap<QString, QString> headers; // lower-cased header names
    QByteArray body;
    QString transportError; // network/timeout failure, no secrets
};

// (No Q_OBJECT: no signals; QObject parenting only.)
class IIssueApi : public QObject
{
public:
    explicit IIssueApi(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    virtual void searchIssues(const QUrl &url, const QString &accessToken,
                              std::function<void(IssueHttpResult)> callback) = 0;
};
