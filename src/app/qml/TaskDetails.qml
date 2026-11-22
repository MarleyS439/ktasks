// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.dateandtime as DateTime
import app.ktasks.backend

// Editor for one task. Every change is saved immediately (and can be undone).
Item {
    id: root

    property var taskId: 0
    signal closeRequested()
    signal taskRequested(var taskId)

    // Re-read whenever anything in the store changes.
    readonly property var t: {
        App.revision;
        return App.task(root.taskId);
    }
    readonly property bool editable: !!t.exists && !t.readOnly

    // Text being typed must not be replaced by a refresh, and must not be
    // lost when another task is selected: remember which task it belongs to.
    property var notesTaskId: 0
    function flushNotes() {
        notesTimer.stop();
        if (notesTaskId > 0 && notes.text !== (App.task(notesTaskId).notes ?? "")) {
            App.updateTask(notesTaskId, { notes: notes.text });
        }
    }
    function load() {
        if (!t.exists) {
            return;
        }
        if (notesTaskId !== t.id) {
            flushNotes();
            notesTaskId = t.id;
            titleField.text = t.title;
            notes.text = t.notes;
            notes.viewMode = 0;
            return;
        }
        if (!titleField.activeFocus) {
            titleField.text = t.title;
        }
        if (!notes.editor.activeFocus && !notesTimer.running) {
            notes.text = t.notes;
        }
    }
    onTChanged: load()
    Component.onCompleted: load()
    Component.onDestruction: flushNotes()
    onVisibleChanged: {
        if (!visible) {
            flushNotes();
        }
    }

    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    Rectangle {
        anchors.fill: parent
        color: Kirigami.Theme.backgroundColor
    }

    DateTime.DatePopup {
        id: duePopup
        onAccepted: App.setDueDate(root.taskId, value)
    }
    DateTime.TimePopup {
        id: timePopup
        onAccepted: App.setDueTime(root.taskId, value)
    }
    DateTime.DatePopup {
        id: startPopup
        onAccepted: App.setStartDate(root.taskId, value)
    }

    QQC2.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Kirigami.Units.largeSpacing

            // --- title ---
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                Layout.bottomMargin: 0
                spacing: Kirigami.Units.smallSpacing

                QQC2.CheckBox {
                    checked: !!root.t.done
                    enabled: root.editable
                    Accessible.name: i18nc("@option:check", "Completed")
                    onToggled: App.setDone(root.taskId, checked)
                }
                QQC2.TextField {
                    id: titleField
                    Layout.fillWidth: true
                    readOnly: !root.editable
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                    font.strikeout: !!root.t.done
                    placeholderText: i18nc("@info:placeholder", "Title")
                    Accessible.name: i18nc("@label", "Title")
                    onEditingFinished: {
                        if (text.trim() !== "" && text.trim() !== root.t.title) {
                            App.updateTask(root.taskId, { title: text });
                        } else {
                            text = root.t.title ?? "";
                        }
                    }
                }
                QQC2.ToolButton {
                    icon.name: "edit-copy"
                    Accessible.name: i18nc("@action:button", "Copy details")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: App.copyTaskDetails(root.taskId)
                }
                QQC2.ToolButton {
                    icon.name: "mail-send"
                    Accessible.name: i18nc("@action:button", "Send by email…")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: App.emailTaskDetails(root.taskId)
                }
                QQC2.ToolButton {
                    icon.name: "dialog-close"
                    Accessible.name: i18nc("@action:button", "Close details")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: root.closeRequested()
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: (root.t.parentId ?? 0) > 0
                text: i18nc("@info %1 is the title of the parent task", "Subtask of <a href=\"#\">%1</a>", (root.t.parentTitle ?? "").replace(/&/g, "&amp;").replace(/</g, "&lt;"))
                textFormat: Text.StyledText
                elide: Text.ElideRight
                font: Kirigami.Theme.smallFont
                onLinkActivated: root.taskRequested(root.t.parentId)
                HoverHandler { cursorShape: Qt.PointingHandCursor }
            }

            Kirigami.FormLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                enabled: root.editable

                // --- list ---
                QQC2.ComboBox {
                    id: listCombo
                    Kirigami.FormData.label: i18nc("@label:listbox", "List:")
                    Layout.fillWidth: true
                    readonly property var entries: { App.revision; return App.lists(); }
                    model: entries
                    textRole: "name"
                    valueRole: "id"
                    currentIndex: {
                        for (let i = 0; i < entries.length; ++i) {
                            if (entries[i].id === root.t.listId) {
                                return i;
                            }
                        }
                        return -1;
                    }
                    onActivated: index => {
                        if (entries[index].id !== root.t.listId) {
                            App.moveTask(root.taskId, entries[index].id);
                        }
                    }
                }

                // --- due ---
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Due:")
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        Layout.fillWidth: true
                        icon.name: "view-calendar-day"
                        text: root.t.hasDue ? root.t.dueDateText : i18nc("@action:button", "Set date")
                        Kirigami.Theme.textColor: root.t.overdue && !root.t.done ? Kirigami.Theme.negativeTextColor : undefined
                        onClicked: {
                            duePopup.value = root.t.hasDue ? root.t.due : new Date();
                            duePopup.open();
                        }
                    }
                    QQC2.ToolButton {
                        visible: !!root.t.hasDue
                        icon.name: "edit-clear"
                        Accessible.name: i18nc("@action:button", "Remove due date")
                        QQC2.ToolTip.text: Accessible.name
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: App.clearDue(root.taskId)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Button {
                        flat: true
                        text: i18nc("@action:button due date shortcut", "Today")
                        onClicked: App.postpone(root.taskId, 0)
                    }
                    QQC2.Button {
                        flat: true
                        text: i18nc("@action:button due date shortcut", "+1 day")
                        onClicked: App.postpone(root.taskId, 1)
                    }
                    QQC2.Button {
                        flat: true
                        text: i18nc("@action:button due date shortcut", "+1 week")
                        onClicked: App.postpone(root.taskId, 7)
                    }
                }
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Time:")
                    Layout.fillWidth: true
                    visible: !!root.t.hasDue
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        Layout.fillWidth: true
                        icon.name: "clock"
                        text: root.t.allDay ? i18nc("@action:button", "All day") : root.t.dueTimeText ?? ""
                        onClicked: {
                            let d = new Date();
                            if (!root.t.allDay) {
                                d = root.t.due;
                            } else {
                                d.setHours(9, 0, 0, 0);
                            }
                            timePopup.value = d;
                            timePopup.open();
                        }
                    }
                    QQC2.ToolButton {
                        visible: !root.t.allDay
                        icon.name: "edit-clear"
                        Accessible.name: i18nc("@action:button", "Remove time")
                        QQC2.ToolTip.text: Accessible.name
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: App.clearDueTime(root.taskId)
                    }
                }

                // --- start ---
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Start:")
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        Layout.fillWidth: true
                        icon.name: "media-playback-start"
                        text: root.t.hasStart ? root.t.startDateText : i18nc("@action:button", "Set date")
                        onClicked: {
                            startPopup.value = root.t.hasStart ? root.t.start : new Date();
                            startPopup.open();
                        }
                    }
                    QQC2.ToolButton {
                        visible: !!root.t.hasStart && (root.t.rrule ?? "") === ""
                        icon.name: "edit-clear"
                        Accessible.name: i18nc("@action:button", "Remove start date")
                        QQC2.ToolTip.text: Accessible.name
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: App.clearStart(root.taskId)
                    }
                }

                // --- repeat ---
                ColumnLayout {
                    Kirigami.FormData.label: i18nc("@label:listbox", "Repeat:")
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    readonly property var rparts: root.t.recurrenceParts ?? ({ valid: true, freq: "", interval: 1, byDays: [], endMode: "never", count: 5, untilIso: "" })
                    readonly property bool hasRule: (root.t.rrule ?? "") !== ""
                    readonly property bool editableRule: !hasRule || !!rparts.valid

                    function applyParts(patch) {
                        const next = {
                            freq: patch.freq !== undefined ? patch.freq : (rparts.freq ?? ""),
                            interval: patch.interval !== undefined ? patch.interval : (rparts.interval ?? 1),
                            byDays: patch.byDays !== undefined ? patch.byDays : (rparts.byDays ?? []),
                            endMode: patch.endMode !== undefined ? patch.endMode : (rparts.endMode ?? "never"),
                            count: patch.count !== undefined ? patch.count : (rparts.count ?? 5),
                            untilIso: patch.untilIso !== undefined ? patch.untilIso : (rparts.untilIso ?? "")
                        };
                        App.setRecurrenceRule(root.taskId, next);
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.ComboBox {
                            id: repeatCombo
                            Layout.fillWidth: true
                            readonly property var freqs: ["", "DAILY", "WEEKLY", "MONTHLY", "YEARLY"]
                            model: {
                                const items = [
                                    i18nc("recurrence", "Does not repeat"),
                                    i18nc("recurrence", "Daily"),
                                    i18nc("recurrence", "Weekly"),
                                    i18nc("recurrence", "Monthly"),
                                    i18nc("recurrence", "Yearly")
                                ];
                                if (!parent.parent.editableRule) {
                                    items.push(i18nc("recurrence set by another application", "Custom"));
                                }
                                return items;
                            }
                            currentIndex: !parent.parent.editableRule ? 5 : Math.max(0, freqs.indexOf(parent.parent.rparts.freq ?? ""))
                            onActivated: index => {
                                if (index < 5) {
                                    const f = freqs[index];
                                    const days = f === "WEEKLY" ? (parent.parent.rparts.byDays ?? []) : [];
                                    parent.parent.applyParts({ freq: f, byDays: days });
                                }
                            }
                        }

                        PaddedSpinBox {
                            id: everySpin
                            minWidthUnits: 7
                            visible: parent.parent.editableRule && (parent.parent.rparts.freq ?? "") !== ""
                            from: 1
                            to: 99
                            value: parent.parent.rparts.interval ?? 1
                            textFromValue: v => i18ncp("recurrence interval, e.g. every 2 (weeks)", "every %1", "every %1", v)
                            valueFromText: text => parseInt(text.replace(/\D/g, "")) || 1
                            onValueModified: parent.parent.applyParts({ interval: value })
                        }
                    }

                    // Weekday picker when Weekly
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        visible: parent.editableRule && (parent.rparts.freq ?? "") === "WEEKLY"

                        Repeater {
                            model: [
                                { code: "MO", label: i18nc("weekday short", "Mon") },
                                { code: "TU", label: i18nc("weekday short", "Tue") },
                                { code: "WE", label: i18nc("weekday short", "Wed") },
                                { code: "TH", label: i18nc("weekday short", "Thu") },
                                { code: "FR", label: i18nc("weekday short", "Fri") },
                                { code: "SA", label: i18nc("weekday short", "Sat") },
                                { code: "SU", label: i18nc("weekday short", "Sun") }
                            ]
                            QQC2.ToolButton {
                                required property var modelData
                                readonly property var col: parent.parent
                                readonly property bool on: (col.rparts.byDays ?? []).includes(modelData.code)
                                checkable: true
                                checked: on
                                text: modelData.label
                                font: Kirigami.Theme.smallFont
                                onClicked: {
                                    const cur = (col.rparts.byDays ?? []).slice();
                                    const idx = cur.indexOf(modelData.code);
                                    if (idx >= 0) {
                                        cur.splice(idx, 1);
                                    } else {
                                        cur.push(modelData.code);
                                    }
                                    col.applyParts({ byDays: cur });
                                }
                            }
                        }
                    }

                    // Repetition limit: Forever / N times / Until date
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        visible: parent.editableRule && (parent.rparts.freq ?? "") !== ""

                        QQC2.ComboBox {
                            id: endModeCombo
                            readonly property var modes: ["never", "count", "until"]
                            model: [
                                i18nc("recurrence end", "Forever"),
                                i18nc("recurrence end", "Fixed number of times"),
                                i18nc("recurrence end", "Until date")
                            ]
                            currentIndex: Math.max(0, modes.indexOf(parent.parent.rparts.endMode ?? "never"))
                            onActivated: index => {
                                const m = modes[index];
                                let until = parent.parent.rparts.untilIso ?? "";
                                if (m === "until" && until === "") {
                                    const d = new Date();
                                    d.setMonth(d.getMonth() + 1);
                                    until = Qt.formatDate(d, "yyyy-MM-dd");
                                }
                                parent.parent.applyParts({ endMode: m, untilIso: until });
                            }
                        }

                        PaddedSpinBox {
                            minWidthUnits: 7
                            visible: (parent.parent.rparts.endMode ?? "never") === "count"
                            from: 1
                            to: 999
                            value: parent.parent.rparts.count ?? 5
                            textFromValue: v => i18ncp("recurrence count", "%1 time", "%1 times", v)
                            valueFromText: text => parseInt(text.replace(/\D/g, "")) || 1
                            onValueModified: parent.parent.applyParts({ count: value })
                        }

                        QQC2.Button {
                            visible: (parent.parent.rparts.endMode ?? "never") === "until"
                            icon.name: "view-calendar-day"
                            text: (parent.parent.rparts.untilIso ?? "") !== "" ? parent.parent.rparts.untilIso : i18nc("@action:button", "Pick date")
                            onClicked: {
                                const iso = parent.parent.rparts.untilIso ?? "";
                                untilPopup.value = iso !== "" ? new Date(iso + "T00:00:00") : new Date();
                                untilPopup.open();
                            }
                        }
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        visible: parent.hasRule && (root.t.recurrenceText ?? "") !== ""
                        text: root.t.recurrenceText ?? ""
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        wrapMode: Text.Wrap
                    }

                    DateTime.DatePopup {
                        id: untilPopup
                        onAccepted: parent.applyParts({ endMode: "until", untilIso: Qt.formatDate(value, "yyyy-MM-dd") })
                    }
                }

                // --- reminders ---
                Flow {
                    Kirigami.FormData.label: i18nc("@label", "Reminders:")
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: root.t.reminders ?? []
                        Kirigami.Chip {
                            required property int index
                            required property string modelData
                            text: modelData
                            icon.name: "notifications"
                            checkable: false
                            onRemoved: App.removeReminder(root.taskId, index)
                        }
                    }
                    QQC2.ToolButton {
                        icon.name: "list-add"
                        text: (root.t.reminders ?? []).length === 0 ? i18nc("@action:button", "Add reminder") : ""
                        Accessible.name: i18nc("@action:button", "Add reminder")
                        onClicked: reminderMenu.popup(this, 0, height)

                        QQC2.Menu {
                            id: reminderMenu
                            Repeater {
                                model: [
                                    { minutes: 0, label: i18nc("reminder", "When it is due") },
                                    { minutes: 5, label: i18nc("reminder", "5 minutes before") },
                                    { minutes: 15, label: i18nc("reminder", "15 minutes before") },
                                    { minutes: 30, label: i18nc("reminder", "30 minutes before") },
                                    { minutes: 60, label: i18nc("reminder", "1 hour before") },
                                    { minutes: 120, label: i18nc("reminder", "2 hours before") },
                                    { minutes: 1440, label: i18nc("reminder", "1 day before") },
                                    { minutes: 2880, label: i18nc("reminder", "2 days before") },
                                    { minutes: 10080, label: i18nc("reminder", "1 week before") }
                                ]
                                QQC2.MenuItem {
                                    required property var modelData
                                    text: modelData.label
                                    onTriggered: App.addReminder(root.taskId, modelData.minutes)
                                }
                            }
                        }
                    }
                }

                // --- priority ---
                QQC2.ComboBox {
                    Kirigami.FormData.label: i18nc("@label:listbox", "Priority:")
                    Layout.fillWidth: true
                    model: [ i18nc("priority", "None"), i18nc("priority", "Low"), i18nc("priority", "Medium"), i18nc("priority", "High") ]
                    currentIndex: root.t.priority ?? 0
                    onActivated: index => App.updateTask(root.taskId, { priority: index })
                }

                // --- execution time & pomodoro ---
                ColumnLayout {
                    Kirigami.FormData.label: i18nc("@label", "Execution:")
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        PaddedSpinBox {
                            id: estSpin
                            minWidthUnits: 7
                            from: 0
                            to: 1440
                            stepSize: 5
                            value: root.t.estimatedMinutes ?? 0
                            textFromValue: v => v <= 0 ? i18nc("estimate", "No estimate") : i18nc("minutes est", "%1 min est.", v)
                            valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                            onValueModified: App.updateTask(root.taskId, { estimatedMinutes: value })
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                            elide: Text.ElideRight
                            text: {
                                const tracked = root.t.trackedText ?? "0 min";
                                const poms = root.t.pomodorosCompleted ?? 0;
                                return poms > 0
                                    ? i18nc("execution stats", "Spent: %1 · 🍅 ×%2", tracked, poms)
                                    : i18nc("execution stats", "Spent: %1", tracked);
                            }
                        }
                    }

                    QQC2.ProgressBar {
                        visible: (root.t.estimatedMinutes ?? 0) > 0
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, (root.t.estimatedMinutes ?? 0) * 60)
                        value: Math.min(to, root.t.trackedSeconds ?? 0)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.Button {
                            icon.name: "media-playback-start"
                            text: i18nc("@action:button", "Pomodoro")
                            enabled: !(root.t.done ?? false)
                            onClicked: App.focus.startPomodoro(root.taskId)
                        }
                        QQC2.Button {
                            icon.name: "chronometer-lap"
                            text: i18nc("@action:button", "Timer")
                            enabled: !(root.t.done ?? false)
                            onClicked: App.focus.startStopwatch(root.taskId)
                        }
                        QQC2.ToolButton {
                            icon.name: "list-add"
                            text: "+15m"
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Log 15 minutes manually")
                            QQC2.ToolTip.visible: hovered
                            onClicked: App.focus.addManualMinutes(root.taskId, 15)
                        }
                    }
                }

                // --- tags ---
                Flow {
                    Kirigami.FormData.label: i18nc("@label", "Tags:")
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: root.t.tags ?? []
                        Kirigami.Chip {
                            required property string modelData
                            text: modelData
                            checkable: false
                            onRemoved: App.removeTag(root.taskId, modelData)
                        }
                    }
                    QQC2.TextField {
                        id: tagField
                        width: Kirigami.Units.gridUnit * 7
                        placeholderText: i18nc("@info:placeholder", "Add tag…")
                        onAccepted: {
                            App.addTag(root.taskId, text);
                            text = "";
                        }
                    }
                    QQC2.ToolButton {
                        id: tagButton
                        readonly property var suggestions: {
                            App.revision;
                            const mine = root.t.tags ?? [];
                            return App.allTags().filter(tag => !mine.includes(tag));
                        }
                        visible: suggestions.length > 0
                        icon.name: "tag"
                        Accessible.name: i18nc("@action:button", "Choose an existing tag")
                        QQC2.ToolTip.text: Accessible.name
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: tagMenu.popup(this, 0, height)

                        QQC2.Menu {
                            id: tagMenu
                            Repeater {
                                model: tagButton.suggestions
                                QQC2.MenuItem {
                                    required property string modelData
                                    text: modelData
                                    onTriggered: App.addTag(root.taskId, modelData)
                                }
                            }
                        }
                    }
                }

                // --- progress ---
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label:slider", "Progress:")
                    Layout.fillWidth: true
                    visible: !root.t.done
                    QQC2.Slider {
                        id: progress
                        Layout.fillWidth: true
                        from: 0
                        to: 90
                        stepSize: 10
                        snapMode: QQC2.Slider.SnapAlways
                        value: root.t.percent ?? 0
                        onMoved: progressTimer.restart()
                        Timer {
                            id: progressTimer
                            interval: 400
                            onTriggered: App.updateTask(root.taskId, { percent: progress.value })
                        }
                    }
                    QQC2.Label {
                        text: i18nc("progress in percent", "%1%", Math.round(progress.value))
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                        horizontalAlignment: Text.AlignRight
                    }
                }
            }

            // --- subtasks ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: 0

                Kirigami.Heading {
                    level: 4
                    text: i18nc("@title:group", "Subtasks")
                    Layout.bottomMargin: Kirigami.Units.smallSpacing
                }
                Repeater {
                    model: root.t.subtasks ?? []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.CheckBox {
                            checked: modelData.done
                            enabled: root.editable
                            onToggled: App.setDone(modelData.id, checked)
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: modelData.title
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.strikeout: modelData.done
                            opacity: modelData.done ? 0.6 : 1
                            TapHandler { onTapped: root.taskRequested(modelData.id) }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                        QQC2.ToolButton {
                            icon.name: "go-next"
                            Accessible.name: i18nc("@action:button", "Open subtask")
                            onClicked: root.taskRequested(modelData.id)
                        }
                    }
                }
                QQC2.TextField {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                    enabled: root.editable
                    placeholderText: i18nc("@info:placeholder", "Add subtask…")
                    onAccepted: {
                        if (text.trim() !== "") {
                            App.quickAdd(text, 0, root.taskId);
                            text = "";
                        }
                    }
                }
            }

            // --- notes ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true
                    Kirigami.Heading {
                        level: 4
                        text: i18nc("@title:group", "Notes")
                        Layout.fillWidth: true
                    }
                    QQC2.ToolButton {
                        icon.name: "view-fullscreen"
                        text: i18nc("@action:button", "Expand")
                        onClicked: {
                            expandedNotesEditor.text = notes.text;
                            expandedNotesEditor.viewMode = notes.viewMode;
                            expandedNotesDialog.open();
                            if (expandedNotesEditor.viewMode !== 1) {
                                expandedNotesEditor.editor.forceActiveFocus();
                            }
                        }
                    }
                }
                MarkdownEditor {
                    id: notes
                    Layout.fillWidth: true
                    Layout.minimumHeight: Kirigami.Units.gridUnit * 16
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 18
                    compact: true
                    readOnly: !root.editable
                    placeholderText: i18nc("@info:placeholder", "Notes (Markdown supported)")
                    onContentEdited: notesTimer.restart()
                    onFocusLost: root.flushNotes()
                    Timer {
                        id: notesTimer
                        interval: 1500
                        onTriggered: root.flushNotes()
                    }
                }
            }

            Kirigami.Dialog {
                id: expandedNotesDialog
                title: root.t.title ?? i18nc("@title:window", "Notes")
                padding: Kirigami.Units.largeSpacing
                preferredWidth: Math.max(Kirigami.Units.gridUnit * 38, applicationWindow() ? applicationWindow().width * 0.72 : 720)
                preferredHeight: Math.max(Kirigami.Units.gridUnit * 26, applicationWindow() ? applicationWindow().height * 0.72 : 520)
                standardButtons: Kirigami.Dialog.Close
                onClosed: {
                    if (root.editable && expandedNotesEditor.text !== notes.text) {
                        notes.text = expandedNotesEditor.text;
                        root.flushNotes();
                    }
                }

                MarkdownEditor {
                    id: expandedNotesEditor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    readOnly: !root.editable
                    placeholderText: i18nc("@info:placeholder", "Notes (Markdown supported)")
                }
            }

            // --- footer ---
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Separator {
                    Layout.fillWidth: true
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    text: {
                        const lines = [ i18nc("@info", "Created %1 · Changed %2", root.t.createdText ?? "", root.t.modifiedText ?? "") ];
                        if ((root.t.completedText ?? "") !== "") {
                            lines.push(i18nc("@info", "Completed %1", root.t.completedText));
                        }
                        if (root.t.pendingSync) {
                            lines.push(i18nc("@info", "Waiting to be synchronized"));
                        }
                        if (root.t.readOnly) {
                            lines.push(i18nc("@info", "Read-only list"));
                        }
                        return lines.join("\n");
                    }
                }
                QQC2.Button {
                    icon.name: "edit-delete"
                    text: i18nc("@action:button", "Delete Task")
                    enabled: root.editable
                    onClicked: {
                        const id = root.taskId;
                        root.closeRequested();
                        App.deleteTask(id);
                    }
                }
            }
        }
    }
}
