// Kgitdo foundation window (W0).
// Full checklist UI arrives with W3; this shell proves native
// Kirigami launch, theming, and install integration.
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

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - Kirigami.Units.largeSpacing * 4, 480)
            spacing: Kirigami.Units.largeSpacing

            Controls.Label {
                Layout.alignment: Qt.AlignHCenter
                text: i18n("Kgitdo")
                font.pointSize: 22
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
            }

            Controls.Label {
                Layout.fillWidth: true
                text: i18n("Your GitHub Issues, your KDE to-do list.")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Controls.Label {
                Layout.fillWidth: true
                text: i18n("Foundation build: GitHub sign-in and issue list arrive in the next milestones.")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                opacity: 0.7
            }
        }
    }
}
