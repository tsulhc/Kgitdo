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

    property string openedTitle: ""
    property string openedBody: ""
    property string openedRepo: ""
    property string openedNumber: ""
    property string openedUrl: ""
    property var openedLabels: []
    property string creationNotice: ""

    function showIssue(title, body, repo, number, url, labels) {
        openedTitle = title
        openedBody = body
        openedRepo = repo
        openedNumber = "#" + number
        openedUrl = url
        openedLabels = labels
        detailDialog.open()
    }

    function openCreateDialog() {
        newIssueTitle.text = ""
        newIssueBody.text = ""
        const preferred = issueController.availableRepos.indexOf(issueController.repoFilter)
        newIssueRepo.currentIndex = preferred >= 0 ? preferred : 0
        createDialog.open()
        newIssueTitle.forceActiveFocus()
    }

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

    // Account & actions: a clear headline and one primary creation action.
    Kirigami.AbstractCard {
        Layout.fillWidth: true
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Controls.Label {
                        textFormat: Text.PlainText
                        text: i18n("My issues")
                        font.pixelSize: 23
                        font.bold: true
                        Accessible.name: text
                    }
                    Controls.Label {
                        textFormat: Text.PlainText
                        text: i18n("Connected as %1", authSession.userLogin)
                        opacity: 0.78
                        font.pixelSize: 13
                    }
                }
                Controls.Button {
                    text: i18n("New issue")
                    icon.name: "list-add"
                    highlighted: true
                    enabled: issueController.availableRepos.length > 0 && !issueController.creatingIssue
                    onClicked: checklist.openCreateDialog()
                    Accessible.name: text
                }
                Controls.ToolButton {
                    text: i18n("Refresh")
                    icon.name: "view-refresh"
                    enabled: !issueController.loading && !authSession.pending
                    onClicked: issueController.reload()
                }
                Controls.ToolButton {
                    text: i18n("Sign out")
                    icon.name: "system-log-out"
                    enabled: !authSession.pending && !issueController.creatingIssue
                    onClicked: authSession.logout()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                visible: authSession.volatileSession
                spacing: Kirigami.Units.smallSpacing
                Controls.Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.textColor
                    font.pixelSize: 13
                    text: authSession.walletSaving
                        ? i18n("Saving your login securely in KWallet…")
                        : i18n("Login not saved yet. You may have to sign in again after restarting.")
                }
                Controls.Button {
                    text: i18n("Retry saving")
                    enabled: !authSession.walletSaving
                    visible: !authSession.walletSaving
                    onClicked: authSession.retrySave()
                }
            }
            Controls.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: authSession.walletError.length > 0 && authSession.volatileSession
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.negativeTextColor
                text: authSession.walletError
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
                    font.pixelSize: 14
                    onActivated: function (index) { issueController.view = index; }
                }
                Controls.ComboBox {
                    Accessible.name: i18n("State")
                    model: [i18n("Open"), i18n("Closed")]
                    currentIndex: issueController.issueState
                    font.pixelSize: 14
                    onActivated: function (index) { issueController.issueState = index; }
                }
                Controls.ComboBox {
                    Accessible.name: i18n("Repository")
                    font.pixelSize: 14
                    implicitWidth: 240
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
            Controls.TextField {
                Layout.fillWidth: true
                placeholderText: i18n("Search titles, labels or repositories…")
                text: issueController.searchText
                onTextEdited: issueController.searchText = text
                font.pixelSize: 14
                Accessible.name: i18n("Search issues")
            }
            Controls.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: i18n("%1 issues shown · %2 loaded", issueController.visibleCount, issueController.loadedCount)
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
                textFormat: Text.PlainText
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
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                // Server-provided retry delay for 429/rate-limit failures:
                // a static statement of the suggested wait, not a live
                // countdown — nothing ticks it down in the UI.
                visible: issueController.rateLimited && issueController.retryAfterSecs > 0
                text: i18n("GitHub rate limit reached. Suggested retry delay: %1 seconds.", issueController.retryAfterSecs)
                Accessible.name: text
            }
            Controls.Label {
                textFormat: Text.PlainText
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
                textFormat: Text.PlainText
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
            textFormat: Text.PlainText
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
                id: issueCard
                width: ListView.view.width - 8
                property var rowLabels: model.labels
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing
                    Controls.CheckBox {
                        checked: model.state === "closed"
                        enabled: !model.pending && !issueController.loading
                        onClicked: {
                            checked = model.state === "closed"
                            if (model.state === "open")
                                issueController.closeIssue(model.issueId)
                            else
                                issueController.reopenIssue(model.issueId)
                        }
                        Accessible.name: model.state === "closed"
                            ? i18n("Reopen issue") : i18n("Complete issue")
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        Controls.Button {
                            id: issueTitleButton
                            Layout.fillWidth: true
                            flat: true
                            padding: 2
                            text: model.title
                            font.pixelSize: 17
                            font.bold: true
                            contentItem: Controls.Label {
                                textFormat: Text.PlainText
                                text: issueTitleButton.text
                                font: issueTitleButton.font
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignLeft
                                color: Kirigami.Theme.textColor
                            }
                            onClicked: checklist.showIssue(model.title, model.body,
                                                           model.repoFullName, model.number,
                                                           model.htmlUrl, model.labels)
                            Accessible.name: text
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Rectangle {
                                color: "#253a4a"
                                border.color: "#456478"
                                border.width: 1
                                radius: 6
                                implicitHeight: repoText.implicitHeight + 10
                                implicitWidth: repoText.implicitWidth + 16
                                Controls.Label {
                                    id: repoText
                                    anchors.centerIn: parent
                                    textFormat: Text.PlainText
                                    text: model.repoFullName
                                    color: "#d2eaff"
                                    font.pixelSize: 13
                                    font.bold: true
                                }
                            }
                            Controls.Label {
                                textFormat: Text.PlainText
                                text: "#" + model.number
                                opacity: 0.85
                                font.pixelSize: 13
                            }
                            Controls.Label {
                                textFormat: Text.PlainText
                                text: model.updatedAt !== undefined && !isNaN(model.updatedAt.getTime())
                                    ? model.updatedAt.toLocaleString(Qt.locale(), Locale.ShortFormat) : ""
                                opacity: 0.65
                                font.pixelSize: 12
                            }
                            Item { Layout.fillWidth: true }
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: issueCard.rowLabels
                                delegate: Rectangle {
                                    radius: 9
                                    color: modelData.toLowerCase().includes("bug") ? "#422729"
                                        : modelData.toLowerCase().includes("upgrade") ? "#403519" : "#263e36"
                                    border.color: modelData.toLowerCase().includes("bug") ? "#a8575c"
                                        : modelData.toLowerCase().includes("upgrade") ? "#a88b36" : "#548b74"
                                    implicitWidth: tagText.implicitWidth + 16
                                    implicitHeight: tagText.implicitHeight + 7
                                    Controls.Label {
                                        id: tagText
                                        anchors.centerIn: parent
                                        textFormat: Text.PlainText
                                        text: modelData
                                        color: "#f3f4f6"
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }
                    Controls.BusyIndicator {
                        visible: model.pending
                        running: visible
                    }
                    Controls.ToolButton {
                        icon.name: "document-preview"
                        text: i18n("Read")
                        onClicked: checklist.showIssue(model.title, model.body,
                                                       model.repoFullName, model.number,
                                                       model.htmlUrl, model.labels)
                        Accessible.name: i18n("Read issue in Kgitdo")
                    }
                }
                Keys.onReturnPressed: checklist.showIssue(model.title, model.body,
                                                          model.repoFullName, model.number,
                                                          model.htmlUrl, model.labels)
                Keys.onEnterPressed: checklist.showIssue(model.title, model.body,
                                                         model.repoFullName, model.number,
                                                         model.htmlUrl, model.labels)
            }
        }
    }

    // Empty state (distinct from errors and loading).
    ColumnLayout {
        Layout.fillWidth: true
        visible: issueController.errorMessage.length === 0 && !issueController.loading && issueController.visibleCount === 0
        spacing: Kirigami.Units.smallSpacing
        Controls.Label {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: checklist.emptyStateText()
            Accessible.name: text
        }
        Controls.Label {
            textFormat: Text.PlainText
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
    // In-app reader: full body as plain text, with optional browser action.
    Controls.Dialog {
        id: detailDialog
        title: checklist.openedRepo + " " + checklist.openedNumber
        modal: true
        width: Math.min(760, checklist.width - 36)
        anchors.centerIn: parent
        standardButtons: Controls.Dialog.Close
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Controls.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                text: checklist.openedTitle
                font.pixelSize: 21
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Controls.ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 360
                Controls.TextArea {
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    wrapMode: TextEdit.Wrap
                    text: checklist.openedBody.length > 0
                        ? checklist.openedBody : i18n("No description.")
                    font.pixelSize: 15
                    selectByMouse: true
                }
            }
            Controls.Button {
                Layout.alignment: Qt.AlignRight
                text: i18n("Open on GitHub")
                icon.name: "internet-web-browser"
                onClicked: checklist.openIssueUrl(checklist.openedUrl)
            }
        }
    }

    // Minimal native issue creation with explicit confirmation and no
    // automatic POST replay (a timed-out request may have succeeded).
    Controls.Dialog {
        id: createDialog
        title: i18n("New issue")
        modal: true
        width: Math.min(680, checklist.width - 36)
        anchors.centerIn: parent
        standardButtons: Controls.Dialog.NoButton
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing
            Controls.Label {
                textFormat: Text.PlainText
                text: i18n("Repository")
                font.pixelSize: 14
                font.bold: true
            }
            Controls.ComboBox {
                id: newIssueRepo
                Layout.fillWidth: true
                model: issueController.availableRepos
                Accessible.name: i18n("Repository for new issue")
            }
            Controls.Label {
                textFormat: Text.PlainText
                text: i18n("Title")
                font.pixelSize: 14
                font.bold: true
            }
            Controls.TextField {
                id: newIssueTitle
                Layout.fillWidth: true
                placeholderText: i18n("What needs to be done?")
                maximumLength: 256
                Accessible.name: i18n("Issue title")
            }
            Controls.Label {
                textFormat: Text.PlainText
                text: i18n("Description (Markdown)")
                font.pixelSize: 14
                font.bold: true
            }
            Controls.ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: 200
                Controls.TextArea {
                    id: newIssueBody
                    wrapMode: TextEdit.Wrap
                    placeholderText: i18n("Add details, context, or acceptance criteria…")
                    font.pixelSize: 14
                }
            }
            Controls.Label {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                visible: issueController.createError.length > 0
                text: issueController.createError
                wrapMode: Text.WordWrap
                color: Kirigami.Theme.negativeTextColor
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Controls.Button {
                    text: i18n("Cancel")
                    enabled: !issueController.creatingIssue
                    onClicked: createDialog.close()
                }
                Controls.Button {
                    text: issueController.creatingIssue ? i18n("Creating…") : i18n("Create issue")
                    highlighted: true
                    enabled: !issueController.creatingIssue && newIssueRepo.currentIndex >= 0
                        && newIssueTitle.text.trim().length > 0
                    onClicked: issueController.createIssue(newIssueRepo.currentText,
                                                           newIssueTitle.text, newIssueBody.text)
                }
            }
        }
    }

    Connections {
        target: issueController
        function onIssueCreated(url) {
            createDialog.close()
            checklist.creationNotice = i18n("Issue created successfully.")
        }
    }
    Controls.Label {
        textFormat: Text.PlainText
        Layout.fillWidth: true
        visible: checklist.creationNotice.length > 0
        text: checklist.creationNotice
        Accessible.name: text
    }

}
