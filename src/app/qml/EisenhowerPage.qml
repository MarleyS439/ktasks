// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

Item {
    id: page

    property var currentTaskId: 0
    property bool showDetailsPane: false
    property bool triageExpanded: true

    // Active drag state for reliable cross-quadrant & intra-quadrant reordering on Wayland/X11
    property bool dragActive: false
    property var draggedTask: null
    property real dragX: 0
    property real dragY: 0
    property int hoverQuadrant: 0
    property var hoverBeforeTaskId: 0
    property real hoverLineY: -1
    property var qBoxes: ({})
    property var cachedBoxRects: []

    signal taskActivated(var taskId)
    signal printMatrixRequested()

    readonly property bool compactGrid: gridArea.width < Kirigami.Units.gridUnit * 32
    readonly property string pageTitle: i18nc("@title", "Priority Matrix")

    readonly property var listChoices: {
        App.revision;
        const items = [{ id: 0, name: i18nc("matrix filter", "All lists") }];
        for (const l of App.lists()) {
            items.push({ id: l.id, name: l.name });
        }
        return items;
    }
    readonly property var tagChoices: {
        App.revision;
        const items = [{ value: "", label: i18nc("matrix filter", "All tags") }];
        for (const t of App.allTags()) {
            items.push({ value: t, label: "#" + t });
        }
        return items;
    }

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
            icon.name: "view-process-all"
            text: App.eisenhower.triageCount > 0
                  ? i18nc("@action:button", "Guided Triage (%1)", App.eisenhower.triageCount)
                  : i18nc("@action:button", "Guided Triage")
            enabled: App.eisenhower.triageCount > 0
            onTriggered: triageDialog.openNext()
        },
        Kirigami.Action {
            icon.name: "list-add"
            text: i18nc("@action:button", "New Task")
            onTriggered: newTaskDialog.openFresh()
        },
        Kirigami.Action {
            icon.name: "document-print"
            text: i18nc("@action:button", "Print / PDF…")
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.printMatrixRequested()
        }
    ]

    function quadrantName(q) {
        switch (q) {
        case 1: return i18nc("eisenhower quadrant short", "Q1 · Do Now");
        case 2: return i18nc("eisenhower quadrant short", "Q2 · Schedule");
        case 3: return i18nc("eisenhower quadrant short", "Q3 · Delegate");
        case 4: return i18nc("eisenhower quadrant short", "Q4 · Eliminate");
        default: return i18nc("eisenhower quadrant short", "Unclassified");
        }
    }

    function openCardMenu(taskObj) {
        cardMenu.taskObj = taskObj;
        cardMenu.popup();
    }

    function rebuildDragBoxCache() {
        const rects = [];
        for (let q = 1; q <= 4; ++q) {
            const box = qBoxes[q];
            if (!box || !box.visible) {
                continue;
            }
            const bPt = box.mapToItem(page, 0, 0);
            const lv = box.listViewItem;
            const lPt = lv ? lv.mapToItem(page, 0, 0) : bPt;
            rects.push({
                q: q,
                box: box,
                lv: lv,
                bx0: bPt.x,
                by0: bPt.y,
                bx1: bPt.x + box.width,
                by1: bPt.y + box.height,
                lx0: lPt.x,
                ly0: lPt.y,
                lw: lv ? lv.width : 0,
                lh: lv ? lv.height : 0
            });
        }
        cachedBoxRects = rects;
    }

    function beginDrag(taskObj, px, py) {
        if (!taskObj || taskObj.readOnly) {
            return;
        }
        rebuildDragBoxCache();
        draggedTask = taskObj;
        dragActive = true;
        updateDrag(px, py);
    }

    function updateDrag(px, py) {
        dragX = px;
        dragY = py;
        let foundQ = 0;
        let beforeId = 0;
        let lineY = -1;

        for (let i = 0; i < cachedBoxRects.length; ++i) {
            const r = cachedBoxRects[i];
            if (px >= r.bx0 && px <= r.bx1 && py >= r.by0 && py <= r.by1) {
                foundQ = r.q;
                const lv = r.lv;
                const model = r.box.tasksModel;
                const count = lv ? lv.count : 0;
                const listPy = py - r.ly0;
                if (lv && model && count > 0 && listPy >= 0 && listPy <= r.lh) {
                    const contentYPos = listPy + lv.contentY;
                    const idx = lv.indexAt(r.lw * 0.5, contentYPos);
                    if (idx >= 0 && idx < count) {
                        const item = lv.itemAtIndex(idx);
                        if (item) {
                            const relTop = item.y - lv.contentY;
                            const mid = relTop + item.height * 0.5;
                            if (listPy < mid) {
                                beforeId = model.idAt(idx);
                                lineY = Math.max(2, relTop - 2);
                            } else {
                                beforeId = (idx + 1 < count) ? model.idAt(idx + 1) : 0;
                                lineY = Math.min(r.lh - 4, relTop + item.height + 2);
                            }
                        }
                    } else if (listPy < lv.topMargin + 8) {
                        beforeId = model.idAt(0);
                        lineY = 2;
                    } else {
                        beforeId = 0;
                        const lastItem = lv.itemAtIndex(count - 1);
                        lineY = lastItem ? Math.min(r.lh - 4, (lastItem.y - lv.contentY) + lastItem.height + 2) : -1;
                    }
                }
                break;
            }
        }

        hoverQuadrant = foundQ;
        hoverBeforeTaskId = beforeId;
        hoverLineY = lineY;
    }

    function finishDrag() {
        if (dragActive && draggedTask && hoverQuadrant >= 1 && hoverQuadrant <= 4) {
            const tid = draggedTask.id;
            if (hoverBeforeTaskId !== tid) {
                App.eisenhower.moveToQuadrantAt(tid, hoverQuadrant, hoverBeforeTaskId);
            } else {
                App.eisenhower.classifyTask(tid, hoverQuadrant);
            }
        }
        cancelDrag();
    }

    function cancelDrag() {
        dragActive = false;
        draggedTask = null;
        cachedBoxRects = [];
        hoverQuadrant = 0;
        hoverBeforeTaskId = 0;
        hoverLineY = -1;
    }

    // Gentle auto-scroll when dragging near top/bottom edge of hovered quadrant list
    Timer {
        interval: 40
        repeat: true
        running: page.dragActive && page.hoverQuadrant >= 1 && page.hoverQuadrant <= 4
        onTriggered: {
            const box = page.qBoxes[page.hoverQuadrant];
            if (!box) {
                return;
            }
            const lv = box.listViewItem;
            const pt = page.mapToItem(lv, page.dragX, page.dragY);
            if (pt.x < 0 || pt.x > lv.width) {
                return;
            }
            if (pt.y >= 0 && pt.y < 28 && lv.contentY > 0) {
                lv.contentY = Math.max(0, lv.contentY - 12);
                page.updateDrag(page.dragX, page.dragY);
            } else if (pt.y > lv.height - 28 && pt.y <= lv.height && lv.contentY < lv.contentHeight - lv.height) {
                lv.contentY = Math.min(lv.contentHeight - lv.height, lv.contentY + 12);
                page.updateDrag(page.dragX, page.dragY);
            }
        }
    }

    // ---------- Reusable Quadrant Box Component ----------
    component QuadrantBox: Rectangle {
        id: qbox
        required property int quadrant
        required property string badgeText
        required property string titleText
        required property string subtitleText
        required property string priorityLabel
        required property color accentColor
        required property var tasksModel
        required property string emptyText

        readonly property alias listViewItem: qList
        readonly property bool isDropActive: page.dragActive && page.hoverQuadrant === qbox.quadrant

        Component.onCompleted: {
            const map = page.qBoxes;
            map[quadrant] = qbox;
            page.qBoxes = map;
        }

        radius: Kirigami.Units.cornerRadius * 1.5
        color: isDropActive ? Qt.alpha(accentColor, 0.08) : Kirigami.Theme.backgroundColor
        border.width: isDropActive ? 2 : 1
        border.color: isDropActive ? accentColor : Qt.alpha(accentColor, 0.35)
        clip: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Quadrant Header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: headerRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                color: Qt.alpha(qbox.accentColor, 0.14)

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 3
                    color: qbox.accentColor
                }

                RowLayout {
                    id: headerRow
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        radius: 4
                        color: qbox.accentColor
                        Layout.preferredWidth: qTagLbl.implicitWidth + 10
                        Layout.preferredHeight: qTagLbl.implicitHeight + 4
                        QQC2.Label {
                            id: qTagLbl
                            anchors.centerIn: parent
                            text: qbox.badgeText
                            font.bold: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: "#ffffff"
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: qbox.titleText
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: qbox.subtitleText + " · " + qbox.priorityLabel
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                            elide: Text.ElideRight
                        }
                    }

                    // Discreet overflow menu on Q4 so bulk deletion is strictly last-resort
                    QQC2.ToolButton {
                        visible: qbox.quadrant === 4 && qbox.tasksModel.count > 0
                        icon.name: "overflow-menu"
                        icon.width: Kirigami.Units.iconSizes.small
                        icon.height: Kirigami.Units.iconSizes.small
                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Quadrant options")
                        QQC2.ToolTip.visible: hovered
                        onClicked: q4HeaderMenu.popup(this, 0, height)
                        QQC2.Menu {
                            id: q4HeaderMenu
                            QQC2.MenuItem {
                                icon.name: "edit-delete"
                                text: i18nc("@action:inmenu", "Delete All Q4 Tasks…")
                                onTriggered: clearQ4Prompt.open()
                            }
                        }
                    }

                    Rectangle {
                        radius: 10
                        color: Qt.alpha(qbox.accentColor, 0.22)
                        Layout.preferredWidth: Math.max(22, cntLbl.implicitWidth + 12)
                        Layout.preferredHeight: 20
                        QQC2.Label {
                            id: cntLbl
                            anchors.centerIn: parent
                            text: qbox.tasksModel.count
                            font.bold: true
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            color: App.adaptiveChipColor(qbox.accentColor, Kirigami.Theme.backgroundColor)
                        }
                    }
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                color: Qt.alpha(qbox.accentColor, 0.25)
            }

            // Direct ListView (never nested inside another ScrollView so wheel scrolling never locks)
            ListView {
                id: qList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: qbox.tasksModel
                clip: true
                cacheBuffer: 320
                spacing: 4
                topMargin: Kirigami.Units.smallSpacing
                bottomMargin: Kirigami.Units.smallSpacing
                leftMargin: Kirigami.Units.smallSpacing
                rightMargin: Kirigami.Units.smallSpacing
                boundsBehavior: Flickable.StopAtBounds
                interactive: !page.dragActive
                QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 2
                    visible: qList.count === 0
                    icon.name: "task-complete"
                    text: qbox.emptyText
                }

                // Live insertion indicator bar during drag
                Rectangle {
                    z: 20
                    visible: qbox.isDropActive && page.hoverLineY >= 0
                    x: qList.leftMargin
                    y: Math.max(0, Math.min(qList.height - 3, page.hoverLineY))
                    width: Math.max(20, qList.width - qList.leftMargin - qList.rightMargin)
                    height: 3
                    radius: 2
                    color: qbox.accentColor
                }

                delegate: Rectangle {
                    id: card
                    required property int index
                    required property int taskId
                    required property string title
                    required property string notes
                    required property bool done
                    required property int priority
                    required property int quadrant
                    required property bool explicitQuadrant
                    required property int suggestedQuadrant
                    required property bool urgentDue
                    required property bool suggestPromote
                    required property string dueText
                    required property bool overdue
                    required property int listId
                    required property string listName
                    required property string listColor
                    required property var tags
                    required property bool hasNotes
                    required property bool recurring
                    required property int estimatedMinutes
                    required property int trackedSeconds
                    required property string trackedText
                    required property int pomodorosCompleted
                    required property bool readOnly

                    function snapshot() {
                        return {
                            id: card.taskId,
                            title: card.title,
                            notes: card.notes,
                            done: card.done,
                            priority: card.priority,
                            quadrant: card.quadrant,
                            explicitQuadrant: card.explicitQuadrant,
                            suggestedQuadrant: card.suggestedQuadrant,
                            urgentDue: card.urgentDue,
                            suggestPromote: card.suggestPromote,
                            dueText: card.dueText,
                            overdue: card.overdue,
                            listId: card.listId,
                            listName: card.listName,
                            listColor: card.listColor,
                            tags: card.tags,
                            hasNotes: card.hasNotes,
                            recurring: card.recurring,
                            estimatedMinutes: card.estimatedMinutes,
                            trackedSeconds: card.trackedSeconds,
                            trackedText: card.trackedText,
                            pomodorosCompleted: card.pomodorosCompleted,
                            readOnly: card.readOnly
                        };
                    }

                    readonly property bool selected: page.currentTaskId === card.taskId
                    readonly property bool isBeingDragged: page.dragActive && page.draggedTask && page.draggedTask.id === card.taskId
                    readonly property bool isFocusing: App.focus.hasSession && App.focus.activeTaskId === card.taskId

                    width: qList.width - qList.leftMargin - qList.rightMargin
                    implicitHeight: cardCol.implicitHeight + Kirigami.Units.smallSpacing * 2
                    radius: Kirigami.Units.cornerRadius
                    color: selected ? Qt.alpha(Kirigami.Theme.highlightColor, 0.14)
                         : cardHover.hovered ? Kirigami.Theme.alternateBackgroundColor
                         : Kirigami.Theme.backgroundColor
                    border.width: selected ? 1.5 : 1
                    border.color: selected ? Kirigami.Theme.highlightColor
                                : Qt.alpha(Kirigami.Theme.textColor, 0.12)
                    opacity: isBeingDragged ? 0.35 : (card.done ? 0.65 : 1.0)

                    // Colored accent stripe
                    Rectangle {
                        width: 4
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        radius: 2
                        color: card.listColor !== "" ? card.listColor : qbox.accentColor
                    }

                    // Card drag / click / right-click area (behind checkboxes & action buttons in cardCol)
                    MouseArea {
                        id: cardArea
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: false
                        cursorShape: page.dragActive ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                        property real pressX: 0
                        property real pressY: 0

                        onPressed: mouse => {
                            if (mouse.button === Qt.RightButton) {
                                page.taskActivated(card.taskId);
                                page.openCardMenu(card.snapshot());
                                mouse.accepted = true;
                                return;
                            }
                            pressX = mouse.x;
                            pressY = mouse.y;
                            preventStealing = true;
                        }
                        onPositionChanged: mouse => {
                            if (!(mouse.buttons & Qt.LeftButton) || card.readOnly) {
                                return;
                            }
                            const pt = cardArea.mapToItem(page, mouse.x, mouse.y);
                            if (!page.dragActive) {
                                if (Math.hypot(mouse.x - pressX, mouse.y - pressY) >= 8) {
                                    page.beginDrag(card.snapshot(), pt.x, pt.y);
                                }
                            } else {
                                page.updateDrag(pt.x, pt.y);
                            }
                        }
                        onReleased: mouse => {
                            preventStealing = false;
                            if (mouse.button !== Qt.LeftButton) {
                                return;
                            }
                            if (page.dragActive) {
                                page.finishDrag();
                            } else {
                                page.taskActivated(card.taskId);
                            }
                        }
                        onCanceled: {
                            preventStealing = false;
                            if (page.dragActive) {
                                page.cancelDrag();
                            }
                        }
                    }

                    ColumnLayout {
                        id: cardCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Kirigami.Units.smallSpacing
                        anchors.leftMargin: Kirigami.Units.smallSpacing + 5
                        spacing: 3

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Kirigami.Icon {
                                visible: !card.readOnly
                                source: "handle-sort"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Layout.preferredWidth
                                opacity: cardHover.hovered ? 0.8 : 0.35
                            }

                            QQC2.CheckBox {
                                checked: card.done
                                enabled: !card.readOnly
                                onToggled: App.setDone(card.taskId, checked)
                            }

                            QQC2.Label {
                                Layout.fillWidth: true
                                text: card.title
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                font.strikeout: card.done
                                font.weight: card.selected ? Font.DemiBold : Font.Normal
                            }

                            // Lazy action buttons: instantiated only on hovered, selected, or actively focused card
                            Loader {
                                active: cardHover.hovered || card.selected || card.isFocusing
                                visible: active
                                sourceComponent: RowLayout {
                                    spacing: 2

                                    QQC2.ToolButton {
                                        visible: !card.readOnly && card.index > 0
                                        icon.name: "go-up"
                                        icon.width: Kirigami.Units.iconSizes.small * 0.85
                                        icon.height: Kirigami.Units.iconSizes.small * 0.85
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Move up")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: App.eisenhower.moveTaskStep(card.taskId, -1)
                                    }

                                    QQC2.ToolButton {
                                        visible: !card.readOnly && card.index < qList.count - 1
                                        icon.name: "go-down"
                                        icon.width: Kirigami.Units.iconSizes.small * 0.85
                                        icon.height: Kirigami.Units.iconSizes.small * 0.85
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Move down")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: App.eisenhower.moveTaskStep(card.taskId, 1)
                                    }

                                    QQC2.ToolButton {
                                        visible: !card.done
                                        icon.name: card.isFocusing ? "media-playback-pause" : "media-playback-start"
                                        icon.width: Kirigami.Units.iconSizes.small * 0.9
                                        icon.height: Kirigami.Units.iconSizes.small * 0.9
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Start Pomodoro Focus")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: {
                                            if (card.isFocusing) {
                                                App.focus.togglePause();
                                            } else {
                                                App.focus.startPomodoro(card.taskId);
                                            }
                                        }
                                    }

                                    QQC2.ToolButton {
                                        visible: !card.readOnly && !card.done
                                        icon.name: "chronometer"
                                        icon.width: Kirigami.Units.iconSizes.small * 0.9
                                        icon.height: Kirigami.Units.iconSizes.small * 0.9
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Schedule in Routine")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: scheduleMatrixDialog.openFor(card.snapshot())
                                    }

                                    QQC2.ToolButton {
                                        icon.name: "overflow-menu"
                                        icon.width: Kirigami.Units.iconSizes.small * 0.9
                                        icon.height: Kirigami.Units.iconSizes.small * 0.9
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Task actions")
                                        QQC2.ToolTip.visible: hovered
                                        onClicked: page.openCardMenu(card.snapshot())
                                    }
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.gridUnit * 1.8
                            spacing: 4
                            visible: card.dueText !== "" || card.listName !== ""
                                     || card.suggestPromote || (card.tags && card.tags.length > 0)
                                     || card.trackedSeconds > 0 || card.estimatedMinutes > 0

                            Chip {
                                visible: card.dueText !== ""
                                text: card.dueText
                                iconName: "view-calendar-day"
                                filled: card.overdue
                                tint: card.overdue ? Kirigami.Theme.negativeTextColor
                                    : card.urgentDue ? Kirigami.Theme.neutralTextColor
                                    : Kirigami.Theme.disabledTextColor
                            }
                            Chip {
                                visible: card.trackedSeconds > 0 || card.estimatedMinutes > 0
                                text: {
                                    const spent = card.trackedSeconds > 0 ? card.trackedText : "0m";
                                    const est = card.estimatedMinutes > 0 ? (" / " + card.estimatedMinutes + "m") : "";
                                    const pom = card.pomodorosCompleted > 0 ? (" · 🍅×" + card.pomodorosCompleted) : "";
                                    return spent + est + pom;
                                }
                                iconName: "chronometer-lap"
                                tint: "#3daee9"
                            }
                            Chip {
                                visible: card.listName !== ""
                                text: card.listName
                                tint: card.listColor !== "" ? card.listColor : "#607d8b"
                            }
                            QQC2.ToolButton {
                                visible: card.suggestPromote && !card.readOnly
                                text: i18nc("@action:button", "Due soon → Q1")
                                icon.name: "go-up"
                                icon.width: Kirigami.Units.iconSizes.small * 0.85
                                icon.height: Kirigami.Units.iconSizes.small * 0.85
                                font: Kirigami.Theme.smallFont
                                padding: 2
                                leftPadding: 6
                                rightPadding: 6
                                QQC2.ToolTip.text: i18nc("@info:tooltip", "Promote to Q1 (Urgent & Important)")
                                QQC2.ToolTip.visible: hovered
                                onClicked: App.eisenhower.classifyTask(card.taskId, 1)
                            }
                            Repeater {
                                model: (card.tags && card.tags.length > 0) ? card.tags.slice(0, 3) : 0
                                Chip {
                                    required property string modelData
                                    text: "#" + modelData
                                    tint: "#fb923c"
                                }
                            }
                        }
                    }

                    HoverHandler { id: cardHover }
                }
            }
        }
    }

    // ---------- Main Split Layout (Matrix + TaskDetails) ----------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        ColumnLayout {
            id: gridArea
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 20
            spacing: Kirigami.Units.smallSpacing

            // Filter Bar
            Rectangle {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                Layout.bottomMargin: 0
                implicitHeight: filterFlow.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.cornerRadius
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.1)

                Flow {
                    id: filterFlow
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.ComboBox {
                        id: listFilter
                        implicitWidth: Math.min(Kirigami.Units.gridUnit * 11, filterFlow.width * 0.4)
                        model: page.listChoices
                        textRole: "name"
                        currentIndex: {
                            const idx = page.listChoices.findIndex(x => x.id === App.eisenhower.filterListId);
                            return idx >= 0 ? idx : 0;
                        }
                        onActivated: idx => App.eisenhower.filterListId = page.listChoices[idx].id
                    }

                    QQC2.ComboBox {
                        id: tagFilter
                        implicitWidth: Math.min(Kirigami.Units.gridUnit * 9, filterFlow.width * 0.35)
                        model: page.tagChoices
                        textRole: "label"
                        currentIndex: {
                            const idx = page.tagChoices.findIndex(x => x.value === App.eisenhower.filterTag);
                            return idx >= 0 ? idx : 0;
                        }
                        onActivated: idx => App.eisenhower.filterTag = page.tagChoices[idx].value
                    }

                    QQC2.ComboBox {
                        id: urgencyCombo
                        readonly property var hoursList: [24, 48, 168]
                        implicitWidth: Kirigami.Units.gridUnit * 10.5
                        model: [
                            i18nc("urgency window", "Urgent: Today (24h)"),
                            i18nc("urgency window", "Urgent: 48 hours"),
                            i18nc("urgency window", "Urgent: 7 days")
                        ]
                        currentIndex: Math.max(0, hoursList.indexOf(App.eisenhower.urgencyHours))
                        onActivated: idx => App.eisenhower.urgencyHours = hoursList[idx]
                    }

                    Kirigami.SearchField {
                        id: matrixSearch
                        implicitWidth: Math.max(Kirigami.Units.gridUnit * 10, Kirigami.Units.gridUnit * 13)
                        placeholderText: i18nc("@info:placeholder", "Filter matrix…")
                        text: App.eisenhower.searchText
                        onTextChanged: App.eisenhower.searchText = text
                    }

                    QQC2.CheckBox {
                        text: i18nc("@option:check", "Show completed")
                        checked: App.eisenhower.showCompleted
                        onToggled: App.eisenhower.showCompleted = checked
                    }
                }
            }

            // Collapsible Triage Strip ("Para Classificar")
            Rectangle {
                visible: App.eisenhower.triageCount > 0
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                Layout.rightMargin: Kirigami.Units.smallSpacing
                implicitHeight: triageCol.implicitHeight + Kirigami.Units.smallSpacing * 2
                radius: Kirigami.Units.cornerRadius
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.highlightColor, 0.3)

                ColumnLayout {
                    id: triageCol
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: "view-list-icons"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Layout.preferredWidth
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            font.bold: true
                            text: i18ncp("triage header", "%1 task awaiting classification — drag or click Q1–Q4 to set priority (Shift+wheel to scroll)",
                                         "%1 tasks awaiting classification — drag or click Q1–Q4 to set priority (Shift+wheel to scroll)",
                                         App.eisenhower.triageCount)
                            elide: Text.ElideRight
                        }
                        QQC2.ToolButton {
                            visible: page.triageExpanded && triageList.contentWidth > triageList.width
                            icon.name: "go-previous"
                            enabled: triageList.contentX > 2
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Scroll left")
                            QQC2.ToolTip.visible: hovered
                            onClicked: triageList.contentX = Math.max(0, triageList.contentX - (Kirigami.Units.gridUnit * 16 + Kirigami.Units.smallSpacing))
                        }
                        QQC2.ToolButton {
                            visible: page.triageExpanded && triageList.contentWidth > triageList.width
                            icon.name: "go-next"
                            enabled: triageList.contentX < triageList.contentWidth - triageList.width - 2
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Scroll right")
                            QQC2.ToolTip.visible: hovered
                            onClicked: triageList.contentX = Math.min(Math.max(0, triageList.contentWidth - triageList.width),
                                                                      triageList.contentX + (Kirigami.Units.gridUnit * 16 + Kirigami.Units.smallSpacing))
                        }
                        QQC2.ToolButton {
                            icon.name: page.triageExpanded ? "go-up" : "go-down"
                            QQC2.ToolTip.text: page.triageExpanded ? i18nc("@info:tooltip", "Collapse strip") : i18nc("@info:tooltip", "Expand strip")
                            QQC2.ToolTip.visible: hovered
                            onClicked: page.triageExpanded = !page.triageExpanded
                        }
                    }

                    ListView {
                        id: triageList
                        visible: page.triageExpanded
                        Layout.fillWidth: true
                        Layout.preferredHeight: Kirigami.Units.gridUnit * 5.8
                        bottomMargin: 16
                        orientation: ListView.Horizontal
                        spacing: Kirigami.Units.smallSpacing
                        clip: true
                        cacheBuffer: 240
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: !page.dragActive
                        model: App.eisenhower.triageModel

                        WheelHandler {
                            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                            onWheel: event => {
                                const maxX = Math.max(0, triageList.contentWidth - triageList.width);
                                if (maxX <= 0) {
                                    event.accepted = false;
                                    return;
                                }
                                let delta = 0;
                                if (Math.abs(event.angleDelta.x) > 0) {
                                    delta = event.angleDelta.x;
                                } else if ((event.modifiers & Qt.ShiftModifier) && Math.abs(event.angleDelta.y) > 0) {
                                    delta = event.angleDelta.y;
                                }
                                if (delta !== 0) {
                                    triageList.contentX = Math.max(0, Math.min(maxX, triageList.contentX - delta * 0.65));
                                    event.accepted = true;
                                } else {
                                    event.accepted = false;
                                }
                            }
                        }

                        QQC2.ScrollBar.horizontal: QQC2.ScrollBar {
                            policy: triageList.contentWidth > triageList.width ? QQC2.ScrollBar.AlwaysOn : QQC2.ScrollBar.AsNeeded
                            active: hovered || pressed || triageList.moving || triageList.contentWidth > triageList.width
                        }

                        delegate: Rectangle {
                            id: tcard
                            required property int taskId
                            required property string title
                            required property string notes
                            required property int priority
                            required property int quadrant
                            required property int suggestedQuadrant
                            required property bool urgentDue
                            required property string dueText
                            required property bool overdue
                            required property int listId
                            required property string listName
                            required property string listColor
                            required property bool readOnly
                            required property bool done

                            function snapshot() {
                                return {
                                    id: tcard.taskId,
                                    title: tcard.title,
                                    notes: tcard.notes,
                                    priority: tcard.priority,
                                    quadrant: tcard.quadrant,
                                    suggestedQuadrant: tcard.suggestedQuadrant,
                                    urgentDue: tcard.urgentDue,
                                    dueText: tcard.dueText,
                                    overdue: tcard.overdue,
                                    listId: tcard.listId,
                                    listName: tcard.listName,
                                    listColor: tcard.listColor,
                                    readOnly: tcard.readOnly,
                                    done: tcard.done
                                };
                            }

                            width: Kirigami.Units.gridUnit * 16
                            height: Math.max(40, ListView.view.height - ListView.view.bottomMargin)
                            radius: Kirigami.Units.cornerRadius
                            color: page.currentTaskId === tcard.taskId ? Qt.alpha(Kirigami.Theme.highlightColor, 0.14) : Kirigami.Theme.backgroundColor
                            border.width: tcard.urgentDue ? 1.5 : 1
                            border.color: tcard.urgentDue ? Kirigami.Theme.neutralTextColor : Qt.alpha(Kirigami.Theme.textColor, 0.15)
                            opacity: (page.dragActive && page.draggedTask && page.draggedTask.id === tcard.taskId) ? 0.35 : 1.0

                            MouseArea {
                                id: tArea
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                cursorShape: page.dragActive ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                property real pressX: 0
                                property real pressY: 0
                                onPressed: mouse => {
                                    if (mouse.button === Qt.RightButton) {
                                        page.taskActivated(tcard.taskId);
                                        page.openCardMenu(tcard.snapshot());
                                        mouse.accepted = true;
                                        return;
                                    }
                                    pressX = mouse.x;
                                    pressY = mouse.y;
                                    preventStealing = true;
                                }
                                onPositionChanged: mouse => {
                                    if (!(mouse.buttons & Qt.LeftButton) || tcard.readOnly) {
                                        return;
                                    }
                                    const pt = tArea.mapToItem(page, mouse.x, mouse.y);
                                    if (!page.dragActive) {
                                        if (Math.hypot(mouse.x - pressX, mouse.y - pressY) >= 8) {
                                            page.beginDrag(tcard.snapshot(), pt.x, pt.y);
                                        }
                                    } else {
                                        page.updateDrag(pt.x, pt.y);
                                    }
                                }
                                onReleased: mouse => {
                                    preventStealing = false;
                                    if (mouse.button !== Qt.LeftButton) {
                                        return;
                                    }
                                    if (page.dragActive) {
                                        page.finishDrag();
                                    } else {
                                        page.taskActivated(tcard.taskId);
                                    }
                                }
                                onCanceled: {
                                    preventStealing = false;
                                    if (page.dragActive) {
                                        page.cancelDrag();
                                    }
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Kirigami.Units.smallSpacing
                                spacing: 4

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: tcard.title
                                    textFormat: Text.PlainText
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        font: Kirigami.Theme.smallFont
                                        color: tcard.overdue ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                                        elide: Text.ElideRight
                                        text: [tcard.dueText, tcard.listName].filter(s => s !== "").join(" · ")
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Repeater {
                                        model: [
                                            { q: 1, label: "Q1", tip: i18nc("q1 tip", "Do Now (High)") },
                                            { q: 2, label: "Q2", tip: i18nc("q2 tip", "Schedule (Medium)") },
                                            { q: 3, label: "Q3", tip: i18nc("q3 tip", "Delegate (Low)") },
                                            { q: 4, label: "Q4", tip: i18nc("q4 tip", "Eliminate (None)") }
                                        ]
                                        QQC2.Button {
                                            required property var modelData
                                            readonly property bool isSuggested: tcard.suggestedQuadrant === modelData.q && tcard.urgentDue
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: Kirigami.Units.gridUnit * 1.45
                                            enabled: !tcard.readOnly
                                            text: modelData.label
                                            highlighted: isSuggested
                                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                                            font.bold: true
                                            padding: 0
                                            QQC2.ToolTip.text: modelData.tip
                                            QQC2.ToolTip.visible: hovered
                                            onClicked: App.eisenhower.classifyTask(tcard.taskId, modelData.q)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // 2x2 Eisenhower Grid inside clean Flickable (non-locking even when detailsPane opens)
            Flickable {
                id: matrixScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: Kirigami.Units.smallSpacing
                Layout.topMargin: 0
                clip: true
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                interactive: !page.dragActive
                contentWidth: width
                contentHeight: page.compactGrid ? Kirigami.Units.gridUnit * 60 : Math.max(height, Kirigami.Units.gridUnit * 24)
                QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                GridLayout {
                    width: matrixScroll.width
                    height: matrixScroll.contentHeight
                    columns: page.compactGrid ? 1 : 2
                    rows: page.compactGrid ? 4 : 2
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    QuadrantBox {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: page.compactGrid ? Kirigami.Units.gridUnit * 14 : -1
                        quadrant: 1
                        badgeText: "Q1"
                        titleText: i18nc("quadrant title", "Do Now")
                        subtitleText: i18nc("quadrant axis", "Urgent & Important")
                        priorityLabel: i18nc("quadrant priority", "High priority")
                        accentColor: "#e93d58"
                        tasksModel: App.eisenhower.q1Model
                        emptyText: i18nc("empty q1", "No urgent & important fires right now")
                    }

                    QuadrantBox {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: page.compactGrid ? Kirigami.Units.gridUnit * 14 : -1
                        quadrant: 2
                        badgeText: "Q2"
                        titleText: i18nc("quadrant title", "Schedule & Focus")
                        subtitleText: i18nc("quadrant axis", "Important · Not Urgent")
                        priorityLabel: i18nc("quadrant priority", "Medium priority")
                        accentColor: "#2980b9"
                        tasksModel: App.eisenhower.q2Model
                        emptyText: i18nc("empty q2", "Plan high-impact goals before they become urgent")
                    }

                    QuadrantBox {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: page.compactGrid ? Kirigami.Units.gridUnit * 14 : -1
                        quadrant: 3
                        badgeText: "Q3"
                        titleText: i18nc("quadrant title", "Delegate / Quick Wins")
                        subtitleText: i18nc("quadrant axis", "Urgent · Less Important")
                        priorityLabel: i18nc("quadrant priority", "Low priority")
                        accentColor: "#f67400"
                        tasksModel: App.eisenhower.q3Model
                        emptyText: i18nc("empty q3", "No minor urgencies waiting")
                    }

                    QuadrantBox {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.preferredHeight: page.compactGrid ? Kirigami.Units.gridUnit * 14 : -1
                        quadrant: 4
                        badgeText: "Q4"
                        titleText: i18nc("quadrant title", "Eliminate / Someday")
                        subtitleText: i18nc("quadrant axis", "Neither Urgent nor Important")
                        priorityLabel: i18nc("quadrant priority", "No priority")
                        accentColor: "#7f8c8d"
                        tasksModel: App.eisenhower.q4Model
                        emptyText: i18nc("empty q4", "Noise-free quadrant")
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            visible: detailsPane.visible
        }

        Loader {
            id: detailsPane
            active: page.showDetailsPane && page.currentTaskId > 0
            visible: active
            Layout.fillHeight: true
            Layout.preferredWidth: Math.min(Kirigami.Units.gridUnit * 24, page.width * 0.40)
            sourceComponent: TaskDetails {
                taskId: page.currentTaskId
                onCloseRequested: page.taskActivated(0)
                onTaskRequested: tid => page.taskActivated(tid)
            }
        }
    }

    // ---------- Floating Drag Ghost ----------
    Rectangle {
        id: dragGhost
        z: 9999
        visible: page.dragActive && !!page.draggedTask
        x: Math.max(4, Math.min(page.width - width - 4, page.dragX + 12))
        y: Math.max(4, Math.min(page.height - height - 4, page.dragY + 12))
        width: Kirigami.Units.gridUnit * 15
        height: ghostCol.implicitHeight + Kirigami.Units.smallSpacing * 2
        radius: Kirigami.Units.cornerRadius
        color: Kirigami.Theme.backgroundColor
        border.width: 2
        border.color: Kirigami.Theme.highlightColor
        opacity: 0.94

        ColumnLayout {
            id: ghostCol
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            spacing: 2

            QQC2.Label {
                Layout.fillWidth: true
                text: page.draggedTask ? page.draggedTask.title : ""
                font.bold: true
                elide: Text.ElideRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                color: page.hoverQuadrant > 0 ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                text: page.hoverQuadrant > 0
                      ? i18nc("drag hint", "Drop into %1", page.quadrantName(page.hoverQuadrant))
                      : i18nc("drag hint", "Drag over Q1–Q4…")
                elide: Text.ElideRight
            }
        }
    }

    // ---------- Task Context Menu ----------
    QQC2.Menu {
        id: cardMenu
        property var taskObj: ({})

        QQC2.MenuItem {
            icon.name: cardMenu.taskObj.done ? "edit-undo" : "task-complete"
            text: cardMenu.taskObj.done ? i18nc("@action:inmenu", "Reopen") : i18nc("@action:inmenu", "Complete")
            enabled: !cardMenu.taskObj.readOnly
            onTriggered: App.setDone(cardMenu.taskObj.id, !cardMenu.taskObj.done)
        }
        QQC2.MenuItem {
            icon.name: "media-playback-start"
            text: i18nc("@action:inmenu", "Start Pomodoro Focus")
            enabled: !cardMenu.taskObj.done
            onTriggered: App.focus.startPomodoro(cardMenu.taskObj.id)
        }
        QQC2.MenuItem {
            icon.name: "chronometer-lap"
            text: i18nc("@action:inmenu", "Start Execution Timer")
            enabled: !cardMenu.taskObj.done
            onTriggered: App.focus.startStopwatch(cardMenu.taskObj.id)
        }
        QQC2.MenuItem {
            icon.name: "chronometer"
            text: i18nc("@action:inmenu", "Schedule in Routine…")
            enabled: !cardMenu.taskObj.readOnly && !cardMenu.taskObj.done
            onTriggered: scheduleMatrixDialog.openFor(cardMenu.taskObj)
        }
        QQC2.MenuSeparator {}
        QQC2.Menu {
            title: i18nc("@title:menu", "Move to Quadrant")
            icon.name: "view-grid"
            enabled: !cardMenu.taskObj.readOnly
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
                    checkable: true
                    checked: cardMenu.taskObj.quadrant === modelData.q
                    onTriggered: App.eisenhower.classifyTask(cardMenu.taskObj.id, modelData.q)
                }
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                icon.name: "edit-undo"
                text: i18nc("@action:inmenu", "Return to Triage")
                onTriggered: App.eisenhower.resetQuadrant(cardMenu.taskObj.id)
            }
        }
        QQC2.MenuItem {
            icon.name: "go-up"
            text: i18nc("@action:inmenu", "Move Up in Quadrant")
            enabled: !cardMenu.taskObj.readOnly
            onTriggered: App.eisenhower.moveTaskStep(cardMenu.taskObj.id, -1)
        }
        QQC2.MenuItem {
            icon.name: "go-down"
            text: i18nc("@action:inmenu", "Move Down in Quadrant")
            enabled: !cardMenu.taskObj.readOnly
            onTriggered: App.eisenhower.moveTaskStep(cardMenu.taskObj.id, 1)
        }
        QQC2.MenuItem {
            icon.name: "view-calendar-day"
            text: i18nc("@action:inmenu", "Due Today")
            enabled: !cardMenu.taskObj.readOnly
            onTriggered: App.postpone(cardMenu.taskObj.id, 0)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            icon.name: "edit-copy"
            text: i18nc("@action:inmenu", "Copy Details")
            onTriggered: App.copyTaskDetails(cardMenu.taskObj.id)
        }
        QQC2.MenuItem {
            icon.name: "edit-delete"
            text: i18nc("@action:inmenu", "Delete")
            enabled: !cardMenu.taskObj.readOnly
            onTriggered: App.deleteTask(cardMenu.taskObj.id)
        }
    }

    // ---------- Guided Triage Wizard Dialog ----------
    Kirigami.Dialog {
        id: triageDialog
        property var queue: []
        property int index: 0
        readonly property var currentItem: (index >= 0 && index < queue.length) ? queue[index] : null

        function openNext() {
            queue = App.eisenhower.triageTasks().slice();
            index = 0;
            if (queue.length > 0) {
                open();
            }
        }

        function assignAndAdvance(q) {
            if (!currentItem) {
                return;
            }
            App.eisenhower.classifyTask(currentItem.id, q);
            index += 1;
            if (index >= queue.length) {
                close();
                applicationWindow().showPassiveNotification(i18nc("@info", "All pending tasks prioritized!"), "short");
            }
        }

        title: i18nc("@title:window", "Guided Priority Triage")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 32
        standardButtons: Kirigami.Dialog.Close

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.largeSpacing

            Shortcut { enabled: triageDialog.opened; sequence: "1"; onActivated: triageDialog.assignAndAdvance(1) }
            Shortcut { enabled: triageDialog.opened; sequence: "2"; onActivated: triageDialog.assignAndAdvance(2) }
            Shortcut { enabled: triageDialog.opened; sequence: "3"; onActivated: triageDialog.assignAndAdvance(3) }
            Shortcut { enabled: triageDialog.opened; sequence: "4"; onActivated: triageDialog.assignAndAdvance(4) }
            Shortcut { enabled: triageDialog.opened; sequence: "Space"; onActivated: {
                triageDialog.index += 1;
                if (triageDialog.index >= triageDialog.queue.length) triageDialog.close();
            }}

            QQC2.Label {
                visible: !!triageDialog.currentItem
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                text: i18nc("triage progress", "Task %1 of %2 · Press 1–4 to classify or Space to skip",
                            triageDialog.index + 1, triageDialog.queue.length)
            }

            Rectangle {
                visible: !!triageDialog.currentItem
                Layout.fillWidth: true
                implicitHeight: previewCol.implicitHeight + Kirigami.Units.largeSpacing * 2
                radius: Kirigami.Units.cornerRadius
                color: Kirigami.Theme.alternateBackgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)

                ColumnLayout {
                    id: previewCol
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        Layout.fillWidth: true
                        font.bold: true
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                        wrapMode: Text.Wrap
                        text: triageDialog.currentItem ? triageDialog.currentItem.title : ""
                    }
                    QQC2.Label {
                        visible: text !== ""
                        Layout.fillWidth: true
                        font: Kirigami.Theme.smallFont
                        color: triageDialog.currentItem && triageDialog.currentItem.overdue ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                        text: triageDialog.currentItem
                              ? [triageDialog.currentItem.dueText, triageDialog.currentItem.listName].filter(s => s !== "").join(" · ")
                              : ""
                    }
                    QQC2.Label {
                        visible: text !== ""
                        Layout.fillWidth: true
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        text: triageDialog.currentItem ? triageDialog.currentItem.notes : ""
                    }
                }
            }

            GridLayout {
                visible: !!triageDialog.currentItem
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: [
                        { q: 1, title: i18nc("triage btn", "[1] Q1 · Do Now"), sub: i18nc("triage sub", "Urgent & Important → High") },
                        { q: 2, title: i18nc("triage btn", "[2] Q2 · Schedule"), sub: i18nc("triage sub", "Important, Not Urgent → Medium") },
                        { q: 3, title: i18nc("triage btn", "[3] Q3 · Delegate"), sub: i18nc("triage sub", "Urgent, Low Impact → Low") },
                        { q: 4, title: i18nc("triage btn", "[4] Q4 · Eliminate"), sub: i18nc("triage sub", "Not Urgent or Important → None") }
                    ]
                    QQC2.Button {
                        required property var modelData
                        Layout.fillWidth: true
                        enabled: triageDialog.currentItem && !triageDialog.currentItem.readOnly
                        highlighted: triageDialog.currentItem && triageDialog.currentItem.suggestedQuadrant === modelData.q
                        onClicked: triageDialog.assignAndAdvance(modelData.q)
                        contentItem: ColumnLayout {
                            spacing: 2
                            QQC2.Label { Layout.fillWidth: true; text: modelData.title; font.bold: true }
                            QQC2.Label { Layout.fillWidth: true; text: modelData.sub; font: Kirigami.Theme.smallFont; opacity: 0.8; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }
    }

    // ---------- New Task Directly in Matrix ----------
    Kirigami.Dialog {
        id: newTaskDialog
        property int chosenQ: 2
        function openFresh() {
            newTitle.text = "";
            chosenQ = 2;
            newDueToday.checked = false;
            newListCombo.currentIndex = Math.max(0, newListCombo.choices.findIndex(c => c.id === App.eisenhower.filterListId));
            open();
            newTitle.forceActiveFocus();
        }
        title: i18nc("@title:window", "New Task in Priority Matrix")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 26
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "list-add"
                text: i18nc("@action:button", "Add")
                enabled: newTitle.text.trim() !== ""
                onTriggered: {
                    const lid = (newListCombo.currentIndex >= 0 && newListCombo.currentIndex < newListCombo.choices.length)
                        ? newListCombo.choices[newListCombo.currentIndex].id : 0;
                    const id = App.quickAdd(newTitle.text.trim(), lid, 0);
                    if (id > 0) {
                        App.eisenhower.classifyTask(id, newTaskDialog.chosenQ, newDueToday.checked);
                    }
                    newTaskDialog.close();
                }
            }
        ]
        Kirigami.FormLayout {
            QQC2.TextField {
                id: newTitle
                Kirigami.FormData.label: i18nc("@label", "Task:")
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "What needs to be done?")
            }
            QQC2.ComboBox {
                id: newListCombo
                readonly property var choices: {
                    App.revision;
                    return App.lists().filter(l => !l.readOnly);
                }
                Kirigami.FormData.label: i18nc("@label:listbox", "List:")
                Layout.fillWidth: true
                model: choices
                textRole: "name"
            }
            QQC2.ComboBox {
                Kirigami.FormData.label: i18nc("@label:listbox", "Quadrant:")
                Layout.fillWidth: true
                model: [
                    i18nc("quadrant menu", "Q1 · Do Now (High)"),
                    i18nc("quadrant menu", "Q2 · Schedule (Medium)"),
                    i18nc("quadrant menu", "Q3 · Delegate (Low)"),
                    i18nc("quadrant menu", "Q4 · Eliminate (None)")
                ]
                currentIndex: newTaskDialog.chosenQ - 1
                onActivated: idx => newTaskDialog.chosenQ = idx + 1
            }
            QQC2.CheckBox {
                id: newDueToday
                text: i18nc("@option:check", "Set due date to today")
            }
        }
    }

    // ---------- Quick Schedule in Routine Dialog ----------
    Kirigami.Dialog {
        id: scheduleMatrixDialog
        property var targetTask: ({})
        function openFor(t) {
            targetTask = t;
            msHour.value = Math.min(22, Math.max(0, new Date().getHours() + 1));
            msMin.value = 0;
            msDur.value = 60;
            open();
        }
        title: i18nc("@title:window", "Schedule in Day Planner")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 24
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "chronometer"
                text: i18nc("@action:button", "Schedule")
                onTriggered: {
                    const bid = App.routine.scheduleTask(scheduleMatrixDialog.targetTask.id,
                                                         scheduleMatrixDialog.targetTask.title,
                                                         msHour.value * 60 + msMin.value,
                                                         msDur.value,
                                                         scheduleMatrixDialog.targetTask.listColor || "");
                    if (bid > 0 && App.sync.hasAccounts && App.settings.syncRoutines) {
                        App.routine.setBlockSyncAccount(bid, App.sync.accounts[0].id);
                    }
                    scheduleMatrixDialog.close();
                    applicationWindow().showPassiveNotification(i18nc("@info", "Scheduled in Routine & Day Planner"), "short");
                }
            }
        ]
        Kirigami.FormLayout {
            QQC2.Label {
                Kirigami.FormData.label: i18nc("@label", "Task:")
                Layout.fillWidth: true
                font.bold: true
                wrapMode: Text.Wrap
                text: scheduleMatrixDialog.targetTask.title ?? ""
            }
            RowLayout {
                Kirigami.FormData.label: i18nc("@label", "Start time:")
                spacing: Kirigami.Units.smallSpacing
                PaddedSpinBox {
                    id: msHour
                    minWidthUnits: 6
                    from: 0; to: 23
                    textFromValue: v => String(v).padStart(2, "0") + " h"
                    valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                }
                QQC2.Label { text: ":"; font.bold: true }
                PaddedSpinBox {
                    id: msMin
                    minWidthUnits: 6
                    from: 0; to: 55; stepSize: 5
                    textFromValue: v => String(v).padStart(2, "0") + " min"
                    valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
                }
            }
            PaddedSpinBox {
                id: msDur
                minWidthUnits: 8
                Kirigami.FormData.label: i18nc("@label", "Duration:")
                from: 10; to: 360; stepSize: 5
                textFromValue: v => i18ncp("minutes", "%1 min", "%1 min", v)
                valueFromText: t => parseInt(t.replace(/\D/g, "")) || 30
            }
        }
    }

    // ---------- Confirm Clear Q4 (Last Resort) ----------
    Kirigami.PromptDialog {
        id: clearQ4Prompt
        title: i18nc("@title:window", "Delete Q4 Tasks?")
        subtitle: i18ncp("@info", "Delete %1 task in Q4? You can undo this action immediately afterward.",
                         "Delete all %1 tasks in Q4? You can undo this action immediately afterward.",
                         App.eisenhower.q4Count)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Delete Q4 Tasks")
                onTriggered: {
                    App.eisenhower.clearQ4();
                    clearQ4Prompt.close();
                }
            }
        ]
    }
}
