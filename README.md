# Kgitdo

**Your GitHub Issues, your KDE to-do list.**

*Kgitdo* (pronounced **KappaGitdo**) is a free and open-source, KDE-native to-do application that uses **GitHub Issues as tasks**.

The goal is simple: keep track of issues you created or were assigned to, across repositories, in a clean checklist that feels at home on KDE Plasma. No full Git client, no heavyweight project-management suite, and no second task database to maintain.

> [!IMPORTANT]
> **Project status: W6 release candidate.** This README describes the intended product and its initial scope. The Kirigami shell builds and runs with GitHub App device-flow sign-in, KWallet session persistence, and an issue checklist (All/Assigned/Created views, Open/Closed state, repository filter) over issues assigned to you or created by you; the checkbox closes an open issue (or reopens a closed one) directly on GitHub, with a short undo window after closing. Rate-limit failures surface a suggested retry delay from the server headers.

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

### Obtaining a client ID (one-time setup)

Kgitdo needs the **public client ID** of a GitHub App registered by you (or your organization). Device flow uses only this public ID — never a client secret or private key.

1. Open <https://github.com/settings/apps> and choose **New GitHub App**. Fill in a name and homepage URL.
2. Under **Permissions**, grant the minimum the app needs: repository **Issues: Read and write** plus **Metadata: Read-only**. Do not add broader permissions.
3. Enable **Device flow** in the app's settings (user authorization): Kgitdo signs in with the device authorization flow, which GitHub requires to be turned on — otherwise sign-in is rejected (`device_flow_disabled`).
4. Create the app, then **install** it on your account (and on any organization repositories you want to track) from the app's **Install App** page. Organization installs may need an administrator's approval.
5. Copy the **Client ID** shown on the app's settings page and export it as `KGITDO_GITHUB_CLIENT_ID` before starting Kgitdo (see step 1 below).
6. If sign-in reports an unknown client ID, double-check the copied value. Never paste a client secret or private key anywhere near this app.

Official reference: <https://docs.github.com/en/apps/creating-github-apps/authenticating-with-a-github-app/generating-a-user-access-token-for-a-github-app>

1. Set the public client ID and run the app:

   ```bash
   export KGITDO_GITHUB_CLIENT_ID=<public-client-id>
   ~/.local/bin/kgitdo
   ```

2. Choose **Sign in with GitHub**. The app copies the shown verification code to your clipboard when one is available and opens `https://github.com/login/device` automatically — enter (or paste) the code there and approve the app. If the browser does not open, use the on-screen **Copy code** and **Open GitHub** buttons.
3. The waiting screen shows the code with **Copy code**, **Open GitHub**, and **Cancel** while the app polls at the server-provided interval, honors `slow_down` backoff, and stores the expiring access token plus refresh token in **KWallet** (`Kgitdo` folder). If the wallet is unavailable or refused, the session stays volatile in memory and the UI says so.
4. Sign-out clears local credentials and memory. If the wallet cannot be opened or entries cannot be removed (e.g. locked wallet), sign-out still ends the session but warns that saved credentials may remain — a **Retry removing saved credentials** button stays visible until the cleanup succeeds. Entry removal is verified by delisting and read-back and a commit marker keeps interrupted saves unloadable, so a half-written session is never resurrected. Sign-out does not revoke the authorization on github.com; use **Manage GitHub App installations** (`https://github.com/settings/installations`) to adjust installations or revoke access.

Limits: a GitHub App token can only access repositories where the app is **installed** and you have access. Organization installs may need admin approval. The app requests an expiring token (`offline_access`) so refresh works even when expiry is optional on the registration.

Security: tokens never live in plain files, logs, QSettings, or CI. `401` (revoked/expired) and `bad_refresh_token` clear local state and require sign-in again.

## Reading issues (search API)

After sign-in the app loads open issues **assigned to you or created by you** across the repositories the GitHub App installation can see, using two `GET /search/issues` queries (`author:<login>` and `assignee:<login>`, `is:issue`, newest first, 100 per page) whose results are merged and deduplicated by stable issue id — an issue from both queries appears exactly once with both provenances. Pull-request entries are never shown. Views (All / Assigned / Created) and Open / Closed reload from GitHub; the repository filter applies to loaded data, with loaded/visible counts, so a narrow filter is never confused with an empty account.

Partial results are always explicit: the UI warns when GitHub reports `incomplete_results`, when the 1,000-result search cap is hit, or when pagination is interrupted (data loaded so far stays visible). `401` asks for sign-in again; rate limits (`429`/`403` quota, `Retry-After` / `X-RateLimit-Reset`) surface a wait-and-retry warning instead of retrying in a loop.

## Completing and reopening issues (PATCH)

The checklist checkbox mutates state on GitHub: ticking an open issue sends `PATCH /repos/{owner}/{repo}/issues/{number}` with `{"state": "closed", "state_reason": "completed"}`; unticking a closed one in the Closed view sends `{"state": "open"}`. Nothing else is touched — title, body, assignees, and labels are never sent. A change counts only when GitHub echoes the requested state, so an unexpected server reply is treated as a failure rather than a success.

Guards and feedback: the row shows a spinner while its PATCH is in flight and a second click on the same issue sends nothing (the first request owns the outcome); stale callbacks after logout or account switch are discarded. A failed mutation is a warning banner, not an error screen — the list stays visible and the row returns to its prior state. Closing shows a short undo banner that restores the row by reopening the same issue on GitHub.

## Known issues and limitations (0.1)

- **Authorization scope:** the app sees only repositories where its GitHub App is installed and your account has access. If expected issues are missing, check **Manage GitHub App installations** (`https://github.com/settings/installations`); organization installs may need admin approval.
- **Search limits:** GitHub search returns at most 1,000 results per query and may report `incomplete_results`. The app warns whenever the list may be partial instead of claiming completeness.
- **Rate limits:** when GitHub throttles requests, the app shows the server-suggested wait and does not retry mutations on its own. Wait for the delay, then use **Refresh** or retry the checkbox.
- **One account at a time:** signing in as a different user clears the previous account's list, filters, and undo state. Sign-out removes local credentials only; revoke the authorization itself on github.com.
- **Locked or missing wallet:** if KWallet cannot be opened, the session stays in memory only and sign-out warns until saved credentials can actually be removed (a retry button is offered).
- **No offline mode:** the list always reflects GitHub at load time; without a network connection it shows an error instead of stale data. There are no background refreshes, notifications, or widgets in 0.1.

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

### AppImage (x86_64)

Successful CI runs produce an `Kgitdo-0.1.0-x86_64.AppImage` artifact. Download
the artifact from the GitHub Actions run, make it executable, and launch it:

```bash
chmod +x Kgitdo-0.1.0-x86_64.AppImage
KGITDO_GITHUB_CLIENT_ID=<public-client-id> ./Kgitdo-0.1.0-x86_64.AppImage
```

You must register and install a GitHub App and enable device flow as explained
above. The client ID is public, but is deliberately not bundled because no
production app registration has been supplied. KWallet requires a working KDE
session and D-Bus. The AppImage packages Qt/KDE libraries but is not a
substitute for testing on a real Plasma desktop. No release is declared until
login, issue changes and KWallet have passed live acceptance tests.
