// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.delegates as Delegates
import app.ktasks.backend

Kirigami.OverlayDrawer {
    id: drawer

    property string currentSection: "summary" // "summary", "routine", "matrix", "journal", "insights", "tasks"
    signal sectionSelected(string section)
    signal editListRequested(var listId)
    signal newListRequested()
    signal settingsRequested()
    signal printRequested()

    readonly property bool wideScreen: applicationWindow().width >= Kirigami.Units.gridUnit * 42
    readonly property int minWidthPx: Math.round(Kirigami.Units.gridUnit * 11)
    readonly property int maxWidthPx: Math.max(minWidthPx + 40, Math.min(Math.round(Kirigami.Units.gridUnit * 28), Math.round(applicationWindow().width * 0.45)))

    edge: Qt.application.layoutDirection === Qt.RightToLeft ? Qt.RightEdge : Qt.LeftEdge
    modal: !wideScreen
    handleVisible: modal
    onModalChanged: drawerOpen = !modal
    Component.onCompleted: drawerOpen = !modal
    width: modal ? Math.min(Math.round(Kirigami.Units.gridUnit * 17), applicationWindow().width * 0.85)
                 : Math.max(minWidthPx, Math.min(maxWidthPx, App.settings.sidebarWidth))
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    contentItem: Item {
        anchors.fill: parent

        ColumnLayout {
            anchors.fill: parent
            anchors.rightMargin: resizeHandle.visible ? resizeHandle.width : 0
            spacing: 0

        QQC2.ToolBar {
            Layout.fillWidth: true
            Layout.preferredHeight: applicationWindow().pageStack.globalToolBar.preferredHeight
            leftPadding: Kirigami.Units.largeSpacing
            rightPadding: Kirigami.Units.smallSpacing

            contentItem: RowLayout {
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 2
                    text: i18nc("@title application name", "KTasks")
                    elide: Text.ElideRight
                }
                QQC2.ToolButton {
                    icon.name: "application-menu"
                    Accessible.name: i18nc("@action:button", "Menu")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    down: appMenu.visible
                    onClicked: appMenu.popup(this, 0, height)

                    QQC2.Menu {
                        id: appMenu
                        QQC2.MenuItem {
                            icon.name: "folder-new"
                            text: i18nc("@action:inmenu", "New List…")
                            onTriggered: drawer.newListRequested()
                        }
                        QQC2.MenuItem {
                            icon.name: "view-refresh"
                            text: i18nc("@action:inmenu", "Synchronize Now")
                            enabled: App.sync.hasAccounts
                            onTriggered: App.sync.syncNow()
                        }
                        QQC2.MenuSeparator {}
                        QQC2.MenuItem {
                            icon.name: "document-import"
                            text: i18nc("@action:inmenu", "Import from iCalendar File…")
                            onTriggered: App.trigger("import_ics")
                        }
                        QQC2.MenuItem {
                            icon.name: "document-export"
                            text: i18nc("@action:inmenu", "Export to iCalendar File…")
                            onTriggered: App.trigger("export_ics")
                        }
                        QQC2.MenuItem {
                            icon.name: "document-print"
                            text: i18nc("@action:inmenu", "Print / Export PDF…")
                            onTriggered: drawer.printRequested()
                        }
                        QQC2.MenuSeparator {}
                        QQC2.MenuItem {
                            icon.name: "configure"
                            text: i18nc("@action:inmenu", "Settings…")
                            onTriggered: drawer.settingsRequested()
                        }
                        QQC2.MenuItem {
                            icon.name: "configure-shortcuts"
                            text: i18nc("@action:inmenu", "Configure Keyboard Shortcuts…")
                            onTriggered: App.trigger("options_configure_keybinding")
                        }
                        QQC2.MenuItem {
                            icon.name: "search"
                            text: i18nc("@action:inmenu", "Find Action…")
                            onTriggered: App.trigger("open_kcommand_bar")
                        }
                        QQC2.MenuItem {
                            icon.name: "help-about"
                            text: i18nc("@action:inmenu", "About KTasks")
                            onTriggered: App.trigger("open_about_page")
                        }
                        QQC2.MenuSeparator {}
                        QQC2.MenuItem {
                            icon.name: "application-exit"
                            text: i18nc("@action:inmenu", "Quit")
                            onTriggered: App.quit()
                        }
                    }
                }
            }
        }

        // Primary workspace switcher in logical daily flow order
        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            spacing: 2

            Repeater {
                model: [
                    { sec: "summary",  text: i18nc("@action:inmenu workspace tab", "Summary"),                 icon: "go-home" },
                    { sec: "routine",  text: i18nc("@action:inmenu workspace tab", "Routine & Planner"),       icon: "chronometer" },
                    { sec: "matrix",   text: i18nc("@action:inmenu workspace tab", "Priority Matrix"),         icon: "view-grid" },
                    { sec: "journal",  text: i18nc("@action:inmenu workspace tab", "Journal"),                 icon: "story-editor" },
                    { sec: "insights", text: i18nc("@action:inmenu workspace tab", "Productivity & Insights"), icon: "view-statistics" }
                ]
                Delegates.RoundedItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    text: modelData.text
                    icon.name: modelData.icon
                    highlighted: drawer.currentSection === modelData.sec
                    onClicked: {
                        drawer.sectionSelected(modelData.sec);
                        if (drawer.modal) {
                            drawer.close();
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: view
                model: App.sidebar
                clip: true
                topMargin: Kirigami.Units.smallSpacing
                bottomMargin: Kirigami.Units.smallSpacing

                section.property: "section"
                section.delegate: Kirigami.ListSectionHeader {
                    required property string section
                    width: view.width
                    text: section
                    visible: section !== ""
                    height: visible ? implicitHeight : 0
                }

                delegate: Delegates.RoundedItemDelegate {
                    id: entry

                    required property int index
                    required property string key
                    required property string name
                    required property string iconName
                    required property string color
                    required property int count
                    required property string kind
                    required property var listId
                    required property bool readOnly
                    required property bool pending

                    width: view.width
                    text: name
                    highlighted: drawer.currentSection === "tasks" && App.tasks.view === key && App.tasks.searchText === "" || dropArea.containsDrag
                    onClicked: {
                        drawer.sectionSelected("tasks");
                        App.tasks.searchText = "";
                        App.tasks.view = key;
                        if (drawer.modal) {
                            drawer.close();
                        }
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Item {
                            Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium

                            Kirigami.Icon {
                                anchors.fill: parent
                                visible: !dot.visible
                                source: entry.iconName
                                color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }
                            Rectangle {
                                id: dot
                                visible: entry.kind === "list" && entry.color !== "" && !entry.readOnly
                                anchors.centerIn: parent
                                width: Kirigami.Units.iconSizes.small * 0.7
                                height: width
                                radius: width / 2
                                color: entry.color !== "" ? entry.color : "transparent"
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: entry.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        }
                        Kirigami.Icon {
                            visible: entry.pending
                            source: "cloud-upload"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.7
                            color: entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Has changes waiting to be synchronized")
                            QQC2.ToolTip.visible: pendingHover.hovered
                            HoverHandler { id: pendingHover }
                        }
                        QQC2.Label {
                            visible: entry.count > 0
                            text: entry.count
                            font: Kirigami.Theme.smallFont
                            color: entry.key === "overdue" && !entry.highlighted ? Kirigami.Theme.negativeTextColor
                                   : entry.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        enabled: entry.kind === "list"
                        onTapped: listMenu.popup()
                    }

                    QQC2.Menu {
                        id: listMenu
                        QQC2.MenuItem {
                            icon.name: "document-edit"
                            text: i18nc("@action:inmenu", "Edit List…")
                            enabled: !entry.readOnly
                            onTriggered: drawer.editListRequested(entry.listId)
                        }
                        QQC2.MenuItem {
                            icon.name: "edit-clear-history"
                            text: i18nc("@action:inmenu", "Delete Completed Tasks")
                            enabled: !entry.readOnly
                            onTriggered: App.deleteCompleted(entry.listId)
                        }
                    }

                    DropArea {
                        id: dropArea
                        anchors.fill: parent
                        enabled: entry.kind === "list" && !entry.readOnly
                        keys: ["application/x-ktasks-task"]
                        onDropped: drop => {
                            App.moveTask(Number(drop.getDataAsString("application/x-ktasks-task")), entry.listId);
                            drop.accept(Qt.MoveAction);
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // User profile card (system avatar or custom + name + email)
        QQC2.ItemDelegate {
            Layout.fillWidth: true
            onClicked: drawer.settingsRequested()
            QQC2.ToolTip.text: i18nc("@info:tooltip", "Open profile and settings")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Components.Avatar {
                    source: App.effectiveAvatarPath !== "" ? ("file://" + App.effectiveAvatarPath) : ""
                    name: App.effectiveProfileName
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: App.effectiveProfileName
                        font.bold: true
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        visible: App.effectiveProfileEmail !== ""
                        Layout.fillWidth: true
                        text: App.effectiveProfileEmail
                        font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.92
                        color: Kirigami.Theme.disabledTextColor
                        elide: Text.ElideRight
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // Sync status
        QQC2.ItemDelegate {
            Layout.fillWidth: true
            enabled: true
            onClicked: App.sync.hasAccounts ? App.sync.syncNow() : drawer.settingsRequested()
            QQC2.ToolTip.text: App.sync.errorText !== "" ? App.sync.errorText
                               : App.sync.hasAccounts ? i18nc("@info:tooltip", "Click to synchronize now")
                               : i18nc("@info:tooltip", "Tasks are stored on this device only. Click to connect a Nextcloud account.")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.BusyIndicator {
                    visible: App.sync.syncing
                    running: visible
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                Kirigami.Icon {
                    visible: !App.sync.syncing
                    source: App.sync.errorText !== "" ? "data-warning"
                            : !App.sync.hasAccounts ? "computer-symbolic"
                            : App.sync.offline ? "network-disconnect" : "cloud-download"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    text: App.sync.statusText
                    font: Kirigami.Theme.smallFont
                    elide: Text.ElideRight
                    color: App.sync.errorText !== "" ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.disabledTextColor
                }
            }
        }
        }

        // Interactive resize handle on the inner edge of the sidebar
        Rectangle {
            id: resizeHandle
            visible: !drawer.modal
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: Qt.application.layoutDirection === Qt.RightToLeft ? undefined : parent.right
            anchors.left: Qt.application.layoutDirection === Qt.RightToLeft ? parent.left : undefined
            width: 5
            color: resizeArea.containsMouse || resizeArea.pressed ? Kirigami.Theme.highlightColor : "transparent"
            opacity: resizeArea.containsMouse || resizeArea.pressed ? 0.45 : 1.0

            MouseArea {
                id: resizeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                property real startGlobalX: 0
                property int startWidth: 0
                onPressed: mouse => {
                    startGlobalX = mapToGlobal(mouse.x, mouse.y).x;
                    startWidth = drawer.width;
                }
                onPositionChanged: mouse => {
                    if (!pressed) {
                        return;
                    }
                    const gx = mapToGlobal(mouse.x, mouse.y).x;
                    const delta = Qt.application.layoutDirection === Qt.RightToLeft ? (startGlobalX - gx) : (gx - startGlobalX);
                    const next = Math.max(drawer.minWidthPx, Math.min(drawer.maxWidthPx, Math.round(startWidth + delta)));
                    App.settings.sidebarWidth = next;
                }
                onDoubleClicked: App.settings.sidebarWidth = 240
            }
        }
    }
}
