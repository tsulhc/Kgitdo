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
    // Official bundled public client ID (GitHub App device flow).
    // This identifier is public by design (sent in cleartext HTTPS
    // requests); it is not a secret. KGITDO_GITHUB_CLIENT_ID still
    // overrides it for development and testing.
    return QStringLiteral("Iv23liV6HbrnNijBdmHN");
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
