// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.statefulapp as StatefulApp
import app.ktasks.backend

StatefulApp.StatefulWindow {
    id: root

    property var currentTaskId: 0
    property string currentSection: "summary" // "summary", "tasks", "matrix", "insights", "routine", "journal"
    property var savedJournalEntryId: 0
    property bool savedJournalEditMode: false
    property int savedRoutineCalendarMode: 0
    // Room for the list and the details side by side?
    readonly property bool wide: width >= Kirigami.Units.gridUnit * 56
    property var detailsLayer: null
    property var settingsLayer: null

    Binding {
        target: App.summary
        property: "active"
        value: root.currentSection === "summary"
    }
    Binding {
        target: App.eisenhower
        property: "active"
        value: root.currentSection === "matrix"
    }
    Binding {
        target: App.analytics
        property: "active"
        value: root.currentSection === "insights"
    }

    Timer {
        id: memoryTrimTimer
        interval: 450
        repeat: false
        onTriggered: App.trimMemory(root)
    }

    function switchSection(section) {
        while (pageStack.layers.depth > 1) {
            pageStack.layers.pop();
        }
        if (currentSection !== section) {
            memoryTrimTimer.restart();
        }
        if (section !== "tasks") {
            root.selectTask(0);
        }
        currentSection = section;
    }

    function selectTask(taskId) {
        currentTaskId = taskId;
        updateDetailsLayer();
    }
    function updateDetailsLayer() {
        const want = !wide && currentTaskId > 0;
        if (want && !detailsLayer) {
            detailsLayer = pageStack.layers.push(detailsPage);
        } else if (!want && detailsLayer) {
            if (pageStack.layers.currentItem === detailsLayer) {
                pageStack.layers.pop();
            }
            detailsLayer = null;
        }
    }
    function openSettings() {
        if (!settingsLayer) {
            settingsLayer = pageStack.layers.push(settingsPage);
        }
    }
    function openListDialog(id) {
        listDialogLoader.active = true;
        listDialogLoader.item.openFor(id);
    }
    function openAccountDialog() {
        accountDialogLoader.active = true;
        accountDialogLoader.item.open();
    }
    function openPrintDialog(scope, jId) {
        printDialogLoader.active = true;
        printDialogLoader.item.openFor(scope, jId);
    }
    function openJournalEntry(entryId, edit) {
        root.savedJournalEntryId = entryId;
        root.savedJournalEditMode = !!edit;
        root.switchSection("journal");
        if (journalLoader.item && entryId > 0) {
            Qt.callLater(() => {
                if (!journalLoader.item) return;
                if (edit) journalLoader.item.openEntryForEditing(entryId);
                else journalLoader.item.openEntryReadOnly(entryId);
            });
        }
    }
    onWideChanged: updateDetailsLayer()

    windowName: "Main"
    application: App
    title: App.tasks.title
    visible: false
    minimumWidth: Kirigami.Units.gridUnit * 20
    minimumHeight: Kirigami.Units.gridUnit * 22
    width: Kirigami.Units.gridUnit * 64
    height: Kirigami.Units.gridUnit * 40

    onClosing: {
        if (!App.settings.runInBackground) {
            App.quit();
        }
    }

    // Configurable shortcuts: one Shortcut per action of the collection.
    Instantiator {
        model: App.shortcutActions
        delegate: Shortcut {
            required property string modelData
            sequence: App.shortcutRevision >= 0 ? App.shortcutOf(modelData) : ""
            context: Qt.ApplicationShortcut
            onActivated: App.trigger(modelData)
        }
    }

    globalDrawer: Sidebar {
        currentSection: root.currentSection
        onSectionSelected: section => root.switchSection(section)
        onNewListRequested: root.openListDialog(0)
        onEditListRequested: listId => root.openListDialog(listId)
        onSettingsRequested: root.openSettings()
        onPrintRequested: root.openPrintDialog(
            root.currentSection === "matrix" ? "matrix"
            : root.currentSection === "routine" ? "routine"
            : (App.tasks.view === "week" ? "week" : "today"), 0)
    }

    pageStack.initialPage: Kirigami.Page {
        id: workspacePage
        padding: 0
        property var searchFieldRef: null
        title: root.currentSection === "summary" && summaryLoader.item ? summaryLoader.item.pageTitle
             : root.currentSection === "tasks" && tasksLoader.item ? tasksLoader.item.pageTitle
             : root.currentSection === "matrix" && matrixLoader.item ? matrixLoader.item.pageTitle
             : root.currentSection === "insights" && insightsLoader.item ? insightsLoader.item.pageTitle
             : root.currentSection === "routine" && routineLoader.item ? routineLoader.item.pageTitle
             : root.currentSection === "journal" && journalLoader.item ? journalLoader.item.pageTitle
             : i18n("KTasks")
        titleDelegate: RowLayout {
            spacing: Kirigami.Units.smallSpacing
            Layout.fillWidth: true

            Kirigami.Heading {
                level: 1
                text: workspacePage.title
                elide: Text.ElideRight
                textFormat: Text.PlainText
                Layout.maximumWidth: root.currentSection === "tasks" ? Kirigami.Units.gridUnit * 14 : Kirigami.Units.gridUnit * 28
            }
            QQC2.ToolButton {
                id: summaryHelpBtn
                visible: root.currentSection === "summary"
                flat: true
                hoverEnabled: true
                icon.name: "help-about-symbolic"
                icon.color: Kirigami.Theme.textColor
                Accessible.name: i18nc("@action:button", "About Summary")
                onHoveredChanged: summaryHelpPopup.syncVisibility()
                onClicked: summaryHelpPopup.visible ? summaryHelpPopup.close() : summaryHelpPopup.open()

                Connections {
                    target: root
                    function onCurrentSectionChanged() {
                        summaryHelpPopup.close();
                    }
                }

                QQC2.Popup {
                    id: summaryHelpPopup
                    x: 0
                    y: parent.height + 4
                    width: Math.min(Kirigami.Units.gridUnit * 28, root.width - Kirigami.Units.largeSpacing * 2)
                    padding: Kirigami.Units.largeSpacing
                    margins: Kirigami.Units.smallSpacing
                    modal: false
                    focus: false
                    closePolicy: QQC2.Popup.CloseOnEscape | QQC2.Popup.CloseOnPressOutsideParent

                    function syncVisibility() {
                        if (summaryHelpBtn.hovered || popupHover.hovered) {
                            helpHideTimer.stop();
                            if (!visible) {
                                open();
                            }
                        } else {
                            helpHideTimer.restart();
                        }
                    }

                    Timer {
                        id: helpHideTimer
                        interval: 160
                        onTriggered: {
                            if (!summaryHelpBtn.hovered && !popupHover.hovered) {
                                summaryHelpPopup.close();
                            }
                        }
                    }

                    background: Rectangle {
                        radius: Kirigami.Units.cornerRadius * 1.5
                        color: Kirigami.Theme.backgroundColor
                        border.width: 1
                        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.22)
                    }

                    contentItem: Item {
                        implicitHeight: guideBodyCol.implicitHeight

                        HoverHandler {
                            id: popupHover
                            onHoveredChanged: summaryHelpPopup.syncVisibility()
                        }

                        ColumnLayout {
                            id: guideBodyCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Kirigami.Icon {
                                    source: "help-about-symbolic"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: Kirigami.Theme.textColor
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    font.bold: true
                                    text: i18nc("@title", "How the Summary Screen Works")
                                    wrapMode: Text.Wrap
                                }
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                font: Kirigami.Theme.smallFont
                                wrapMode: Text.Wrap
                                text: i18nc("summary guide body", "• Overview & KPIs: daily progress combines tasks finished today and your routine blocks. Click any KPI card to jump straight to that view.\n• Weather: updates automatically every hour with a 10-day scrollable strip. Configure your city inside Settings.\n• Priorities & Journal: finish top urgent/important tasks, launch Pomodoro execution, or open today's Markdown journal page.\n• Routine vs. External Agenda: Routine tracks your recurring daily habits (synced optionally with Nextcloud). External Agenda displays read-only appointments from KOrganizer (Google Calendar, Birthdays, local .ics files), highlights schedule conflicts, and lets you create a prep task or start Pomodoro for an appointment.")
                            }
                        }
                    }
                }
            }
            Kirigami.SearchField {
                id: topTaskSearch
                visible: root.currentSection === "tasks"
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                placeholderText: i18nc("@info:placeholder", "Search tasks…")
                text: App.tasks.searchText
                Component.onCompleted: workspacePage.searchFieldRef = topTaskSearch
                onTextChanged: searchDelay.restart()
                Keys.onEscapePressed: text = ""
                Timer {
                    id: searchDelay
                    interval: 120
                    onTriggered: App.tasks.searchText = topTaskSearch.text
                }
            }
        }
        actions: root.currentSection === "summary" && summaryLoader.item ? summaryLoader.item.pageActions
               : root.currentSection === "tasks" && tasksLoader.item ? tasksLoader.item.pageActions
               : root.currentSection === "matrix" && matrixLoader.item ? matrixLoader.item.pageActions
               : root.currentSection === "insights" && insightsLoader.item ? insightsLoader.item.pageActions
               : root.currentSection === "routine" && routineLoader.item ? routineLoader.item.pageActions
               : root.currentSection === "journal" && journalLoader.item ? journalLoader.item.pageActions
               : []

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Persistent compact Pomodoro / Execution Timer bar (instantiated only while a session is active)
            Loader {
                id: focusBarLoader
                active: App.focus.hasSession
                visible: active
                Layout.fillWidth: true
                Layout.preferredHeight: item ? item.implicitHeight : 0
                sourceComponent: Rectangle {
                    id: focusBar
                    implicitHeight: focusBarCol.implicitHeight
                    color: Qt.alpha(App.focus.mode === 1 ? "#27ae60" : App.focus.mode === 2 ? "#3daee9" : "#e93d58", 0.12)

                    ColumnLayout {
                        id: focusBarCol
                        width: parent.width
                        spacing: 0

                        RowLayout {
                            id: focusBarRow
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: App.focus.mode === 2 ? "chronometer-lap" : "chronometer"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Layout.preferredWidth
                                color: App.focus.mode === 1 ? "#27ae60" : App.focus.mode === 2 ? "#3daee9" : "#e93d58"
                            }

                            QQC2.Label {
                                text: App.focus.timeText
                                font.bold: true
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                                font.family: "monospace"
                            }

                            Chip {
                                text: App.focus.statusLabel
                                filled: true
                                tint: App.focus.mode === 1 ? "#27ae60" : App.focus.mode === 2 ? "#3daee9" : "#e93d58"
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: App.focus.activeTaskTitle
                                elide: Text.ElideRight
                                font.weight: Font.DemiBold
                            }

                            QQC2.Button {
                                flat: true
                                icon.name: App.focus.running ? "media-playback-pause" : "media-playback-start"
                                text: App.focus.running ? i18nc("@action:button", "Pause") : i18nc("@action:button", "Resume")
                                onClicked: App.focus.togglePause()
                            }

                            QQC2.Button {
                                visible: App.focus.mode !== 1
                                flat: true
                                icon.name: "chronometer"
                                text: i18nc("@action:button", "Break")
                                onClicked: App.focus.startBreak(false)
                            }

                            QQC2.Button {
                                visible: App.focus.activeTaskId > 0 && App.focus.mode !== 1
                                flat: true
                                icon.name: "task-complete"
                                text: i18nc("@action:button", "Complete")
                                onClicked: App.focus.completeActiveTask()
                            }

                            QQC2.Button {
                                flat: true
                                icon.name: "media-playback-stop"
                                text: i18nc("@action:button", "Stop & Save")
                                onClicked: App.focus.stopAndSave()
                            }

                            QQC2.ToolButton {
                                icon.name: "window-close"
                                QQC2.ToolTip.text: i18nc("@info:tooltip", "Discard timer without saving")
                                QQC2.ToolTip.visible: hovered
                                onClicked: App.focus.discard()
                            }
                        }

                        Rectangle {
                            visible: App.focus.progress > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: 3
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
                            Rectangle {
                                width: Math.round(parent.width * App.focus.progress)
                                height: parent.height
                                color: App.focus.mode === 1 ? "#27ae60" : "#e93d58"
                            }
                        }
                    }
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: root.currentSection === "summary" ? 0
                            : root.currentSection === "tasks" ? 1
                            : root.currentSection === "matrix" ? 2
                            : root.currentSection === "insights" ? 3
                            : root.currentSection === "routine" ? 4 : 5

            Loader {
                id: summaryLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: root.currentSection === "summary"
                sourceComponent: SummaryPage {
                    onNavigateRequested: sec => root.switchSection(sec)
                    onOpenViewRequested: v => {
                        root.switchSection("tasks");
                        App.tasks.searchText = "";
                        App.tasks.view = v;
                    }
                    onTaskActivated: taskId => {
                        root.switchSection("tasks");
                        App.tasks.view = "all";
                        root.selectTask(taskId);
                        if (tasksLoader.item) {
                            Qt.callLater(() => {
                                if (tasksLoader.item) tasksLoader.item.reveal(taskId);
                            });
                        }
                    }
                    onJournalRequested: (entryId, edit) => root.openJournalEntry(entryId, edit)
                    onSettingsRequested: root.openSettings()
                }
            }
            Loader {
                id: tasksLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: root.currentSection === "tasks"
                sourceComponent: TasksPage {
                    currentTaskId: root.currentTaskId
                    showDetailsPane: root.wide
                    onTaskActivated: taskId => root.selectTask(taskId)
                    onEditListRequested: listId => root.openListDialog(listId)
                }
            }
            Loader {
                id: matrixLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: root.currentSection === "matrix"
                sourceComponent: EisenhowerPage {
                    currentTaskId: root.currentTaskId
                    showDetailsPane: root.wide
                    onTaskActivated: taskId => root.selectTask(taskId)
                    onPrintMatrixRequested: root.openPrintDialog("matrix", 0)
                }
            }
            Loader {
                id: insightsLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: root.currentSection === "insights"
                sourceComponent: InsightsPage {
                    onNavigateRequested: sec => root.switchSection(sec)
                    onTaskActivated: taskId => root.selectTask(taskId)
                }
            }
            Loader {
                id: routineLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: root.currentSection === "routine"
                onLoaded: {
                    if (item) {
                        item.calendarMode = root.savedRoutineCalendarMode;
                    }
                }
                sourceComponent: RoutinePage {
                    onCalendarModeChanged: root.savedRoutineCalendarMode = calendarMode
                    onPrintRoutineRequested: root.openPrintDialog("routine", 0)
                }
            }
            Loader {
                id: journalLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: root.currentSection === "journal"
                onLoaded: {
                    if (item && root.savedJournalEntryId > 0) {
                        if (root.savedJournalEditMode) {
                            item.openEntryForEditing(root.savedJournalEntryId);
                            root.savedJournalEditMode = false;
                        } else {
                            item.openEntryReadOnly(root.savedJournalEntryId);
                        }
                    }
                }
                sourceComponent: JournalPage {
                    onCurrentEntryIdChanged: root.savedJournalEntryId = currentEntryId
                    onPrintEntryRequested: entryId => root.openPrintDialog("journal", entryId)
                }
            }
            }
        }
    }
    pageStack.columnView.columnResizeMode: Kirigami.ColumnView.SingleColumn

    Connections {
        target: pageStack.layers
        function onCurrentItemChanged() {
            // Back button or Esc on a layer.
            if (root.detailsLayer && pageStack.layers.currentItem !== root.detailsLayer) {
                root.detailsLayer = null;
                root.currentTaskId = 0;
            }
            if (root.settingsLayer && pageStack.layers.currentItem !== root.settingsLayer) {
                root.settingsLayer = null;
            }
        }
    }

    Component {
        id: detailsPage
        Kirigami.Page {
            title: i18nc("@title", "Task")
            padding: 0
            TaskDetails {
                anchors.fill: parent
                taskId: root.currentTaskId
                onCloseRequested: root.selectTask(0)
                onTaskRequested: taskId => root.selectTask(taskId)
            }
        }
    }
    Component {
        id: settingsPage
        SettingsPage {
            onAddAccountRequested: root.openAccountDialog()
        }
    }

    Loader {
        id: listDialogLoader
        active: false
        sourceComponent: ListDialog {}
    }
    Loader {
        id: accountDialogLoader
        active: false
        sourceComponent: AccountDialog {}
    }
    Loader {
        id: printDialogLoader
        active: false
        sourceComponent: PrintDialog {}
    }
    Loader {
        id: quickAddWindowLoader
        active: false
        sourceComponent: QuickAddWindow {}
    }

    Dialogs.FileDialog {
        id: importDialog
        title: i18nc("@title:window", "Import Tasks")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [i18nc("file type", "iCalendar files (*.ics *.ical)"), i18nc("file type", "All files (*)")]
        onAccepted: App.importIcs(selectedFile, 0)
    }
    Dialogs.FileDialog {
        id: exportDialog
        title: i18nc("@title:window", "Export Tasks")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "ics"
        nameFilters: [i18nc("file type", "iCalendar files (*.ics)")]
        onAccepted: App.exportIcs(selectedFile, App.tasks.listId)
    }

    Connections {
        target: App
        function onMessage(text, withUndo) {
            if (withUndo) {
                root.showPassiveNotification(text, "long", i18nc("@action:button", "Undo"), () => App.undo());
            } else {
                root.showPassiveNotification(text, "short");
            }
        }
        function onShowTask(taskId) {
            root.switchSection("tasks");
            root.selectTask(taskId);
            if (tasksLoader.item) {
                Qt.callLater(() => {
                    if (tasksLoader.item) tasksLoader.item.reveal(taskId);
                });
            }
        }
        function onShowView(view) {
            root.switchSection("tasks");
            App.tasks.searchText = "";
            App.tasks.view = view;
        }
        function onShowSection(section) {
            root.switchSection(section);
        }
        function onShowSettings() {
            root.openSettings();
        }
        function onFocusQuickAdd() {
            App.showWindow();
            root.switchSection("tasks");
            if (tasksLoader.item) {
                Qt.callLater(() => {
                    if (tasksLoader.item) tasksLoader.item.focusQuickAdd();
                });
            }
        }
        function onFocusSearch() {
            if (root.currentSection === "tasks" && workspacePage.searchFieldRef) {
                workspacePage.searchFieldRef.forceActiveFocus();
                workspacePage.searchFieldRef.selectAll();
            }
        }
        function onPrintRequested() {
            const scope = root.currentSection === "matrix" ? "matrix"
                        : root.currentSection === "routine" ? "routine"
                        : App.tasks.view === "week" ? "week"
                        : App.tasks.view === "overdue" ? "overdue"
                        : "today";
            root.openPrintDialog(scope, 0);
        }
        function onQuickAddWindowRequested() {
            quickAddWindowLoader.active = true;
            quickAddWindowLoader.item.present();
        }
        function onNewListRequested() {
            root.openListDialog(0);
        }
        function onCompleteCurrentRequested() {
            if (root.currentSection === "tasks" && tasksLoader.item && tasksLoader.item.selectedIds.length > 1) {
                const ref = App.task(tasksLoader.item.selectedIds[0]);
                App.setDoneMany(tasksLoader.item.selectedIds, !ref.done);
            } else if (root.currentTaskId > 0) {
                App.setDone(root.currentTaskId, !App.task(root.currentTaskId).done);
            }
        }
        function onDeleteCurrentRequested() {
            if (!root.settingsLayer) {
                if (root.currentSection === "tasks" && tasksLoader.item && tasksLoader.item.selectedIds.length > 1) {
                    const ids = tasksLoader.item.selectedIds.slice();
                    tasksLoader.item.clearSelection();
                    root.selectTask(0);
                    App.deleteTasks(ids);
                } else if (root.currentTaskId > 0) {
                    const id = root.currentTaskId;
                    if (tasksLoader.item) {
                        tasksLoader.item.clearSelection();
                    }
                    root.selectTask(0);
                    App.deleteTask(id);
                }
            }
        }
        function onImportRequested() {
            importDialog.open();
        }
        function onExportRequested() {
            exportDialog.open();
        }
    }
    Connections {
        target: App.sync
        function onAccountAdded(name, taskLists) {
            root.showPassiveNotification(i18ncp("@info", "Connected to %2: %1 task list found.", "Connected to %2: %1 task lists found.", taskLists, name), "long");
        }
    }
    Connections {
        target: App.tasks
        function onViewChanged() {
            if (tasksLoader.item) {
                tasksLoader.item.clearSelection();
            }
            root.selectTask(0);
        }
    }
    Connections {
        target: App
        function onRevisionChanged() {
            // The selected task was deleted (here or on another device).
            if (root.currentTaskId > 0 && !App.task(root.currentTaskId).exists) {
                root.selectTask(0);
            }
        }
    }
}
