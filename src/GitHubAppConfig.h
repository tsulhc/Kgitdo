#pragma once

#include <QString>

// Resolves the public GitHub App client ID and fixed GitHub URLs.
// The client ID is public (not a secret) but must never be hardcoded with
// secrets: it is injected via the KGITDO_GITHUB_CLIENT_ID environment
// variable so test, dev, and release registrations can differ without code
// changes. No client secret or token belongs in the repository.
namespace GitHubAppConfig
{
QString clientIdFromEnvironment();
QString manageInstallationsUrl(); // https://github.com/settings/installations
QString authorizedAppsUrl(); // https://github.com/settings/apps/authorizations
QString deviceVerificationUrl(); // https://github.com/login/device
} // namespace GitHubAppConfig
