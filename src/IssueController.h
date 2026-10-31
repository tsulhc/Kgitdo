#pragma once

#include <QDateTime>
#include <QObject>
#include <QStringList>

#include "AuthSession.h"
#include "GitHubClient.h"
#include "IssueListModel.h"
#include "IssueQuery.h"

// Owns the checklist state: view/state/repo filters, loading, the full
// loaded set, and the fetch outcome flags. View/state changes reload from
// GitHub; the repository filter applies client-side to loaded data so a
// narrow filter is never confused with an empty account.
class IssueController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(int issueState READ issueState WRITE setIssueState NOTIFY issueStateChanged)
    Q_PROPERTY(QString repoFilter READ repoFilter WRITE setRepoFilter NOTIFY repoFilterChanged)
    Q_PROPERTY(QStringList availableRepos READ availableRepos NOTIFY availableReposChanged)
    Q_PROPERTY(IssueListModel *model READ model CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    Q_PROPERTY(bool hasWarning READ hasWarning NOTIFY warningChanged)
    Q_PROPERTY(QString warningMessage READ warningMessage NOTIFY warningChanged)
    Q_PROPERTY(bool incompleteResults READ incompleteResults NOTIFY outcomeChanged)
    Q_PROPERTY(bool cappedResults READ cappedResults NOTIFY outcomeChanged)
    Q_PROPERTY(bool truncatedResults READ truncatedResults NOTIFY outcomeChanged)
    Q_PROPERTY(bool rateLimited READ rateLimited NOTIFY outcomeChanged)
    Q_PROPERTY(int retryAfterSecs READ retryAfterSecs NOTIFY outcomeChanged)
    Q_PROPERTY(int loadedCount READ loadedCount NOTIFY countsChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY countsChanged)
    Q_PROPERTY(QDateTime lastRefresh READ lastRefresh NOTIFY lastRefreshChanged)
    Q_PROPERTY(bool canUndoClose READ canUndoClose NOTIFY undoChanged)
    Q_PROPERTY(QString undoMessage READ undoMessage NOTIFY undoChanged)

public:
    // Mirror IssueQuery enums for QML (int-compatible values).
    enum View {
        ViewAll = 0,
        ViewAssigned = 1,
        ViewCreated = 2
    };
    Q_ENUM(View)
    enum IssueState {
        StateOpen = 0,
        StateClosed = 1
    };
    Q_ENUM(IssueState)

    explicit IssueController(AuthSession *authSession, GitHubClient *client, QObject *parent = nullptr);

    int view() const { return m_view; }
    int issueState() const { return m_issueState; }
    QString repoFilter() const { return m_repoFilter; }
    QStringList availableRepos() const { return m_availableRepos; }
    IssueListModel *model() const { return m_model; }
    bool loading() const { return m_loading; }
    QString errorMessage() const { return m_errorMessage; }
    bool hasWarning() const { return !m_warningMessage.isEmpty(); }
    QString warningMessage() const { return m_warningMessage; }
    bool incompleteResults() const { return m_incomplete; }
    bool cappedResults() const { return m_capped; }
    bool truncatedResults() const { return m_truncated; }
    bool rateLimited() const { return m_rateLimited; }
    int retryAfterSecs() const { return m_retryAfterSecs; }
    int loadedCount() const { return m_loaded.size(); }
    int visibleCount() const { return m_model ? m_model->rowCount() : 0; }
    QDateTime lastRefresh() const { return m_lastRefresh; }
    bool canUndoClose() const { return m_undo.id != 0; }
    QString undoMessage() const { return m_undoMessage; }

    void setView(int view);
    void setIssueState(int state);
    void setRepoFilter(const QString &repoFullName);

    Q_INVOKABLE void reload();
    Q_INVOKABLE void closeIssue(const QString &issueId);
    Q_INVOKABLE void reopenIssue(const QString &issueId);
    Q_INVOKABLE void undoLastClose();
    Q_INVOKABLE void dismissUndo();

Q_SIGNALS:
    void viewChanged();
    void issueStateChanged();
    void repoFilterChanged();
    void availableReposChanged();
    void loadingChanged();
    void errorMessageChanged();
    void warningChanged();
    void outcomeChanged();
    void countsChanged();
    void lastRefreshChanged();
    void undoChanged();

private:
    void setLoading(bool loading);
    void applyRepoFilter();
    void clearOutcomeFlags();
    // Drops every account-derived state (list, repos, filter, messages,
    // outcome flags, refresh stamp, undo snapshot) and invalidates
    // in-flight fetches. Shared by sign-out and new-identity sign-in cleanup.
    void clearAllState();
    // One-shot close/reopen: resolves coordinates from the loaded list,
    // guards double activation via the pending flag, then delegates.
    void sendStateChange(qlonglong id, bool closed, bool isUndo);
    void clearUndo();
    int loadedRow(qlonglong id) const;

    AuthSession *m_auth = nullptr;
    GitHubClient *m_client = nullptr;
    IssueListModel *m_model = nullptr;

    int m_view = ViewAll;
    int m_issueState = StateOpen;
    QString m_repoFilter;
    QStringList m_availableRepos;
    QList<Issue> m_loaded;

    bool m_loading = false;
    int m_generation = 0; // fetch epoch: reload/logout bumps it so stale fetch callbacks die
    // Account epoch: logout and new-identity sign-in bump it so in-flight
    // mutation callbacks drop their results instead of applying one
    // account's confirmed change to another account's (now empty) state.
    // A plain reload does NOT bump this: a successful mutation always
    // reflects server truth, so it still converges locally even when a
    // reload raced it (and provides the close-undo snapshot).
    int m_accountGeneration = 0;
    QString m_errorMessage;
    QString m_warningMessage;
    bool m_incomplete = false;
    bool m_capped = false;
    bool m_truncated = false;
    bool m_rateLimited = false;
    int m_retryAfterSecs = 0;
    QDateTime m_lastRefresh;
    // Login the loaded list belongs to. Compared on each Authenticated
    // transition so a different account signing in without an intermediate
    // sign-out (e.g. refresh failure followed by a fresh device flow) still
    // drops the prior account's issues and reloads for the new identity.
    QString m_lastLogin;
    // One-shot undo snapshot for the last successful close: the issue is
    // re-added locally when the undo reopen succeeds. Cleared on logout,
    // new-identity sign-in, dismiss, successful undo, or a new close.
    struct UndoClose {
        qlonglong id = 0;
        QString owner;
        QString repo;
        int number = 0;
    };
    UndoClose m_undo;
    QString m_undoMessage;
    bool m_undoInFlight = false; // undo PATCH outstanding: second undo is a no-op
};
