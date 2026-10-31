# Kgitdo

**Your GitHub Issues, your KDE to-do list.**

*Kgitdo* (pronounced **KappaGitdo**) is a free and open-source, KDE-native to-do application that uses **GitHub Issues as tasks**.

The goal is simple: keep track of issues you created or were assigned to, across repositories, in a clean checklist that feels at home on KDE Plasma. No full Git client, no heavyweight project-management suite, and no second task database to maintain.

> [!IMPORTANT]
> **Project status: W1 auth implemented.** This README describes the intended product and its initial scope. The Kirigami shell builds and runs with GitHub App device-flow sign-in and KWallet session persistence; the issue checklist arrives in the next milestones.

## Why Kgitdo?

GitHub is great for managing issues, but staying on top of work spread across several repositories can be cumbersome. Sometimes all you want is a to-do list:

- **What do I need to do?** See open issues assigned to you.
- **What am I tracking?** See open issues you created.
- **What changed?** Refresh the list to reflect the current state on GitHub.
- **Is it done?** Close an issue on GitHub without switching to a browser.

Kgitdo aims to provide that workflow through a lightweight, native desktop interface.

## Design principles

- **GitHub is the source of truth.** An issue *is* a task. Kgitdo does not create a separate set of tasks requiring manual synchronization.
- **Native KDE experience.** Built for **Qt 6**, **KDE Frameworks 6** and **Kirigami**, with Plasma-friendly appearance and behavior. No Electron or embedded web application.
- **One list across repositories.** Bring together issues authored by or assigned to the signed-in user, including issues from different repositories and organizations the account can access.
- **Simple before powerful.** A checklist first; advanced issue-management features only where they improve the to-do workflow.
- **Free and open source.** No proprietary dependencies or hosted Kgitdo service required for the basic experience.
- **Respect user permissions.** Access GitHub with the least privileges needed, protect credentials, and clearly distinguish local UI actions from successful GitHub updates.

## Planned MVP

The first usable version should support:

1. **GitHub sign-in** using a suitable secure authorization flow.
2. **Unified to-do list** of open issues **assigned to me** or **created by me**, deduplicated when both apply.
3. **Useful task context:** issue title, repository, issue number, labels, and a link to the original issue.
4. **Simple views and filters:** all my issues, assigned to me, created by me, and repository filter.
5. **Task completion:** close an issue on GitHub from the app, with a clear indication of success or failure.
6. **Reopen issues** and refresh the list to reflect remote changes.
7. **Native KDE desktop experience** with appropriate keyboard navigation, theme support, and accessible controls.

The application should work with public repositories and with private repositories the signed-in account is authorized to access.

### Possible follow-ups (not MVP commitments)

- Plasma panel widget / system tray integration and unread or open-task counters.
- Native KDE notifications for relevant issue activity.
- Saved filters, sorting, and quick search.
- Read-only offline cache and background refresh.
- Additional GitHub workflows, only when they fit the simple to-do model.

## Proposed technology

| Area | Direction |
| --- | --- |
| Language / UI | C++ and QML |
| UI toolkit | Qt 6, KDE Frameworks 6, Kirigami |
| Build | CMake |
| Task data | GitHub Issues via GitHub API |
| Authentication | Secure GitHub authorization; platform credential storage where available |
| Primary desktop | Linux / KDE Plasma 6 |

These are the implemented stack for the W0 foundation (minimal Kirigami shell) plus W1 auth (device flow, KWallet session, QtTest coverage).

## Authentication (GitHub App device flow)

Kgitdo signs in with a **GitHub App** using the device authorization flow for public desktop clients. No client secret is distributed or stored.

1. Set the public client ID and run the app:

   ```bash
   export KGITDO_GITHUB_CLIENT_ID=<public-client-id>
   ~/.local/bin/kgitdo
   ```

2. Choose **Sign in with GitHub**, enter the shown code at `https://github.com/login/device`, and approve the app.
3. The app polls at the server-provided interval, honors `slow_down` backoff, and stores the expiring access token plus refresh token in **KWallet** (`Kgitdo` folder). If the wallet is unavailable or refused, the session stays volatile in memory and the UI says so.
4. Sign-out clears local credentials and memory. If the wallet cannot be opened or entries cannot be removed (e.g. locked wallet), sign-out still ends the session but warns that saved credentials may remain — a **Retry removing saved credentials** button stays visible until the cleanup succeeds. Entry removal is verified read-back and a commit marker keeps interrupted saves unloadable, so a half-written session is never resurrected. Sign-out does not revoke the authorization on github.com; use **Manage GitHub App installations** (`https://github.com/settings/installations`) to adjust installations or revoke access.

Limits: a GitHub App token can only access repositories where the app is **installed** and you have access. Organization installs may need admin approval. The app requests an expiring token (`offline_access`) so refresh works even when expiry is optional on the registration.

Security: tokens never live in plain files, logs, QSettings, or CI. `401` (revoked/expired) and `bad_refresh_token` clear local state and require sign-in again.

## Non-goals

Kgitdo is **not** intended to replace GitHub Issues, GitHub Projects, a Git client, or a general-purpose project-management application. It should not require copying issues into another service to track progress.

## Development

### Build from source (Fedora KDE Plasma 6)

Kgitdo 0.1 targets **Linux / KDE Plasma 6** with **Qt 6**, **KDE Frameworks 6** and **Kirigami**.
The reference build environment is Fedora with KF6 packages. Ubuntu 24.04 LTS
does not ship the required KF6 stack and cannot build the GUI.

Install build dependencies on Fedora:

```bash
sudo dnf install @development-tools @development-libs cmake extra-cmake-modules \
  kf6-kirigami-devel kf6-ki18n-devel kf6-kcoreaddons-devel kf6-kiconthemes-devel \
  kf6-kwallet-devel kf6-qqc2-desktop-style qt6-qtbase-devel qt6-qtdeclarative-devel git
```

Configure, build, test and install to your home directory:

```bash
cmake -B build --install-prefix ~/.local -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

Run the application:

```bash
~/.local/bin/kgitdo
```

Headless smoke test (no display server):

```bash
QT_QPA_PLATFORM=offscreen ~/.local/bin/kgitdo
```

The build installs `kgitdo`, the `org.kde.kgitdo.desktop` launcher entry,
AppStream metadata and the application icon.

Ideas and implementation discussions belong in [GitHub Issues](https://github.com/tsulhc/Kgitdo/issues); prefer extending existing discussions over opening duplicates.

## License

Kgitdo is licensed under the [MIT License](LICENSE).
