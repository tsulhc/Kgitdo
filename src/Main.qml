// Kgitdo shell with GitHub App device-flow sign-in (W1).
// Issue checklist arrives with W2/W3; this page proves secure sign-in,
// session restore, KWallet persistence, and user-visible auth states.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root

    width: 800
    height: 600
    minimumWidth: 360
    minimumHeight: 480

    title: i18nc("@title:window", "Kgitdo")

    pageStack.initialPage: Kirigami.Page {
        title: i18nc("@title", "To-do")

        // Status values mirror AuthSession::Status:
        // 0 SignedOut, 1 RequestingDeviceCode, 2 AwaitingAuthorization,
        // 3 Refreshing, 4 Authenticated, 5 Failed.
        readonly property bool isAuthenticated: authSession.isAuthenticated
        readonly property bool isAwaiting: authSession.status === 2
        readonly property bool isBusy: authSession.status === 1 || authSession.status === 3
        readonly property bool isFailed: authSession.status === 5
        readonly property bool isSignedOut: authSession.status === 0

        Kirigami.CardsLayout {
            anchors.fill: parent
            maximumColumns: 1

            Kirigami.AbstractCard {
                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    Controls.Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: i18n("Kgitdo")
                        font.pointSize: 22
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        Accessible.name: text
                    }

                    Controls.Label {
                        Layout.fillWidth: true
                        text: i18n("Your GitHub Issues, your KDE to-do list.")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }

                    // Account / status line (header-light, per plan §3.4).
                    Controls.Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: {
                            if (!authSession.clientIdConfigured) {
                                return i18n("GitHub App not configured");
                            }
                            if (isAuthenticated) {
                                return i18n("Signed in as %1", authSession.userLogin);
                            }
                            if (isAwaiting) {
                                return i18n("Waiting for GitHub authorization…");
                            }
                            if (isBusy) {
                                return i18n("Contacting GitHub…");
                            }
                            if (isFailed) {
                                return i18n("Sign-in needs attention");
                            }
                            return i18n("Not signed in");
                        }
                        font.bold: true
                        Accessible.name: text
                    }

                    // Not configured: onboarding without secrets.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: !authSession.clientIdConfigured
                        spacing: Kirigami.Units.smallSpacing

                        Controls.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: i18n("Set the public GitHub App client ID via KGITDO_GITHUB_CLIENT_ID, then restart. No secret belongs in the app or repository.")
                            opacity: 0.85
                        }
                        Controls.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Manage GitHub App installations")
                            onClicked: authSession.openManageInstallationsPage()
                            Accessible.name: text
                        }
                    }

                    // Authenticated view.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: authSession.clientIdConfigured && isAuthenticated
                        spacing: Kirigami.Units.smallSpacing

                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            visible: authSession.volatileSession
                            text: i18n("Wallet unavailable: this session is kept only in memory and will not survive a restart.")
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: i18n("Issue list arrives in the next milestone. Sign-in and session restore already work.")
                            opacity: 0.7
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Kirigami.Units.smallSpacing
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
                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: i18n("Sign-out removes credentials from this device only. To revoke GitHub access, manage the app on github.com.")
                            opacity: 0.7
                        }
                        Controls.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Manage GitHub App installations")
                            onClicked: authSession.openManageInstallationsPage()
                            Accessible.name: text
                        }
                    }

                    // Awaiting authorization: device code + browser step.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: authSession.clientIdConfigured && isAwaiting
                        spacing: Kirigami.Units.smallSpacing

                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: i18n("Enter this code on GitHub:")
                        }
                        Controls.TextField {
                            Layout.alignment: Qt.AlignHCenter
                            text: authSession.userCode
                            readOnly: true
                            selectByMouse: true
                            font.pointSize: 20
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            Accessible.name: i18n("GitHub device code")
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: authSession.verificationUri
                            opacity: 0.8
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            visible: authSession.deviceExpiresInSeconds > 0
                            text: i18n("Code expires in %1 seconds", authSession.deviceExpiresInSeconds)
                            opacity: 0.7
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: Kirigami.Units.smallSpacing
                            Controls.Button {
                                text: i18n("Open browser")
                                onClicked: authSession.openVerificationPage()
                                Accessible.name: text
                            }
                            Controls.Button {
                                text: i18n("Cancel")
                                onClicked: authSession.cancelLogin()
                                Accessible.name: text
                            }
                        }
                        Controls.BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: true
                        }
                    }

                    // Busy: requesting code or refreshing/validating.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: authSession.clientIdConfigured && isBusy
                        spacing: Kirigami.Units.smallSpacing
                        Controls.BusyIndicator {
                            Layout.alignment: Qt.AlignHCenter
                            running: true
                        }
                        Controls.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Cancel")
                            visible: authSession.status === 1
                            onClicked: authSession.cancelLogin()
                            Accessible.name: text
                        }
                    }

                    // Signed out / failed: entry point + errors.
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: authSession.clientIdConfigured && (isSignedOut || isFailed)
                        spacing: Kirigami.Units.smallSpacing

                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            visible: isFailed && authSession.errorMessage.length > 0
                            text: authSession.errorMessage
                        }
                        Controls.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Sign in with GitHub")
                            enabled: !authSession.pending
                            onClicked: authSession.startLogin()
                            Accessible.name: text
                        }
                        Controls.Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: i18n("Uses the GitHub App device flow. The app sees only repositories where it is installed and you have access; organization approval may be required.")
                            opacity: 0.7
                        }
                        Controls.Button {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Manage GitHub App installations")
                            onClicked: authSession.openManageInstallationsPage()
                            Accessible.name: text
                        }
                    }
                }
            }
        }
    }
}
