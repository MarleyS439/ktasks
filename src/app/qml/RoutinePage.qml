// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.dateandtime as DateTime
import app.ktasks.backend

Item {
    id: page

    signal printRoutineRequested()

    // 0 = Day (00:00 -> 00:00 next day), 1 = Week (KOrganizer grid), 2 = Month (KOrganizer grid)
    property int calendarMode: 0
    property int calRevision: 0

    readonly property string pageTitle: i18nc("@title", "Routine & Day Planner")
    property list<Kirigami.Action> pageActions: [
        Kirigami.Action {
            icon.name: "list-add"
            text: i18nc("@action:button", "New Block")
            onTriggered: blockDialog.openNewWithRange(false, 9 * 60, 60)
        },
        Kirigami.Action {
            icon.name: "view-task"
            text: i18nc("@action:button", "Schedule Task")
            onTriggered: scheduleTaskDialog.openPicker()
        },
        Kirigami.Action {
            icon.name: "view-calendar-week"
            text: i18nc("@action:button", "Routine Templates")
            onTriggered: templatesDrawer.open()
        },
        Kirigami.Action {
            icon.name: "view-refresh"
            text: i18nc("@action:button", "Sync Routine")
            visible: App.sync.hasAccounts
            enabled: !App.sync.syncing
            onTriggered: App.sync.syncRoutinesNow()
        },
        Kirigami.Action {
            icon.name: "document-print"
            text: i18nc("@action:button", "Print / PDF…")
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.printRoutineRequested()
        }
    ]

    readonly property int hourHeight: Kirigami.Units.gridUnit * 2.8
    property int currentMinutes: {
        const d = new Date();
        return d.getHours() * 60 + d.getMinutes();
    }

    readonly property var weekData: {
        calRevision;
        App.routine.selectedDate;
        return App.routine.weekDays();
    }
    readonly property var monthData: {
        calRevision;
        App.routine.selectedDate;
        App.revision;
        return App.routine.monthCells();
    }

    Connections {
        target: App.routine
        function onChanged() { page.calRevision++; }
        function onSelectedDateChanged() { page.calRevision++; }
    }

    Timer {
        interval: 60000
        running: page.visible
        repeat: true
        onTriggered: {
            const d = new Date();
            page.currentMinutes = d.getHours() * 60 + d.getMinutes();
        }
    }

    function formatMins(m) {
        const clamped = Math.max(0, Math.min(1440, m));
        if (clamped === 1440) {
            return "00:00";
        }
        const h = Math.floor(clamped / 60) % 24;
        const min = clamped % 60;
        return String(h).padStart(2, "0") + ":" + String(min).padStart(2, "0");
    }

    function snap15FromY(yPos, topPad, hPerHour) {
        const rawMins = ((yPos - topPad) / hPerHour) * 60;
        return Math.max(0, Math.min(1425, Math.round(rawMins / 15) * 15));
    }

    Component.onCompleted: {
        Qt.callLater(() => {
            timelineFlick.contentY = Math.min(Math.max(0, timelineFlick.contentHeight - timelineFlick.height), 7 * page.hourHeight);
            weekFlick.contentY = Math.min(Math.max(0, weekFlick.contentHeight - weekFlick.height), 7 * (Kirigami.Units.gridUnit * 2.1));
        });
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Navigator + Day / Week / Month switcher (KOrganizer style)
        QQC2.ToolBar {
            Layout.fillWidth: true
            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                QQC2.ToolButton {
                    icon.name: "go-previous"
                    Accessible.name: i18nc("@action:button", "Previous")
                    onClicked: {
                        if (page.calendarMode === 0) App.routine.shiftDays(-1);
                        else if (page.calendarMode === 1) App.routine.shiftWeeks(-1);
                        else App.routine.shiftMonths(-1);
                    }
                }
                QQC2.Button {
                    flat: true
                    text: page.calendarMode === 0 ? App.routine.selectedDateText
                        : page.calendarMode === 1 ? App.routine.weekTitle
                        : App.routine.monthTitle
                    font.bold: true
                    onClicked: App.routine.goToday()
                }
                QQC2.ToolButton {
                    icon.name: "go-next"
                    Accessible.name: i18nc("@action:button", "Next")
                    onClicked: {
                        if (page.calendarMode === 0) App.routine.shiftDays(1);
                        else if (page.calendarMode === 1) App.routine.shiftWeeks(1);
                        else App.routine.shiftMonths(1);
                    }
                }
                QQC2.Button {
                    visible: !App.routine.isToday
                    text: i18nc("@action:button", "Today")
                    onClicked: App.routine.goToday()
                }

                Item { Layout.fillWidth: true }

                QQC2.Label {
                    visible: page.calendarMode === 0 && page.width > Kirigami.Units.gridUnit * 40
                    readonly property int freeMins: {
                        App.routine.plannedMinutes;
                        return App.routine.availableMinutesForMinute(page.currentMinutes);
                    }
                    readonly property int plannedH: Math.floor(App.routine.plannedMinutes / 60)
                    readonly property int plannedM: App.routine.plannedMinutes % 60
                    readonly property int freeH: Math.floor(freeMins / 60)
                    readonly property int freeM: freeMins % 60
                    text: App.routine.isPast
                        ? i18nc("@info", "%1/%2 blocks done · %3h %4m planned",
                                App.routine.completedBlocks,
                                App.routine.totalBlocks,
                                plannedH,
                                plannedM)
                        : App.routine.isToday
                            ? i18nc("@info", "%1/%2 blocks done · %3h %4m planned · %5h %6m left today",
                                    App.routine.completedBlocks,
                                    App.routine.totalBlocks,
                                    plannedH,
                                    plannedM,
                                    freeH,
                                    freeM)
                            : i18nc("@info", "%1/%2 blocks done · %3h %4m planned · %5h %6m free",
                                    App.routine.completedBlocks,
                                    App.routine.totalBlocks,
                                    plannedH,
                                    plannedM,
                                    freeH,
                                    freeM)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }

                // View mode selector: Day / Week / Month
                RowLayout {
                    spacing: 2
                    QQC2.ToolButton {
                        checkable: true
                        checked: page.calendarMode === 0
                        icon.name: "view-calendar-day"
                        text: i18nc("@action:button calendar mode", "Day")
                        onClicked: page.calendarMode = 0
                    }
                    QQC2.ToolButton {
                        checkable: true
                        checked: page.calendarMode === 1
                        icon.name: "view-calendar-week"
                        text: i18nc("@action:button calendar mode", "Week")
                        onClicked: page.calendarMode = 1
                    }
                    QQC2.ToolButton {
                        checkable: true
                        checked: page.calendarMode === 2
                        icon.name: "view-calendar-month"
                        text: i18nc("@action:button calendar mode", "Month")
                        onClicked: page.calendarMode = 2
                    }
                }
            }
        }

        Kirigami.Separator { Layout.fillWidth: true }

        // ================== MODE 0: DAY TIMELINE (00:00 -> 00:00 next day) ==================
        Rectangle {
            visible: page.calendarMode === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor

            Flickable {
                id: timelineFlick
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: timelineCanvas.height
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick
                interactive: !dayDragArea.dragging
                QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                Item {
                    id: timelineCanvas
                    width: timelineFlick.width
                    height: 24 * page.hourHeight + Kirigami.Units.largeSpacing * 4

                    readonly property int topPad: Kirigami.Units.largeSpacing * 2
                    readonly property int gutterWidth: Kirigami.Units.gridUnit * 4

                    // Drag-to-select time interval (Google Calendar style)
                    MouseArea {
                        id: dayDragArea
                        x: timelineCanvas.gutterWidth
                        y: timelineCanvas.topPad
                        width: Math.max(100, timelineCanvas.width - timelineCanvas.gutterWidth - Kirigami.Units.largeSpacing)
                        height: 24 * page.hourHeight
                        acceptedButtons: Qt.LeftButton
                        preventStealing: true

                        property bool dragging: false
                        property int anchorMin: 0
                        property int currentMin: 0

                        readonly property int selStart: Math.min(anchorMin, currentMin)
                        readonly property int selEnd: Math.max(anchorMin, currentMin)

                        onPressed: mouse => {
                            const m = page.snap15FromY(mouse.y, 0, page.hourHeight);
                            anchorMin = m;
                            currentMin = Math.min(1440, m + 30);
                            dragging = true;
                        }
                        onPositionChanged: mouse => {
                            if (!dragging) {
                                return;
                            }
                            const m = Math.max(0, Math.min(1440, Math.round((mouse.y / page.hourHeight) * 4) * 15));
                            currentMin = m;
                        }
                        onReleased: {
                            if (!dragging) {
                                return;
                            }
                            dragging = false;
                            const s = Math.min(anchorMin, currentMin);
                            const e = Math.max(anchorMin, currentMin);
                            const dur = Math.max(30, e - s);
                            if (e - s >= 15) {
                                blockDialog.openNewWithRange(false, s, dur);
                            }
                        }
                        onCanceled: dragging = false
                        onDoubleClicked: mouse => {
                            dragging = false;
                            const m = page.snap15FromY(mouse.y, 0, page.hourHeight);
                            blockDialog.openNewWithRange(false, m, 60);
                        }
                    }

                    // 24 hour rows (00:00 .. 23:00)
                    Repeater {
                        model: 24
                        Item {
                            required property int index
                            x: 0
                            y: timelineCanvas.topPad + index * page.hourHeight
                            width: timelineCanvas.width
                            height: page.hourHeight

                            QQC2.Label {
                                x: Kirigami.Units.smallSpacing
                                y: -height / 2
                                width: timelineCanvas.gutterWidth - Kirigami.Units.smallSpacing * 2
                                horizontalAlignment: Text.AlignRight
                                text: String(index).padStart(2, "0") + ":00"
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                            }
                            Rectangle {
                                x: timelineCanvas.gutterWidth
                                y: 0
                                width: parent.width - x - Kirigami.Units.largeSpacing
                                height: 1
                                color: Qt.alpha(Kirigami.Theme.textColor, 0.22)
                            }
                            Rectangle {
                                x: timelineCanvas.gutterWidth
                                y: parent.height / 2
                                width: parent.width - x - Kirigami.Units.largeSpacing
                                height: 1
                                color: Qt.alpha(Kirigami.Theme.textColor, 0.10)
                            }
                        }
                    }

                    // Bottom 00:00 (next day) boundary line
                    Item {
                        x: 0
                        y: timelineCanvas.topPad + 24 * page.hourHeight
                        width: timelineCanvas.width
                        height: Kirigami.Units.largeSpacing
                        QQC2.Label {
                            x: Kirigami.Units.smallSpacing
                            y: -height / 2
                            width: timelineCanvas.gutterWidth - Kirigami.Units.smallSpacing * 2
                            horizontalAlignment: Text.AlignRight
                            text: "00:00"
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                        Rectangle {
                            x: timelineCanvas.gutterWidth
                            y: 0
                            width: parent.width - x - Kirigami.Units.largeSpacing
                            height: 1
                            color: Qt.alpha(Kirigami.Theme.textColor, 0.22)
                        }
                    }

                    // Live drag-selection preview rectangle
                    Rectangle {
                        visible: dayDragArea.dragging && dayDragArea.selEnd > dayDragArea.selStart
                        x: timelineCanvas.gutterWidth + Kirigami.Units.smallSpacing
                        y: timelineCanvas.topPad + (dayDragArea.selStart / 60.0) * page.hourHeight
                        width: Math.max(120, timelineCanvas.width - x - Kirigami.Units.largeSpacing - 4)
                        height: Math.max(20, ((dayDragArea.selEnd - dayDragArea.selStart) / 60.0) * page.hourHeight)
                        radius: Kirigami.Units.cornerRadius
                        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.28)
                        border.width: 2
                        border.color: Kirigami.Theme.highlightColor
                        z: 4

                        QQC2.Label {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: 6
                            readonly property int mins: dayDragArea.selEnd - dayDragArea.selStart
                            text: page.formatMins(dayDragArea.selStart) + " – " + page.formatMins(dayDragArea.selEnd)
                                + " (" + (mins >= 60 ? Math.floor(mins / 60) + "h " + (mins % 60 > 0 ? (mins % 60) + "min" : "") : mins + " min") + ")"
                            font.bold: true
                            color: Kirigami.Theme.textColor
                        }
                    }

                    // Scheduled blocks & routine items
                    Repeater {
                        model: App.routine
                        delegate: Item {
                            id: blockWrapper
                            required property int index
                            required property var blockId
                            required property string title
                            required property string notes
                            required property int startMinute
                            required property int durationMinutes
                            required property string timeRange
                            required property string color
                            required property string category
                            required property int status
                            required property bool isTemplate
                            required property var linkedTaskId
                            required property bool notifyEnabled
                            required property bool synced
                            required property bool syncPending

                            readonly property string accentColor: blockWrapper.color !== "" ? blockWrapper.color : "#3daee9"
                            readonly property int clampedStart: Math.max(0, startMinute)
                            readonly property int clampedEnd: Math.min(1440, startMinute + durationMinutes)

                            visible: clampedEnd > clampedStart
                            x: timelineCanvas.gutterWidth + Kirigami.Units.smallSpacing
                            y: timelineCanvas.topPad + (clampedStart / 60.0) * page.hourHeight + 2
                            width: Math.max(120, timelineCanvas.width - x - Kirigami.Units.largeSpacing - 4)
                            height: Math.max(Kirigami.Units.gridUnit * 2.2, ((clampedEnd - clampedStart) / 60.0) * page.hourHeight - 4)
                            z: 3

                            Rectangle {
                                id: blockCard
                                anchors.fill: parent
                                radius: Kirigami.Units.cornerRadius
                                color: blockWrapper.status === 1 ? Qt.alpha(Kirigami.Theme.positiveTextColor, 0.18)
                                     : blockWrapper.status === 2 ? Qt.alpha(Kirigami.Theme.disabledTextColor, 0.12)
                                     : Qt.alpha(blockWrapper.accentColor, 0.24)
                                border.width: 1
                                border.color: blockWrapper.status === 1 ? Kirigami.Theme.positiveTextColor : blockWrapper.accentColor
                                opacity: blockWrapper.status === 2 ? 0.65 : 1.0

                                Rectangle {
                                    width: 5
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    anchors.left: parent.left
                                    radius: 2
                                    color: blockWrapper.accentColor
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: Kirigami.Units.smallSpacing + 5
                                    anchors.rightMargin: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.CheckBox {
                                        checked: blockWrapper.status === 1
                                        onToggled: App.routine.setBlockStatus(blockWrapper.blockId, checked ? 1 : 0)
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: Kirigami.Units.smallSpacing

                                            QQC2.Label {
                                                text: blockWrapper.timeRange
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize
                                                font.bold: true
                                                color: Kirigami.Theme.textColor
                                            }
                                            QQC2.Label {
                                                Layout.fillWidth: true
                                                text: blockWrapper.title
                                                font.bold: true
                                                font.strikeout: blockWrapper.status === 1 || blockWrapper.status === 2
                                                elide: Text.ElideRight
                                            }
                                            Kirigami.Icon {
                                                visible: blockWrapper.notifyEnabled
                                                source: "notifications"
                                                Layout.preferredWidth: Kirigami.Units.iconSizes.small * 0.8
                                                Layout.preferredHeight: Layout.preferredWidth
                                                opacity: 0.75
                                            }
                                            Kirigami.Icon {
                                                visible: blockWrapper.isTemplate
                                                source: "media-playlist-repeat"
                                                Layout.preferredWidth: Kirigami.Units.iconSizes.small * 0.8
                                                Layout.preferredHeight: Layout.preferredWidth
                                                opacity: 0.75
                                            }
                                            Kirigami.Icon {
                                                visible: blockWrapper.synced
                                                source: blockWrapper.syncPending ? "view-refresh" : "cloud-upload"
                                                Layout.preferredWidth: Kirigami.Units.iconSizes.small * 0.8
                                                Layout.preferredHeight: Layout.preferredWidth
                                                opacity: 0.75
                                            }
                                            Chip {
                                                visible: blockWrapper.category !== ""
                                                text: blockWrapper.category
                                                filled: true
                                                tint: blockWrapper.accentColor
                                            }
                                        }
                                        QQC2.Label {
                                            visible: blockWrapper.notes !== "" && blockWrapper.height >= Kirigami.Units.gridUnit * 3.5
                                            Layout.fillWidth: true
                                            text: blockWrapper.notes
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                            elide: Text.ElideRight
                                        }
                                    }

                                    QQC2.ToolButton {
                                        visible: blockWrapper.status === 0
                                        icon.name: "go-next"
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Skip today")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: App.routine.setBlockStatus(blockWrapper.blockId, 2)
                                    }
                                    QQC2.ToolButton {
                                        visible: blockWrapper.status === 2
                                        icon.name: "edit-undo"
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Restore for today")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: App.routine.setBlockStatus(blockWrapper.blockId, 0)
                                    }
                                    QQC2.ToolButton {
                                        icon.name: "document-edit"
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Edit block")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: blockDialog.openEdit(blockWrapper.blockId)
                                    }
                                    QQC2.ToolButton {
                                        icon.name: "edit-delete"
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Delete block")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: App.routine.removeBlock(blockWrapper.blockId)
                                    }
                                }
                            }
                        }
                    }

                    // Current time line ("Now")
                    Item {
                        visible: App.routine.isToday
                        x: timelineCanvas.gutterWidth - 4
                        y: timelineCanvas.topPad + (page.currentMinutes / 60.0) * page.hourHeight - 4
                        width: timelineCanvas.width - x - Kirigami.Units.largeSpacing
                        height: 8
                        z: 5

                        Rectangle {
                            width: 8
                            height: 8
                            radius: 4
                            color: "#ef4444"
                        }
                        Rectangle {
                            x: 8
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 8
                            height: 2
                            color: "#ef4444"
                        }
                    }
                }
            }
        }

        // ================== MODE 1: WEEK VIEW (KOrganizer Style) ==================
        ColumnLayout {
            visible: page.calendarMode === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Sticky 7-day header
            RowLayout {
                Layout.fillWidth: true
                spacing: 0

                Item {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 3.5
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                }
                Repeater {
                    model: page.weekData
                    Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 2.5
                        color: modelData.isToday ? Qt.alpha(Kirigami.Theme.highlightColor, 0.20)
                             : modelData.isSelected ? Qt.alpha(Kirigami.Theme.highlightColor, 0.08)
                             : Kirigami.Theme.backgroundColor
                        border.width: 1
                        border.color: modelData.isSelected ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.22)

                        QQC2.Label {
                            anchors.centerIn: parent
                            text: modelData.dayName + " " + modelData.dayNum
                            font.bold: modelData.isToday || modelData.isSelected
                            color: modelData.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.textColor
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: App.routine.selectedDate = modelData.iso
                            onDoubleClicked: {
                                App.routine.selectedDate = modelData.iso;
                                page.calendarMode = 0;
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
                color: Kirigami.Theme.backgroundColor

                Flickable {
                    id: weekFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: weekCanvas.height
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                    RowLayout {
                        id: weekCanvas
                        width: weekFlick.width
                        height: 24 * (Kirigami.Units.gridUnit * 2.1) + Kirigami.Units.largeSpacing * 2
                        spacing: 0

                        // Hour labels column
                        Item {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 3.5
                            Layout.fillHeight: true
                            Repeater {
                                model: 25
                                QQC2.Label {
                                    required property int index
                                    x: 4
                                    y: Kirigami.Units.smallSpacing + index * (Kirigami.Units.gridUnit * 2.1) - height / 2
                                    width: parent.width - 8
                                    horizontalAlignment: Text.AlignRight
                                    text: index === 24 ? "00:00" : String(index).padStart(2, "0") + ":00"
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.9
                                    color: Kirigami.Theme.disabledTextColor
                                }
                            }
                        }

                        // 7 day columns with drag-to-create
                        Repeater {
                            model: page.weekData
                            Rectangle {
                                id: dayCol
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: modelData.isToday ? Qt.alpha(Kirigami.Theme.highlightColor, 0.04) : "transparent"
                                border.width: 1
                                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.18)

                                MouseArea {
                                    id: colDrag
                                    x: 0
                                    y: Kirigami.Units.smallSpacing
                                    width: dayCol.width
                                    height: 24 * (Kirigami.Units.gridUnit * 2.1)
                                    acceptedButtons: Qt.LeftButton
                                    preventStealing: true

                                    property bool dragging: false
                                    property int anchorMin: 0
                                    property int currentMin: 0
                                    readonly property int selStart: Math.min(anchorMin, currentMin)
                                    readonly property int selEnd: Math.max(anchorMin, currentMin)

                                    onPressed: mouse => {
                                        const hH = Kirigami.Units.gridUnit * 2.1;
                                        const m = page.snap15FromY(mouse.y, 0, hH);
                                        anchorMin = m;
                                        currentMin = Math.min(1440, m + 30);
                                        dragging = true;
                                    }
                                    onPositionChanged: mouse => {
                                        if (!dragging) return;
                                        const hH = Kirigami.Units.gridUnit * 2.1;
                                        currentMin = Math.max(0, Math.min(1440, Math.round((mouse.y / hH) * 4) * 15));
                                    }
                                    onReleased: {
                                        if (!dragging) return;
                                        dragging = false;
                                        const s = Math.min(anchorMin, currentMin);
                                        const e = Math.max(anchorMin, currentMin);
                                        if (e - s >= 15) {
                                            App.routine.selectedDate = dayCol.modelData.iso;
                                            blockDialog.openNewWithRange(false, s, Math.max(30, e - s));
                                        }
                                    }
                                    onCanceled: dragging = false
                                    onDoubleClicked: mouse => {
                                        dragging = false;
                                        const hH = Kirigami.Units.gridUnit * 2.1;
                                        App.routine.selectedDate = dayCol.modelData.iso;
                                        blockDialog.openNewWithRange(false, page.snap15FromY(mouse.y, 0, hH), 60);
                                    }
                                }

                                // Hour grid lines
                                Repeater {
                                    model: 25
                                    Rectangle {
                                        required property int index
                                        x: 0
                                        y: Kirigami.Units.smallSpacing + index * (Kirigami.Units.gridUnit * 2.1)
                                        width: dayCol.width
                                        height: 1
                                        color: Qt.alpha(Kirigami.Theme.textColor, 0.18)
                                    }
                                }

                                // Live drag preview in week column
                                Rectangle {
                                    visible: colDrag.dragging && colDrag.selEnd > colDrag.selStart
                                    x: 3
                                    y: Kirigami.Units.smallSpacing + (colDrag.selStart / 60.0) * (Kirigami.Units.gridUnit * 2.1)
                                    width: Math.max(16, dayCol.width - 6)
                                    height: Math.max(14, ((colDrag.selEnd - colDrag.selStart) / 60.0) * (Kirigami.Units.gridUnit * 2.1))
                                    radius: 4
                                    color: Qt.alpha(Kirigami.Theme.highlightColor, 0.28)
                                    border.width: 2
                                    border.color: Kirigami.Theme.highlightColor
                                    z: 4
                                }

                                // Day blocks
                                Repeater {
                                    model: dayCol.modelData.items
                                    Rectangle {
                                        required property var modelData
                                        readonly property string blkColor: modelData.color || "#3daee9"
                                        x: 3
                                        y: Kirigami.Units.smallSpacing + (modelData.startMinute / 60.0) * (Kirigami.Units.gridUnit * 2.1) + 1
                                        width: Math.max(20, dayCol.width - 6)
                                        height: Math.max(Kirigami.Units.gridUnit * 1.4, (modelData.durationMinutes / 60.0) * (Kirigami.Units.gridUnit * 2.1) - 2)
                                        radius: 4
                                        color: Qt.alpha(blkColor, 0.28)
                                        border.width: 1
                                        border.color: blkColor
                                        clip: true
                                        z: 3

                                        Rectangle {
                                            width: 3
                                            anchors.top: parent.top
                                            anchors.bottom: parent.bottom
                                            anchors.left: parent.left
                                            color: parent.blkColor
                                        }
                                        QQC2.Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: 6
                                            anchors.rightMargin: 3
                                            anchors.topMargin: 2
                                            text: modelData.timeRange + "\n" + modelData.title
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.88
                                            font.strikeout: modelData.status === 1 || modelData.status === 2
                                            elide: Text.ElideRight
                                            wrapMode: Text.Wrap
                                        }
                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: blockDialog.openEdit(modelData.id)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ================== MODE 2: MONTH VIEW (KOrganizer Style) ==================
        ColumnLayout {
            visible: page.calendarMode === 2
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Weekday header (Mon .. Sun)
            RowLayout {
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: [
                        i18nc("weekday short", "Mon"), i18nc("weekday short", "Tue"), i18nc("weekday short", "Wed"),
                        i18nc("weekday short", "Thu"), i18nc("weekday short", "Fri"), i18nc("weekday short", "Sat"), i18nc("weekday short", "Sun")
                    ]
                    Rectangle {
                        required property string modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 1.8
                        color: Kirigami.Theme.backgroundColor
                        border.width: 1
                        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.22)
                        QQC2.Label {
                            anchors.centerIn: parent
                            text: modelData
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.bold: true
                        }
                    }
                }
            }

            // 6x7 Month Grid
            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 7
                rows: 6
                columnSpacing: 0
                rowSpacing: 0

                Repeater {
                    model: page.monthData
                    Rectangle {
                        id: monthCell
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: modelData.isToday ? Qt.alpha(Kirigami.Theme.highlightColor, 0.14)
                             : modelData.inMonth ? Kirigami.Theme.backgroundColor
                             : Qt.alpha(Kirigami.Theme.disabledTextColor, 0.06)
                        border.width: modelData.isSelected ? 2 : 1
                        border.color: modelData.isSelected ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.22)
                        clip: true

                        MouseArea {
                            anchors.fill: parent
                            onClicked: App.routine.selectedDate = monthCell.modelData.iso
                            onDoubleClicked: {
                                App.routine.selectedDate = monthCell.modelData.iso;
                                page.calendarMode = 0;
                            }
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 4
                            spacing: 2

                            QQC2.Label {
                                text: monthCell.modelData.dayNum
                                font.bold: monthCell.modelData.isToday || monthCell.modelData.isSelected
                                color: monthCell.modelData.isToday ? Kirigami.Theme.highlightColor
                                     : monthCell.modelData.inMonth ? Kirigami.Theme.textColor
                                     : Kirigami.Theme.disabledTextColor
                            }

                            Repeater {
                                model: (monthCell.modelData.entries ?? []).slice(0, 4)
                                Rectangle {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: lbl.implicitHeight + 2
                                    radius: 3
                                    color: Qt.alpha(modelData.color || "#3daee9", 0.25)
                                    border.width: 1
                                    border.color: modelData.color || "#3daee9"
                                    QQC2.Label {
                                        id: lbl
                                        anchors.fill: parent
                                        anchors.leftMargin: 4
                                        anchors.rightMargin: 4
                                        text: modelData.title
                                        font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.85
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            QQC2.Label {
                                visible: (monthCell.modelData.entries ?? []).length > 4
                                text: "+" + ((monthCell.modelData.entries ?? []).length - 4)
                                font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.82
                                color: Kirigami.Theme.disabledTextColor
                            }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
            }
        }
    }

    DateTime.DatePopup {
        id: routineStartPopup
        onAccepted: blockDialog.startDate = Qt.formatDate(value, "yyyy-MM-dd")
    }

    // Add / Edit Routine Block Dialog
    GuardedDialog {
        id: blockDialog
        property var editId: 0
        property int daysMask: 127
        property string repeatFreq: "WEEKLY"
        property int repeatInterval: 1
        property string blockColor: "#3daee9"
        property bool isTemplate: false
        property bool restrictStartDate: true
        property string startDate: ""
        property var syncAccountId: 0
        property string _initialState: ""

        function currentState() {
            return {
                title: bTitle.text,
                category: bCategory.text,
                notes: bNotes.text,
                startHour: bStartHour.value,
                startMin: bStartMin.value,
                duration: bDuration.totalMinutes,
                isTemplate: isTemplate,
                repeatFreq: repeatFreq,
                repeatInterval: repeatInterval,
                daysMask: daysMask,
                restrictStartDate: restrictStartDate,
                startDate: startDate,
                color: blockColor,
                notify: bNotify.checked,
                lead: bLead.currentIndex,
                syncAccountId: syncAccountId
            };
        }

        dirty: opened && JSON.stringify(currentState()) !== _initialState
        canSave: bTitle.text.trim() !== ""
        saveActionText: blockDialog.editId > 0 ? i18nc("@action:button", "Save") : i18nc("@action:button", "Add")
        onSaveRequested: saveBlock()

        function saveBlock() {
            if (bTitle.text.trim() === "") {
                return;
            }
            const leads = [0, 5, 10, 15, 30];
            const payload = {
                title: bTitle.text.trim(),
                category: bCategory.text.trim(),
                notes: bNotes.text.trim(),
                startMinute: bStartHour.value * 60 + bStartMin.value,
                durationMinutes: bDuration.totalMinutes,
                isTemplate: blockDialog.isTemplate,
                repeatFreq: blockDialog.isTemplate ? blockDialog.repeatFreq : "WEEKLY",
                repeatInterval: blockDialog.isTemplate ? blockDialog.repeatInterval : 1,
                daysMask: blockDialog.isTemplate ? blockDialog.daysMask : 0,
                specificDate: blockDialog.isTemplate ? "" : App.routine.selectedDate,
                startDate: (blockDialog.isTemplate && blockDialog.restrictStartDate)
                    ? (blockDialog.startDate !== "" ? blockDialog.startDate : App.routine.selectedDate)
                    : "",
                color: blockDialog.blockColor,
                notifyEnabled: bNotify.checked,
                notifyMinutesBefore: leads[bLead.currentIndex],
                syncAccountId: blockDialog.syncAccountId
            };
            if (blockDialog.editId > 0) {
                App.routine.updateBlock(blockDialog.editId, payload);
            } else {
                App.routine.addBlock(payload);
            }
            blockDialog.forceClose();
        }

        function syncChoices() {
            const list = [{ id: 0, name: i18nc("storage option", "Offline only (local)") }];
            for (const a of App.sync.accounts) {
                list.push({ id: a.id, name: i18nc("storage option", "Nextcloud Calendar (%1)", a.name) });
            }
            return list;
        }

        function openNewWithRange(asTemplate, startMin, durationMins) {
            expandedMode = false;
            editId = 0;
            isTemplate = !!asTemplate;
            restrictStartDate = true;
            startDate = App.routine.selectedDate;
            bTitle.text = "";
            bCategory.text = "";
            bNotes.text = "";
            const m = Math.max(0, Math.min(1425, startMin ?? (9 * 60)));
            bStartHour.value = Math.floor(m / 60);
            bStartMin.value = m % 60;
            bDuration.totalMinutes = Math.max(15, Math.min(480, durationMins ?? 60));
            repeatFreq = "WEEKLY";
            repeatInterval = 1;
            daysMask = 127;
            blockColor = "#3daee9";
            bNotify.checked = true;
            bLead.currentIndex = 1; // 5 min
            syncAccountId = (App.sync.hasAccounts && App.settings.syncRoutines) ? App.sync.accounts[0].id : 0;
            bStorage.choices = syncChoices();
            bStorage.currentIndex = Math.max(0, bStorage.choices.findIndex(c => c.id === syncAccountId));
            _initialState = JSON.stringify(currentState());
            open();
            bTitle.forceActiveFocus();
        }

        function openNew(asTemplate, startMin) {
            openNewWithRange(asTemplate, startMin, 60);
        }

        function openEdit(id) {
            const info = App.routine.blockInfo(id);
            if (!info.exists) {
                return;
            }
            expandedMode = false;
            editId = id;
            isTemplate = !!info.isTemplate;
            const infoStart = info.startDate ?? "";
            restrictStartDate = isTemplate ? (infoStart !== "") : true;
            startDate = infoStart !== "" ? infoStart : App.routine.selectedDate;
            bTitle.text = info.title;
            bCategory.text = info.category;
            bNotes.text = info.notes;
            bStartHour.value = Math.floor(info.startMinute / 60);
            bStartMin.value = info.startMinute % 60;
            bDuration.totalMinutes = info.durationMinutes;
            repeatFreq = info.repeatFreq || "WEEKLY";
            repeatInterval = Math.max(1, info.repeatInterval || 1);
            daysMask = info.daysMask || 127;
            blockColor = info.color || "#3daee9";
            bNotify.checked = !!info.notifyEnabled;
            const leads = [0, 5, 10, 15, 30];
            bLead.currentIndex = Math.max(0, leads.indexOf(info.notifyMinutesBefore));
            syncAccountId = info.syncAccountId ?? 0;
            bStorage.choices = syncChoices();
            bStorage.currentIndex = Math.max(0, bStorage.choices.findIndex(c => c.id === syncAccountId));
            _initialState = JSON.stringify(currentState());
            open();
            bTitle.forceActiveFocus();
        }

        property bool expandedMode: false

        title: editId > 0 ? i18nc("@title:window", "Edit Routine Block") : i18nc("@title:window", "New Routine Block")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: expandedMode
            ? Math.max(Kirigami.Units.gridUnit * 48, (applicationWindow()?.width ?? 1000) - Kirigami.Units.gridUnit * 4)
            : Math.min(Kirigami.Units.gridUnit * 34, (applicationWindow()?.width ?? 800) - Kirigami.Units.gridUnit * 4)
        preferredHeight: expandedMode
            ? Math.max(Kirigami.Units.gridUnit * 34, (applicationWindow()?.height ?? 720) - Kirigami.Units.gridUnit * 4)
            : Math.min(formScroll.implicitHeight + Kirigami.Units.gridUnit * 7, (applicationWindow()?.height ?? 720) - Kirigami.Units.gridUnit * 4)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: blockDialog.expandedMode ? "view-restore" : "view-fullscreen"
                text: blockDialog.expandedMode ? i18nc("@action:button", "Restore Size") : i18nc("@action:button", "Expand View")
                onTriggered: blockDialog.expandedMode = !blockDialog.expandedMode
            },
            Kirigami.Action {
                icon.name: "dialog-ok"
                text: blockDialog.editId > 0 ? i18nc("@action:button", "Save") : i18nc("@action:button", "Add")
                enabled: bTitle.text.trim() !== ""
                onTriggered: blockDialog.saveBlock()
            }
        ]

        QQC2.ScrollView {
            id: formScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            Kirigami.FormLayout {
                width: formScroll.availableWidth
            QQC2.TextField {
                id: bTitle
                Kirigami.FormData.label: i18nc("@label", "Activity:")
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "e.g. Deep work, Gym, Study…")
            }
            QQC2.TextField {
                id: bCategory
                Kirigami.FormData.label: i18nc("@label", "Category:")
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "Optional (Work, Health, Study…)")
            }
            RowLayout {
                Kirigami.FormData.label: i18nc("@label", "Start time:")
                spacing: Kirigami.Units.smallSpacing
                PaddedSpinBox {
                    id: bStartHour
                    minWidthUnits: 6
                    from: 0
                    to: 23
                    textFromValue: v => String(v).padStart(2, "0") + " h"
                    valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                }
                QQC2.Label { text: ":"; font.bold: true }
                PaddedSpinBox {
                    id: bStartMin
                    minWidthUnits: 6
                    from: 0
                    to: 55
                    stepSize: 5
                    textFromValue: v => String(v).padStart(2, "0") + " min"
                    valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                }
            }
            DurationPicker {
                id: bDuration
                Kirigami.FormData.label: i18nc("@label", "Duration:")
                minMinutes: 10
                maxMinutes: 480
            }
            QQC2.Switch {
                Kirigami.FormData.label: i18nc("@label", "Repeat:")
                text: i18nc("@option:check", "Recurring routine template")
                checked: blockDialog.isTemplate
                onToggled: blockDialog.isTemplate = checked
            }
            RowLayout {
                visible: blockDialog.isTemplate
                Kirigami.FormData.label: i18nc("@label", "Frequency:")
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                QQC2.ComboBox {
                    id: bFreqCombo
                    readonly property var freqs: ["DAILY", "WEEKLY", "MONTHLY", "YEARLY"]
                    Layout.fillWidth: true
                    model: [
                        i18nc("recurrence", "Daily"),
                        i18nc("recurrence", "Weekly"),
                        i18nc("recurrence", "Monthly"),
                        i18nc("recurrence", "Yearly")
                    ]
                    currentIndex: Math.max(0, freqs.indexOf(blockDialog.repeatFreq))
                    onActivated: index => {
                        blockDialog.repeatFreq = freqs[index];
                    }
                }
                PaddedSpinBox {
                    id: bRepeatInterval
                    minWidthUnits: 7
                    from: 1
                    to: 365
                    value: blockDialog.repeatInterval
                    textFromValue: v => i18ncp("recurrence interval, e.g. every 2 (weeks)", "every %1", "every %1", v)
                    valueFromText: text => Math.max(1, parseInt(text.replace(/\D/g, "")) || 1)
                    onValueModified: blockDialog.repeatInterval = value
                }
                QQC2.Label {
                    color: Kirigami.Theme.disabledTextColor
                    text: {
                        const n = blockDialog.repeatInterval;
                        switch (blockDialog.repeatFreq) {
                        case "DAILY":
                            return i18ncp("recurrence unit", "day", "days", n);
                        case "MONTHLY":
                            return i18ncp("recurrence unit", "month", "months", n);
                        case "YEARLY":
                            return i18ncp("recurrence unit", "year", "years", n);
                        default:
                            return i18ncp("recurrence unit", "week", "weeks", n);
                        }
                    }
                }
            }
            // All 7 weekday pills in a single non-wrapping row
            RowLayout {
                visible: blockDialog.isTemplate && blockDialog.repeatFreq === "WEEKLY"
                Kirigami.FormData.label: i18nc("@label", "Days:")
                Layout.fillWidth: true
                spacing: 4
                Repeater {
                    model: [
                        i18nc("weekday short", "Mon"), i18nc("weekday short", "Tue"), i18nc("weekday short", "Wed"),
                        i18nc("weekday short", "Thu"), i18nc("weekday short", "Fri"), i18nc("weekday short", "Sat"), i18nc("weekday short", "Sun")
                    ]
                    QQC2.ToolButton {
                        required property int index
                        required property string modelData
                        readonly property int bit: 1 << index
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 2.3
                        leftPadding: 4
                        rightPadding: 4
                        checkable: true
                        checked: (blockDialog.daysMask & bit) !== 0
                        text: modelData
                        font: Kirigami.Theme.smallFont
                        onClicked: blockDialog.daysMask = blockDialog.daysMask ^ bit
                    }
                }
            }
            ColumnLayout {
                visible: blockDialog.isTemplate
                Kirigami.FormData.label: i18nc("@label", "Starts on:")
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.CheckBox {
                        text: i18nc("@option:check", "Only from start date forward")
                        checked: blockDialog.restrictStartDate
                        onToggled: {
                            blockDialog.restrictStartDate = checked;
                            if (checked && blockDialog.startDate === "") {
                                blockDialog.startDate = App.routine.selectedDate;
                            }
                        }
                    }
                    QQC2.Button {
                        visible: blockDialog.restrictStartDate
                        icon.name: "view-calendar"
                        text: {
                            if (blockDialog.startDate === "") {
                                return i18nc("@action:button", "Pick date");
                            }
                            const parts = blockDialog.startDate.split("-");
                            if (parts.length !== 3) {
                                return blockDialog.startDate;
                            }
                            return Qt.formatDate(new Date(Number(parts[0]), Number(parts[1]) - 1, Number(parts[2])), Qt.DefaultLocaleShortDate);
                        }
                        onClicked: {
                            const s = blockDialog.startDate !== "" ? blockDialog.startDate : App.routine.selectedDate;
                            routineStartPopup.value = new Date(s + "T00:00:00");
                            routineStartPopup.open();
                        }
                    }
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    text: blockDialog.restrictStartDate
                        ? i18nc("@info", "Repeats only from this date onward (does not backfill earlier months).")
                        : i18nc("@info", "Appears across all past and future dates matching the selected weekdays.")
                }
            }
            Flow {
                Kirigami.FormData.label: i18nc("@label", "Color:")
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: ["#3daee9", "#27ae60", "#f67400", "#e93d58", "#9b59b6", "#1abc9c", "#fdbc4b"]
                    Rectangle {
                        id: swatch
                        required property string modelData
                        width: Kirigami.Units.iconSizes.smallMedium
                        height: width
                        radius: width / 2
                        color: swatch.modelData
                        border.width: blockDialog.blockColor === swatch.modelData ? 3 : 1
                        border.color: blockDialog.blockColor === swatch.modelData ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        TapHandler { onTapped: blockDialog.blockColor = swatch.modelData }
                    }
                }
            }
            QQC2.Switch {
                id: bNotify
                Kirigami.FormData.label: i18nc("@label", "Notification:")
                text: i18nc("@option:check", "Send system notification")
                checked: true
            }
            QQC2.ComboBox {
                id: bLead
                visible: bNotify.checked
                Kirigami.FormData.label: i18nc("@label", "Remind:")
                model: [
                    i18nc("routine lead", "At start time"),
                    i18nc("routine lead", "5 minutes before"),
                    i18nc("routine lead", "10 minutes before"),
                    i18nc("routine lead", "15 minutes before"),
                    i18nc("routine lead", "30 minutes before")
                ]
            }
            QQC2.ScrollView {
                id: notesScroll
                Kirigami.FormData.label: i18nc("@label", "Notes:")
                Layout.fillWidth: true
                Layout.preferredHeight: blockDialog.expandedMode
                    ? Kirigami.Units.gridUnit * 14
                    : Math.min(Kirigami.Units.gridUnit * 10, Math.max(Kirigami.Units.gridUnit * 4, bNotes.contentHeight + Kirigami.Units.gridUnit))
                clip: true
                QQC2.ScrollBar.vertical.policy: QQC2.ScrollBar.AsNeeded

                QQC2.TextArea {
                    id: bNotes
                    wrapMode: TextEdit.Wrap
                    placeholderText: i18nc("@info:placeholder", "Details or notes for this routine block…")
                }
            }
            QQC2.ComboBox {
                id: bStorage
                property var choices: []
                visible: App.sync.hasAccounts
                Kirigami.FormData.label: i18nc("@label:listbox", "Storage:")
                Layout.fillWidth: true
                model: choices
                textRole: "name"
                onActivated: index => {
                    if (index >= 0 && index < choices.length) {
                        blockDialog.syncAccountId = choices[index].id;
                    }
                }
            }
        }
        }
    }

    // Schedule an existing task into a time slot
    GuardedDialog {
        id: scheduleTaskDialog
        property var openTasks: []
        property string _initialState: ""

        function currentState() {
            return {
                taskIdx: taskCombo.currentIndex,
                hour: sHour.value,
                min: sMin.value,
                dur: sDur.totalMinutes,
                syncAccId: sStorage.syncAccId
            };
        }

        dirty: opened && JSON.stringify(currentState()) !== _initialState
        canSave: scheduleTaskDialog.openTasks.length > 0 && taskCombo.currentIndex >= 0
        saveActionText: i18nc("@action:button", "Schedule")
        onSaveRequested: saveSchedule()

        function saveSchedule() {
            if (scheduleTaskDialog.openTasks.length === 0 || taskCombo.currentIndex < 0) {
                return;
            }
            const t = scheduleTaskDialog.openTasks[taskCombo.currentIndex];
            const bid = App.routine.scheduleTask(t.id, t.title, sHour.value * 60 + sMin.value, sDur.totalMinutes);
            if (bid > 0 && sStorage.syncAccId > 0) {
                App.routine.setBlockSyncAccount(bid, sStorage.syncAccId);
            }
            scheduleTaskDialog.forceClose();
        }

        function openPicker() {
            openTasks = App.openTasks();
            taskCombo.currentIndex = openTasks.length > 0 ? 0 : -1;
            sHour.value = Math.min(22, Math.max(0, new Date().getHours() + 1));
            sMin.value = 0;
            sDur.totalMinutes = 60;
            sStorage.syncAccId = (App.sync.hasAccounts && App.settings.syncRoutines) ? App.sync.accounts[0].id : 0;
            sStorage.choices = blockDialog.syncChoices();
            sStorage.currentIndex = Math.max(0, sStorage.choices.findIndex(c => c.id === sStorage.syncAccId));
            _initialState = JSON.stringify(currentState());
            open();
        }

        title: i18nc("@title:window", "Schedule Task in Day Planner")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 26
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "dialog-ok"
                text: i18nc("@action:button", "Schedule")
                enabled: scheduleTaskDialog.openTasks.length > 0 && taskCombo.currentIndex >= 0
                onTriggered: scheduleTaskDialog.saveSchedule()
            }
        ]

        Kirigami.FormLayout {
            QQC2.ComboBox {
                id: taskCombo
                Kirigami.FormData.label: i18nc("@label", "Task:")
                Layout.fillWidth: true
                model: scheduleTaskDialog.openTasks
                textRole: "title"
            }
            RowLayout {
                Kirigami.FormData.label: i18nc("@label", "Start time:")
                spacing: Kirigami.Units.smallSpacing
                PaddedSpinBox {
                    id: sHour
                    minWidthUnits: 6
                    from: 0
                    to: 23
                    textFromValue: v => String(v).padStart(2, "0") + " h"
                    valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                }
                QQC2.Label { text: ":"; font.bold: true }
                PaddedSpinBox {
                    id: sMin
                    minWidthUnits: 6
                    from: 0
                    to: 55
                    stepSize: 5
                    textFromValue: v => String(v).padStart(2, "0") + " min"
                    valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                }
            }
            DurationPicker {
                id: sDur
                Kirigami.FormData.label: i18nc("@label", "Duration:")
                minMinutes: 10
                maxMinutes: 360
            }
            QQC2.ComboBox {
                id: sStorage
                property var choices: []
                property var syncAccId: 0
                visible: App.sync.hasAccounts
                Kirigami.FormData.label: i18nc("@label:listbox", "Storage:")
                Layout.fillWidth: true
                model: choices
                textRole: "name"
                onActivated: index => {
                    if (index >= 0 && index < choices.length) {
                        syncAccId = choices[index].id;
                    }
                }
            }
        }
    }

    // Routine Templates Management Drawer
    Kirigami.OverlayDrawer {
        id: templatesDrawer
        edge: Qt.RightEdge
        modal: true
        width: Math.min(Kirigami.Units.gridUnit * 24, page.width * 0.85)

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: i18nc("@title", "Routine Templates")
                }
                QQC2.Button {
                    icon.name: "list-add"
                    text: i18nc("@action:button", "New")
                    onClicked: blockDialog.openNew(true, 7 * 60)
                }
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                text: i18nc("@info", "Recurring blocks repeat automatically according to their frequency and start date.")
            }
            Kirigami.Separator { Layout.fillWidth: true }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: App.routine.templates
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Rectangle {
                            width: 4
                            Layout.fillHeight: true
                            color: modelData.color
                            radius: 2
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: modelData.title
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: {
                                    const base = modelData.repeatSummary
                                        ? i18nc("template time and repeat summary", "%1 · %2", modelData.timeRange, modelData.repeatSummary)
                                        : modelData.timeRange;
                                    return modelData.startDateText
                                        ? i18nc("template summary and start date", "%1 · From %2", base, modelData.startDateText)
                                        : base;
                                }
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                                elide: Text.ElideRight
                            }
                        }
                        Kirigami.Icon {
                            visible: !!modelData.synced
                            source: "cloud-upload"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Layout.preferredWidth
                            opacity: 0.75
                        }
                        QQC2.Switch {
                            checked: modelData.active
                            onToggled: App.routine.updateBlock(modelData.id, { active: checked })
                        }
                        QQC2.ToolButton {
                            icon.name: "document-edit"
                            onClicked: blockDialog.openEdit(modelData.id)
                        }
                        QQC2.ToolButton {
                            icon.name: "edit-delete"
                            onClicked: App.routine.removeBlock(modelData.id)
                        }
                    }
                }
            }
        }
    }
}
