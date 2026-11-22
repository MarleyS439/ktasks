// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

// The "type a task and press Enter" field with a live preview of what the
// natural language parser understood.
ColumnLayout {
    id: root

    property alias text: field.text
    property alias field: field
    /** List the task goes to; 0 lets the application decide. */
    property var listId: 0
    signal added(var taskId)
    signal escaped()

    readonly property var preview: App.parsePreview(field.text)
    readonly property bool hasPreview: field.text.trim() !== "" && (!!preview.due || preview.priority > 0 || (preview.tags?.length ?? 0) > 0 || !!preview.repeat
                                                                    || !!preview.list)

    function submit() {
        if (field.text.trim() === "") {
            return;
        }
        const id = App.quickAdd(field.text, root.listId);
        field.text = "";
        root.added(id);
    }

    spacing: Kirigami.Units.smallSpacing

    RowLayout {
        spacing: Kirigami.Units.smallSpacing
        Layout.fillWidth: true

        QQC2.TextField {
            id: field
            Layout.fillWidth: true
            placeholderText: i18nc("@info:placeholder", "Add a task… try “Pay rent tomorrow 9am !high #home”")
            Accessible.name: i18nc("@label", "New task")
            onAccepted: root.submit()
            Keys.onEscapePressed: {
                if (text !== "") {
                    text = "";
                } else {
                    root.escaped();
                }
            }
        }
        QQC2.Button {
            icon.name: "list-add"
            text: i18nc("@action:button", "Add")
            enabled: field.text.trim() !== ""
            onClicked: root.submit()
        }
    }

    Flow {
        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing
        visible: root.hasPreview

        QQC2.Label {
            text: root.preview.title ?? ""
            font.bold: true
            elide: Text.ElideRight
            width: Math.min(implicitWidth, parent.width)
        }
        Chip {
            visible: !!root.preview.due
            iconName: "view-calendar-day"
            text: root.preview.due ?? ""
            filled: true
            tint: "#38bdf8"
        }
        Chip {
            visible: !!root.preview.repeat
            iconName: "media-playlist-repeat"
            text: root.preview.repeat ?? ""
            filled: true
            tint: "#a855f7"
        }
        Chip {
            visible: (root.preview.priority ?? 0) > 0
            iconName: "flag"
            text: [ "", i18nc("priority", "Low"), i18nc("priority", "Medium"), i18nc("priority", "High") ][root.preview.priority ?? 0]
            filled: true
            tint: [ Kirigami.Theme.textColor, "#22c55e", "#f59e0b", "#ef4444" ][root.preview.priority ?? 0]
        }
        Chip {
            visible: !!root.preview.list
            iconName: "view-list-details"
            text: root.preview.list ?? ""
            filled: true
            tint: "#14b8a6"
        }
        Repeater {
            model: root.preview.tags ?? []
            Chip {
                required property string modelData
                text: "#" + modelData
                filled: true
                tint: "#fb923c"
            }
        }
    }
}
