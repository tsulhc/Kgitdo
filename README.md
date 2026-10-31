# Kgitdo

**Your GitHub Issues, your KDE to-do list.**

*Kgitdo* (pronounced **KappaGitdo**) is a free and open-source, KDE-native to-do application that uses **GitHub Issues as tasks**.

The goal is simple: keep track of issues you created or were assigned to, across repositories, in a clean checklist that feels at home on KDE Plasma. No full Git client, no heavyweight project-management suite, and no second task database to maintain.

> [!IMPORTANT]
> **Project status: planning / pre-alpha.** This README describes the intended product and its initial scope. There is no usable application yet.

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

These are architectural intentions, not an implemented or finalized stack.

## Non-goals

Kgitdo is **not** intended to replace GitHub Issues, GitHub Projects, a Git client, or a general-purpose project-management application. It should not require copying issues into another service to track progress.

## Development

Kgitdo is at the beginning of development. There are no installation or build instructions yet because application code has not been added.

Ideas and implementation discussions belong in [GitHub Issues](https://github.com/tsulhc/Kgitdo/issues); prefer extending existing discussions over opening duplicates.

## License

Kgitdo is licensed under the [MIT License](LICENSE).
