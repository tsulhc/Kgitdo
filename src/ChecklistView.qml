// ChecklistView: W4 interactive issue checklist on top of the W2 model contract.
// All data and actions come from issueController (view/state/repo filters,
// loading, outcomes, reload, close/reopen/undo); all sign-in state from
// authSession. No auth, HTTP, credential, or persistence logic lives here.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: checklist

    spacing: Kirigami.Units.largeSpacing

    // Mirrors AuthSession::Status (see Main.qml): 4 is Authenticated.
    readonly property bool isClosedView: issueController.issueState === 1

    function openIssueUrl(url) {
        if (typeof url === "string" && url.indexOf("https://github.com/") === 0) {
            Qt.openUrlExternally(url);
        }
    }

    function emptyStateText() {
        if (issueController.loadedCount > 0) {
            return i18n("No issues match the current filters.");
        }
        if (isClosedView) {
            return i18n("No closed issues in this view.");
        }
        if (issueController.view === 1) {
            return i18n("No open issues assigned to you.");
        }
        if (issueController.view === 2) {
            return i18n("No open issues created by you.");
        }
        return i18n("No open issues assigned to you or created by you.");
    }

    // Compact account header (plan §3.4: account/status, refresh, filters).
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                font.bold: true
                text: i18n("Signed in as %1", authSession.userLogin)
                Accessible.name: text
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: authSession.volatileSession
                text: i18n("Wallet unavailable: this session is kept only in memory and will not survive a restart.")
            }
            RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Controls.Button {
                    text: i18n("Refresh")
                    enabled: !issueController.loading && !authSession.pending
                    onClicked: issueController.reload()
                    Accessible.name: text
                }
                Controls.Button {
                    text: i18n("Refresh session")
                    enabled: !authSession.pending
                    onClicked: authSession.refreshSession()
                    Accessible.name: text
                }
                Controls.Button {
                    text: i18n("Sign out")
                    enabled: !authSession.pending
                    onClicked: authSession.logout()
                    Accessible.name: text
                }
            }
        }
    }

    // Filter bar: view, state, repository. Wraps on narrow windows.
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Flow {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Controls.ComboBox {
                    Accessible.name: i18n("View")
                    model: [i18n("All"), i18n("Assigned to me"), i18n("Created by me")]
                    currentIndex: issueController.view
                    onActivated: function (index) { issueController.view = index; }
                }
                Controls.ComboBox {
                    Accessible.name: i18n("State")
                    model: [i18n("Open"), i18n("Closed")]
                    currentIndex: issueController.issueState
                    onActivated: function (index) { issueController.issueState = index; }
                }
                Controls.ComboBox {
                    Accessible.name: i18n("Repository")
                    // A filter can outlive its repository: switching view or
                    // state may drop the selected repo from availableRepos.
                    // Keep it as an explicit last entry so the UI never claims
                    // "All repositories" while the list stays filtered.
                    property bool staleFilter: issueController.repoFilter.length > 0
                        && issueController.availableRepos.indexOf(issueController.repoFilter) < 0
                    model: staleFilter
                        ? [i18n("All repositories")].concat(issueController.availableRepos).concat([issueController.repoFilter])
                        : [i18n("All repositories")].concat(issueController.availableRepos)
                    currentIndex: {
                        if (issueController.repoFilter.length === 0) {
                            return 0;
                        }
                        const row = issueController.availableRepos.indexOf(issueController.repoFilter);
                        if (row >= 0) {
                            return row + 1;
                        }
                        return count - 1;
                    }
                    onActivated: function (index) {
                        if (index <= 0) {
                            issueController.repoFilter = "";
                        } else if (index - 1 < issueController.availableRepos.length) {
                            issueController.repoFilter = issueController.availableRepos[index - 1];
                        }
                        // Re-selecting the stale entry leaves the filter unchanged.
                    }
                }
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("%1 of %2 shown", issueController.visibleCount, issueController.loadedCount)
                opacity: 0.8
                Accessible.name: text
            }
        }
    }

    // Outcome banners: errors block with retry, warnings stay informational.
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        visible: issueController.errorMessage.length > 0 || issueController.hasWarning
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: issueController.errorMessage.length > 0
                text: issueController.errorMessage
                Accessible.name: text
            }
            Controls.Button {
                visible: issueController.errorMessage.length > 0
                text: i18n("Retry")
                enabled: !issueController.loading
                onClicked: issueController.reload()
                Accessible.name: text
            }
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                visible: issueController.hasWarning
                text: issueController.warningMessage
                opacity: 0.9
                Accessible.name: text
            }
        }
    }

    // Undo banner: one-shot reopen offer after a confirmed close.
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        visible: issueController.canUndoClose
        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Controls.Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: issueController.undoMessage
                Accessible.name: text
            }
            Controls.Button {
                text: i18n("Undo")
                onClicked: issueController.undoLastClose()
                Accessible.name: text
            }
            Controls.Button {
                text: i18n("Dismiss")
                onClicked: issueController.dismissUndo()
                Accessible.name: text
            }
        }
    }

    // Loading state (list kept visible underneath when refreshing old data).
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        visible: issueController.loading && issueController.visibleCount === 0
        spacing: Kirigami.Units.smallSpacing
        Controls.BusyIndicator {
            running: true
        }
        Controls.Label {
            text: i18n("Loading issues…")
            Accessible.name: text
        }
    }

    // The checklist itself.
    Controls.ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: issueController.errorMessage.length === 0 && issueController.visibleCount > 0
        ListView {
            id: issueList
            anchors.fill: parent
            model: issueController.model
            spacing: Kirigami.Units.smallSpacing
            focus: true
            activeFocusOnTab: true
            clip: true
            Accessible.name: i18n("Issues")
            delegate: Kirigami.AbstractCard {
                width: ListView.view.width
                focus: ListView.isCurrentItem
                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    // Pessimistic checkbox: the visual reverts immediately
                    // and only a confirmed server response changes the
                    // list (the row leaves on success, stays on failure
                    // with a warning). Success always removes the row, so
                    // the box never needs to display post-mutation state
                    // and a broken binding cannot show a stale state.
                    Controls.CheckBox {
                        checked: model.state === "closed"
                        enabled: !model.pending && !issueController.loading
                        onClicked: {
                            checked = model.state === "closed";
                            if (model.state === "open") {
                                issueController.closeIssue(model.issueId);
                            } else {
                                issueController.reopenIssue(model.issueId);
                            }
                        }
                        Accessible.name: model.state === "closed" ? i18n("Closed") : i18n("Open")
                    }
                    Controls.BusyIndicator {
                        visible: model.pending
                        running: visible
                        Accessible.name: i18n("Updating issue")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: model.title
                            Accessible.name: text
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.75
                            text: {
                                let line = model.qualifiedNumber;
                                if (model.updatedAt !== undefined && !isNaN(model.updatedAt.getTime())) {
                                    line += " · " + model.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat);
                                }
                                const flags = [];
                                if (model.isAssignedToMe) {
                                    flags.push(i18n("assigned to you"));
                                }
                                if (model.isAuthoredByMe) {
                                    flags.push(i18n("created by you"));
                                }
                                if (flags.length > 0) {
                                    line += " · " + flags.join(", ");
                                }
                                return line;
                            }
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            visible: model.labels !== undefined && model.labels.length > 0
                            opacity: 0.75
                            text: model.labels !== undefined ? model.labels.join(", ") : ""
                        }
                    }
                    Controls.ToolButton {
                        text: i18n("Open in browser")
                        icon.name: "document-open"
                        onClicked: checklist.openIssueUrl(model.htmlUrl)
                        Accessible.name: text
                    }
                }
                Keys.onReturnPressed: checklist.openIssueUrl(model.htmlUrl)
                Keys.onEnterPressed: checklist.openIssueUrl(model.htmlUrl)
            }
        }
    }

    // Empty state (distinct from errors and loading).
    ColumnLayout {
        Layout.fillWidth: true
        visible: issueController.errorMessage.length === 0 && !issueController.loading && issueController.visibleCount === 0
        spacing: Kirigami.Units.smallSpacing
        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: checklist.emptyStateText()
            Accessible.name: text
        }
        Controls.Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            visible: issueController.loadedCount === 0
            opacity: 0.7
            text: i18n("The app sees only repositories where it is installed and you have access; organization approval may be required.")
        }
        Controls.Button {
            Layout.alignment: Qt.AlignHCenter
            visible: issueController.loadedCount === 0
            text: i18n("Manage GitHub App installations")
            onClicked: authSession.openManageInstallationsPage()
            Accessible.name: text
        }
    }
}
