#include "GitHubAppConfig.h"

#include <QByteArray>
#include <QtGlobal>

namespace GitHubAppConfig
{

QString clientIdFromEnvironment()
{
    return QString::fromUtf8(qgetenv("KGITDO_GITHUB_CLIENT_ID")).trimmed();
}

QString builtinClientId()
{
    // Official bundled public client ID. Empty until the operator provides
    // it; landing it is a single-constant change in this function.
    return QString();
}

QString effectiveClientId()
{
    const QString override = clientIdFromEnvironment();
    if (!override.isEmpty()) {
        return override;
    }
    return builtinClientId();
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
