#pragma once

#include <QString>

// Resolves the public GitHub App client ID and fixed GitHub URLs.
// The client ID is public (not a secret) but must never be hardcoded with
// secrets: KGITDO_GITHUB_CLIENT_ID overrides the bundled official ID so
// test, dev, and release registrations can differ without code changes.
// End users never register an app: the official bundled ID is the default,
// the environment variable stays a dev/test override. No client secret or
// token belongs in the repository.
namespace GitHubAppConfig
{
QString clientIdFromEnvironment();
// Bundled official public client ID (operator-provided official
// registration; always non-empty in release builds).
QString builtinClientId();
// Effective ID: environment override when set, otherwise the bundled one.
QString effectiveClientId();
QString manageInstallationsUrl(); // https://github.com/settings/installations
QString authorizedAppsUrl(); // https://github.com/settings/apps/authorizations
QString deviceVerificationUrl(); // https://github.com/login/device
} // namespace GitHubAppConfig
