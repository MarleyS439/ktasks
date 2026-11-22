// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

Item {
    id: page

    /** The selected task, shown in the details pane. */
    property var currentTaskId: 0
    /** True when there is room for the details next to the list. */
    property bool showDetailsPane: false
    /** Dolphin-style multi-selection (Ctrl / Shift + click). */
    property var selectedIds: []
    property int selectionAnchorRow: -1
    signal taskActivated(var taskId)
    signal editListRequested(var listId)

    function isTaskSelected(id) {
        return selectedIds.indexOf(id) >= 0;
    }

    function clearSelection() {
        selectedIds = [];
        selectionAnchorRow = -1;
    }

    function selectAllVisible() {
        const next = [];
        for (let r = 0; r < list.count; ++r) {
            const rid = App.tasks.taskIdAt(r);
            if (rid > 0) {
                next.push(rid);
            }
        }
        selectedIds = next;
        selectionAnchorRow = list.count > 0 ? 0 : -1;
        page.taskActivated(next.length === 1 ? next[0] : 0);
    }

    function handleActivateRow(rowIndex, taskId) {
        const mods = App.queryKeyboardModifiers();
        const ctrl = (mods & Qt.ControlModifier) !== 0;
        const shift = (mods & Qt.ShiftModifier) !== 0;
        list.currentIndex = rowIndex;
        list.forceActiveFocus();

        if (shift && list.count > 0) {
            const anchor = (selectionAnchorRow >= 0 && selectionAnchorRow < list.count) ? selectionAnchorRow : 0;
            const lo = Math.min(anchor, rowIndex);
            const hi = Math.max(anchor, rowIndex);
            const next = ctrl ? selectedIds.slice() : [];
            for (let r = lo; r <= hi; ++r) {
                const rid = App.tasks.taskIdAt(r);
                if (rid > 0 && next.indexOf(rid) < 0) {
                    next.push(rid);
                }
            }
            selectedIds = next;
            page.taskActivated(next.length === 1 ? next[0] : 0);
        } else if (ctrl) {
            const next = selectedIds.slice();
            const pos = next.indexOf(taskId);
            if (pos >= 0) {
                next.splice(pos, 1);
            } else {
                next.push(taskId);
            }
            selectedIds = next;
            selectionAnchorRow = rowIndex;
            page.taskActivated(next.length === 1 ? next[0] : 0);
        } else {
            selectedIds = [taskId];
            selectionAnchorRow = rowIndex;
            page.taskActivated(taskId);
        }
    }

    function focusQuickAdd() {
        if (quick.visible) {
            quick.field.forceActiveFocus();
        }
    }
    function reveal(taskId) {
        const row = App.tasks.indexOfTask(taskId);
        if (row >= 0) {
            list.positionViewAtIndex(row, ListView.Contain);
        }
    }

    readonly property string pageTitle: App.tasks.title

    property list<Kirigami.Action> pageActions: [
        Kirigami.Action {
            icon.name: "edit-undo"
            text: App.canUndo ? i18nc("@action:button %1 is what will be undone", "Undo: %1", App.undoText) : i18nc("@action:button", "Undo")
            enabled: App.canUndo
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: App.undo()
        },
        Kirigami.Action {
            icon.name: "edit-redo"
            text: App.canRedo ? i18nc("@action:button %1 is what will be redone", "Redo: %1", App.redoText) : i18nc("@action:button", "Redo")
            enabled: App.canRedo
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: App.redo()
        },
        Kirigami.Action {
            icon.name: "view-sort"
            text: i18nc("@action:button", "Sort")
            displayHint: Kirigami.DisplayHint.IconOnly

            Kirigami.Action {
                text: App.tasks.isListView ? i18nc("@action:inmenu sort order", "My order") : i18nc("@action:inmenu sort order", "Automatic")
                checkable: true
                checked: App.tasks.sortMode === TaskModel.SortDefault
                onTriggered: App.tasks.sortMode = TaskModel.SortDefault
            }
            Kirigami.Action {
                text: i18nc("@action:inmenu sort order", "Due date")
                checkable: true
                checked: App.tasks.sortMode === TaskModel.SortDue
                onTriggered: App.tasks.sortMode = TaskModel.SortDue
            }
            Kirigami.Action {
                text: i18nc("@action:inmenu sort order", "Priority")
                checkable: true
                checked: App.tasks.sortMode === TaskModel.SortPriority
                onTriggered: App.tasks.sortMode = TaskModel.SortPriority
            }
            Kirigami.Action {
                text: i18nc("@action:inmenu sort order", "Title")
                checkable: true
                checked: App.tasks.sortMode === TaskModel.SortTitle
                onTriggered: App.tasks.sortMode = TaskModel.SortTitle
            }
            Kirigami.Action {
                text: i18nc("@action:inmenu sort order", "Newest first")
                checkable: true
                checked: App.tasks.sortMode === TaskModel.SortCreated
                onTriggered: App.tasks.sortMode = TaskModel.SortCreated
            }
        },
        Kirigami.Action {
            icon.name: App.tasks.showCompleted ? "view-visible" : "view-hidden"
            text: App.tasks.showCompleted ? i18nc("@action:button", "Hide Completed") : i18nc("@action:button", "Show Completed")
            visible: App.tasks.view !== "completed"
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: App.tasks.showCompleted = !App.tasks.showCompleted
        },
        Kirigami.Action {
            icon.name: "document-print"
            text: i18nc("@action:button", "Print / Export PDF…")
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: App.trigger("print_tasks")
        },
        Kirigami.Action {
            icon.name: "document-edit"
            text: i18nc("@action:button", "Edit List…")
            visible: App.tasks.isListView && !App.tasks.readOnly
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: page.editListRequested(App.tasks.listId)
        },
        Kirigami.Action {
            icon.name: "edit-clear-history"
            text: i18nc("@action:button", "Delete Completed Tasks")
            displayHint: Kirigami.DisplayHint.AlwaysHide
            enabled: !App.tasks.readOnly
            onTriggered: App.deleteCompleted(App.tasks.listId)
        }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 14
            spacing: 0

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                position: Kirigami.InlineMessage.Position.Header
                visible: App.tasks.readOnly
                type: Kirigami.MessageType.Information
                text: i18nc("@info", "This list is shared with you as read-only. Changes cannot be saved.")
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true
                position: Kirigami.InlineMessage.Position.Header
                visible: App.sync.errorText !== ""
                type: Kirigami.MessageType.Warning
                text: App.sync.errorText
                actions: [
                    Kirigami.Action {
                        icon.name: "view-refresh"
                        text: i18nc("@action:button", "Try Again")
                        onTriggered: App.sync.syncNow()
                    }
                ]
            }

            QuickEntry {
                id: quick
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                visible: !App.tasks.readOnly && App.tasks.view !== "completed" && App.tasks.view !== "overdue"
                onAdded: taskId => {
                    Qt.callLater(page.reveal, taskId);
                }
                onEscaped: list.forceActiveFocus()
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                visible: quick.visible
            }

            // Dolphin-style Multi-Selection Batch Action Bar
            Rectangle {
                visible: page.selectedIds.length > 1
                Layout.fillWidth: true
                implicitHeight: batchFlow.implicitHeight + Kirigami.Units.smallSpacing * 2
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0.12)

                Flow {
                    id: batchFlow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: i18np("%1 task selected", "%1 tasks selected", page.selectedIds.length)
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                        height: batchClearBtn.implicitHeight
                    }
                    QQC2.Button {
                        icon.name: "task-complete"
                        text: i18nc("@action:button", "Complete")
                        flat: true
                        onClicked: App.setDoneMany(page.selectedIds, true)
                    }
                    QQC2.Button {
                        icon.name: "go-next"
                        text: i18nc("@action:button", "+1 Day")
                        flat: true
                        onClicked: App.postponeMany(page.selectedIds, 1)
                    }
                    QQC2.Button {
                        icon.name: "edit-copy"
                        text: i18nc("@action:button", "Copy Details")
                        flat: true
                        onClicked: App.copyTasksDetails(page.selectedIds)
                    }
                    QQC2.Button {
                        icon.name: "mail-send"
                        text: i18nc("@action:button", "Send by Email…")
                        flat: true
                        onClicked: App.emailTasksDetails(page.selectedIds)
                    }
                    QQC2.Button {
                        icon.name: "edit-delete"
                        text: i18nc("@action:button", "Delete")
                        flat: true
                        onClicked: {
                            const ids = page.selectedIds.slice();
                            page.clearSelection();
                            App.deleteTasks(ids);
                        }
                    }
                    QQC2.ToolButton {
                        id: batchClearBtn
                        icon.name: "window-close"
                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Clear selection (Esc)")
                        QQC2.ToolTip.visible: hovered
                        onClicked: page.clearSelection()
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                visible: page.selectedIds.length > 1
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
                background: Rectangle {
                    color: Kirigami.Theme.backgroundColor
                }

                ListView {
                    id: list

                    model: App.tasks
                    clip: true
                    activeFocusOnTab: true
                    keyNavigationEnabled: true
                    currentIndex: -1
                    highlightFollowsCurrentItem: false
                    onCurrentIndexChanged: {
                        if (currentIndex >= 0 && activeFocus) {
                            const tid = App.tasks.taskIdAt(currentIndex);
                            page.selectedIds = tid > 0 ? [tid] : [];
                            page.selectionAnchorRow = currentIndex;
                            page.taskActivated(tid);
                        }
                    }
                    Keys.onPressed: event => {
                        if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_A) {
                            page.selectAllVisible();
                            event.accepted = true;
                        } else if (event.key === Qt.Key_Escape && page.selectedIds.length > 0) {
                            page.clearSelection();
                            page.taskActivated(0);
                            event.accepted = true;
                        }
                    }
                    Keys.onSpacePressed: {
                        if (page.selectedIds.length > 1) {
                            const ref = App.task(page.selectedIds[0]);
                            App.setDoneMany(page.selectedIds, !ref.done);
                        } else if (page.currentTaskId > 0) {
                            const t = App.task(page.currentTaskId);
                            App.setDone(page.currentTaskId, !t.done);
                        }
                    }

                    section.property: "section"
                    section.delegate: Kirigami.ListSectionHeader {
                        required property string section
                        width: list.width
                        text: section
                        visible: section !== ""
                        height: visible ? implicitHeight : 0
                    }

                    delegate: TaskDelegate {
                        selected: page.isTaskSelected(taskId) || taskId === page.currentTaskId
                        onActivated: page.handleActivateRow(index, taskId)
                        onContextMenuRequested: {
                            if (!page.isTaskSelected(taskId)) {
                                page.selectedIds = [taskId];
                                page.selectionAnchorRow = index;
                            }
                            taskMenu.targetIds = page.selectedIds.slice();
                            taskMenu.task = App.task(taskId);
                            taskMenu.popup();
                        }
                    }

                    footer: Item {
                        width: list.width
                        height: hiddenButton.visible ? hiddenButton.implicitHeight + Kirigami.Units.largeSpacing * 2 : 0
                        QQC2.Button {
                            id: hiddenButton
                            anchors.centerIn: parent
                            flat: true
                            visible: App.tasks.hiddenCompleted > 0 && list.count > 0
                            icon.name: "view-visible"
                            text: i18ncp("@action:button", "Show %1 completed task", "Show %1 completed tasks", App.tasks.hiddenCompleted)
                            onClicked: App.tasks.showCompleted = true
                        }
                    }

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: list.count === 0
                        icon.name: App.tasks.searchText !== "" ? "search" : App.tasks.view === "today" || App.tasks.view === "overdue" ? "task-complete" : "view-task"
                        text: App.tasks.searchText !== "" ? i18nc("@info:placeholder", "No task matches “%1”", App.tasks.searchText)
                              : App.tasks.view === "today" ? i18nc("@info:placeholder", "Nothing due today")
                              : App.tasks.view === "overdue" ? i18nc("@info:placeholder", "Nothing overdue")
                              : App.tasks.view === "completed" ? i18nc("@info:placeholder", "No completed tasks yet")
                              : i18nc("@info:placeholder", "No tasks here")
                        explanation: App.tasks.hiddenCompleted > 0 ? i18ncp("@info", "%1 completed task is hidden.", "%1 completed tasks are hidden.", App.tasks.hiddenCompleted)
                                     : quick.visible && App.tasks.searchText === "" ? i18nc("@info", "Type in the field above and press Enter to add one.") : ""
                        helpfulAction: Kirigami.Action {
                            enabled: App.tasks.hiddenCompleted > 0
                            icon.name: "view-visible"
                            text: i18nc("@action:button", "Show Completed")
                            onTriggered: App.tasks.showCompleted = true
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            visible: details.visible
        }

        Loader {
            id: details
            active: page.showDetailsPane && page.currentTaskId > 0
            visible: active
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, page.width * 0.48)
            sourceComponent: TaskDetails {
                taskId: page.currentTaskId
                onCloseRequested: page.taskActivated(0)
                onTaskRequested: tid => page.taskActivated(tid)
            }
        }
    }

    QQC2.Menu {
        id: taskMenu
        property var task: ({})
        property var targetIds: []
        readonly property bool multi: targetIds.length > 1
        readonly property int selCount: Math.max(1, targetIds.length)

        QQC2.MenuItem {
            icon.name: taskMenu.task.done && !taskMenu.multi ? "edit-undo" : "task-complete"
            text: taskMenu.multi ? i18ncp("@action:inmenu", "Complete (%1)", "Complete (%1)", taskMenu.selCount)
                : (taskMenu.task.done ? i18nc("@action:inmenu", "Reopen") : i18nc("@action:inmenu", "Complete"))
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            onTriggered: App.setDoneMany(taskMenu.targetIds, taskMenu.multi ? true : !taskMenu.task.done)
        }
        QQC2.MenuItem {
            visible: taskMenu.multi
            icon.name: "edit-undo"
            text: i18ncp("@action:inmenu", "Reopen (%1)", "Reopen (%1)", taskMenu.selCount)
            onTriggered: App.setDoneMany(taskMenu.targetIds, false)
        }
        QQC2.MenuItem {
            visible: !taskMenu.multi
            icon.name: "media-playback-start"
            text: i18nc("@action:inmenu", "Start Pomodoro Focus")
            enabled: !taskMenu.task.done
            onTriggered: App.focus.startPomodoro(taskMenu.task.id)
        }
        QQC2.MenuItem {
            visible: !taskMenu.multi
            icon.name: "chronometer-lap"
            text: i18nc("@action:inmenu", "Start Execution Timer")
            enabled: !taskMenu.task.done
            onTriggered: App.focus.startStopwatch(taskMenu.task.id)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            icon.name: "view-calendar-day"
            text: i18nc("@action:inmenu set due date", "Due Today")
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            onTriggered: App.postponeMany(taskMenu.targetIds, 0)
        }
        QQC2.MenuItem {
            icon.name: "go-next"
            text: i18nc("@action:inmenu move due date", "Postpone One Day")
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            onTriggered: App.postponeMany(taskMenu.targetIds, 1)
        }
        QQC2.MenuItem {
            icon.name: "view-calendar-week"
            text: i18nc("@action:inmenu move due date", "Postpone One Week")
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            onTriggered: App.postponeMany(taskMenu.targetIds, 7)
        }
        QQC2.Menu {
            title: i18nc("@title:menu", "Priority")
            icon.name: "flag"
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            Repeater {
                model: [ i18nc("priority", "None"), i18nc("priority", "Low"), i18nc("priority", "Medium"), i18nc("priority", "High") ]
                QQC2.MenuItem {
                    required property int index
                    required property string modelData
                    text: modelData
                    checkable: !taskMenu.multi
                    checked: !taskMenu.multi && taskMenu.task.priority === index
                    onTriggered: App.updateTasks(taskMenu.targetIds, { priority: index })
                }
            }
        }
        QQC2.Menu {
            title: i18nc("@title:menu", "Eisenhower Matrix")
            icon.name: "view-grid"
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            Repeater {
                model: [
                    { q: 1, label: i18nc("quadrant menu", "Q1 · Do Now (High)") },
                    { q: 2, label: i18nc("quadrant menu", "Q2 · Schedule (Medium)") },
                    { q: 3, label: i18nc("quadrant menu", "Q3 · Delegate (Low)") },
                    { q: 4, label: i18nc("quadrant menu", "Q4 · Eliminate (None)") }
                ]
                QQC2.MenuItem {
                    required property var modelData
                    text: modelData.label
                    checkable: !taskMenu.multi
                    checked: !taskMenu.multi && taskMenu.task.quadrant === modelData.q
                    onTriggered: App.updateTasks(taskMenu.targetIds, { quadrant: modelData.q })
                }
            }
        }
        QQC2.Menu {
            id: moveMenu
            title: i18nc("@title:menu", "Move to List")
            icon.name: "folder-symbolic"
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            Instantiator {
                model: taskMenu.visible ? App.lists() : []
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.account !== "" ? i18nc("list name (account name)", "%1 (%2)", modelData.name, modelData.account) : modelData.name
                    enabled: !modelData.readOnly && (taskMenu.multi || modelData.id !== taskMenu.task.listId)
                    onTriggered: App.moveTasks(taskMenu.targetIds, modelData.id)
                }
                onObjectAdded: (index, object) => moveMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => moveMenu.removeItem(object)
            }
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            icon.name: "edit-copy"
            text: taskMenu.multi ? i18ncp("@action:inmenu", "Copy Details (%1)", "Copy Details (%1)", taskMenu.selCount)
                                 : i18nc("@action:inmenu", "Copy Details")
            onTriggered: App.copyTasksDetails(taskMenu.targetIds)
        }
        QQC2.MenuItem {
            icon.name: "mail-send"
            text: taskMenu.multi ? i18ncp("@action:inmenu", "Send by Email (%1)…", "Send by Email (%1)…", taskMenu.selCount)
                                 : i18nc("@action:inmenu", "Send by Email…")
            onTriggered: App.emailTasksDetails(taskMenu.targetIds)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            icon.name: "edit-delete"
            text: taskMenu.multi ? i18ncp("@action:inmenu", "Delete (%1)", "Delete (%1)", taskMenu.selCount)
                                 : i18nc("@action:inmenu", "Delete")
            enabled: taskMenu.multi || !taskMenu.task.readOnly
            onTriggered: {
                const ids = taskMenu.targetIds.slice();
                page.clearSelection();
                App.deleteTasks(ids);
            }
        }
    }
}
