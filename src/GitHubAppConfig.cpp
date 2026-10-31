#include "GitHubAppConfig.h"

#include <QByteArray>
#include <QtGlobal>

namespace GitHubAppConfig
{

QString clientIdFromEnvironment()
{
    return QString::fromUtf8(qgetenv("KGITDO_GITHUB_CLIENT_ID")).trimmed();
}

QString manageInstallationsUrl()
{
    return QStringLiteral("https://github.com/settings/installations");
}

QString authorizedAppsUrl()
{
    return QStringLiteral("https://github.com/settings/apps/authorizations");
}

QString deviceVerificationUrl()
{
    return QStringLiteral("https://github.com/login/device");
}

} // namespace GitHubAppConfig
