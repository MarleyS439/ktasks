// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

// One row of the task list. Wrapped in an Item so the drag handle can move
// the delegate while the wrapper keeps its place in the ListView.
Item {
    id: wrapper

    required property int index
    required property var taskId
    required property string title
    required property bool done
    required property int priority
    required property string dueText
    required property bool overdue
    required property bool dueToday
    required property string listName
    required property string listColor
    required property var tags
    required property int depth
    required property bool hasChildren
    required property bool collapsed
    required property int childCount
    required property int childDone
    required property bool recurring
    required property bool hasReminder
    required property bool hasNotes
    required property bool readOnly

    property bool selected: false
    signal activated()
    signal contextMenuRequested()

    width: ListView.view ? ListView.view.width : implicitWidth
    height: delegate.implicitHeight
    implicitHeight: delegate.implicitHeight

    // Dragging a row onto a list in the sidebar moves the task there.
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.MoveAction
    Drag.mimeData: { "application/x-ktasks-task": String(wrapper.taskId) }

    QQC2.ItemDelegate {
        id: delegate

        width: wrapper.width
        highlighted: wrapper.selected
        leftPadding: Kirigami.Units.smallSpacing + wrapper.depth * Kirigami.Units.gridUnit * 1.5
        rightPadding: Kirigami.Units.largeSpacing
        Accessible.name: wrapper.title
        onClicked: wrapper.activated()

        contentItem: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            QQC2.ToolButton {
                visible: App.tasks.isListView
                opacity: wrapper.hasChildren ? 1 : 0
                enabled: wrapper.hasChildren
                icon.name: wrapper.collapsed ? (Qt.application.layoutDirection === Qt.RightToLeft ? "arrow-left" : "arrow-right") : "arrow-down"
                icon.width: Kirigami.Units.iconSizes.small
                icon.height: Kirigami.Units.iconSizes.small
                implicitWidth: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing * 2
                implicitHeight: implicitWidth
                Accessible.name: wrapper.collapsed ? i18nc("@action:button", "Show subtasks") : i18nc("@action:button", "Hide subtasks")
                onClicked: App.tasks.toggleCollapsed(wrapper.taskId)
            }

            QQC2.CheckBox {
                checked: wrapper.done
                enabled: !wrapper.readOnly
                Accessible.name: wrapper.done ? i18nc("@action", "Reopen task") : i18nc("@action", "Complete task")
                onToggled: App.setDone(wrapper.taskId, checked)
            }

            Rectangle {
                visible: wrapper.priority > 0 && !wrapper.done
                Layout.preferredWidth: Math.round(Kirigami.Units.smallSpacing * 0.75)
                Layout.fillHeight: true
                Layout.topMargin: Kirigami.Units.smallSpacing / 2
                Layout.bottomMargin: Kirigami.Units.smallSpacing / 2
                radius: width / 2
                color: [ "transparent", Kirigami.Theme.positiveTextColor, Kirigami.Theme.neutralTextColor, Kirigami.Theme.negativeTextColor ][wrapper.priority]
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing / 2

                QQC2.Label {
                    Layout.fillWidth: true
                    text: wrapper.title
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    font.strikeout: wrapper.done
                    opacity: wrapper.done ? 0.6 : 1
                    color: delegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    visible: wrapper.dueText !== "" || wrapper.recurring || wrapper.hasReminder || wrapper.hasNotes || wrapper.childCount > 0
                             || wrapper.tags.length > 0 || (!App.tasks.isListView && wrapper.listName !== "")
                    opacity: wrapper.done ? 0.6 : 1

                    readonly property color baseTint: delegate.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor

                    Chip {
                        visible: wrapper.dueText !== ""
                        iconName: "view-calendar-day"
                        text: wrapper.dueText
                        filled: (wrapper.overdue || wrapper.dueToday) && !delegate.highlighted
                        tint: delegate.highlighted ? Kirigami.Theme.highlightedTextColor
                              : wrapper.overdue ? "#ef4444"
                              : wrapper.dueToday ? "#38bdf8" : Kirigami.Theme.textColor
                    }
                    Chip {
                        visible: wrapper.recurring
                        iconName: "media-playlist-repeat"
                        tint: delegate.highlighted ? Kirigami.Theme.highlightedTextColor : "#a855f7"
                        Accessible.name: i18nc("@info", "Repeats")
                    }
                    Chip {
                        visible: wrapper.hasReminder
                        iconName: "notifications"
                        tint: parent.baseTint
                        Accessible.name: i18nc("@info", "Has a reminder")
                    }
                    Chip {
                        visible: wrapper.hasNotes
                        iconName: "view-list-text"
                        tint: parent.baseTint
                        Accessible.name: i18nc("@info", "Has notes")
                    }
                    Chip {
                        visible: wrapper.childCount > 0
                        iconName: "view-list-tree"
                        text: i18nc("completed subtasks / all subtasks", "%1/%2", wrapper.childDone, wrapper.childCount)
                        tint: parent.baseTint
                    }
                    Chip {
                        visible: !App.tasks.isListView && wrapper.listName !== ""
                        text: wrapper.listName
                        iconName: "view-list-details"
                        filled: !delegate.highlighted
                        tint: wrapper.listColor !== "" && !delegate.highlighted ? wrapper.listColor : (delegate.highlighted ? Kirigami.Theme.highlightedTextColor : "#14b8a6")
                    }
                    Repeater {
                        model: wrapper.tags
                        Chip {
                            required property string modelData
                            text: "#" + modelData
                            filled: !delegate.highlighted
                            tint: delegate.highlighted ? Kirigami.Theme.highlightedTextColor : "#fb923c"
                        }
                    }
                }
            }

            Kirigami.ListItemDragHandle {
                visible: App.tasks.canReorder
                listItem: delegate
                listView: wrapper.ListView.view
                onMoveRequested: (oldIndex, newIndex) => App.tasks.move(oldIndex, newIndex)
                onDropped: App.tasks.commitMove()
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: wrapper.contextMenuRequested()
        }

        DragHandler {
            id: dragHandler
            target: null
            enabled: !wrapper.readOnly
            acceptedDevices: PointerDevice.Mouse
            dragThreshold: Kirigami.Units.gridUnit
            onActiveChanged: {
                if (active) {
                    delegate.grabToImage(result => {
                        wrapper.Drag.imageSource = result.url;
                        wrapper.Drag.active = dragHandler.active;
                    });
                } else {
                    wrapper.Drag.active = false;
                }
            }
        }
    }
}
