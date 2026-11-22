// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

// Small always-on-top window for adding a task from anywhere
// (global shortcut, tray menu, "ktasks --quick-add").
QQC2.ApplicationWindow {
    id: win

    function present() {
        entry.text = "";
        lists.entries = App.lists().filter(l => !l.readOnly);
        let wanted = App.tasks.isListView ? App.tasks.listId : App.settings.defaultListId;
        lists.currentIndex = Math.max(0, lists.entries.findIndex(l => l.id === wanted));
        App.activateQuickAddWindow(win);
        Qt.callLater(() => entry.field.forceActiveFocus());
    }

    readonly property int targetHeight: content.implicitHeight + Kirigami.Units.largeSpacing * 2

    title: i18nc("@title:window", "New Task")
    flags: Qt.Window | Qt.WindowStaysOnTopHint
    transientParent: null
    modality: Qt.NonModal
    width: Kirigami.Units.gridUnit * 30
    height: targetHeight
    minimumHeight: targetHeight
    maximumHeight: targetHeight
    visible: false

    Shortcut {
        sequence: "Escape"
        enabled: entry.text === ""
        onActivated: win.close()
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        QuickEntry {
            id: entry
            Layout.fillWidth: true
            listId: lists.currentIndex >= 0 && lists.entries.length > 0 ? lists.entries[lists.currentIndex].id : 0
            onAdded: win.close()
            onEscaped: win.close()
        }
        RowLayout {
            Layout.fillWidth: true
            QQC2.Label {
                text: i18nc("@label:listbox", "List:")
            }
            QQC2.ComboBox {
                id: lists
                property var entries: []
                Layout.fillWidth: true
                model: entries
                textRole: "name"
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            text: i18nc("@info", "Dates, times, “every monday”, !high, #tag and @list are understood. Enter adds the task, Esc closes.")
        }
    }
}
