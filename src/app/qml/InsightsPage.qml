// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

Item {
    id: page

    signal navigateRequested(string section)
    signal taskActivated(var taskId)

    readonly property string pageTitle: i18nc("@title", "Productivity & Insights")
    readonly property var s: App.analytics.summary
    readonly property var seriesData: App.analytics.dailySeries
    readonly property int seriesCount: seriesData ? seriesData.length : 0

    property list<Kirigami.Action> pageActions: [
        Kirigami.Action {
            icon.name: "view-refresh"
            text: i18nc("@action:button", "Refresh")
            onTriggered: App.analytics.rebuild()
        }
    ]

    readonly property var openFocusChoices: {
        App.revision;
        const list = [{ id: 0, label: i18nc("focus target", "Free Focus (unlinked)") }];
        for (const t of App.openTasks()) {
            list.push({ id: t.id, label: t.title });
        }
        return list;
    }

    // Reusable metric KPI card
    component KpiCard: Rectangle {
        required property string label
        required property string value
        required property string subtitle
        required property string iconName
        required property color accentColor

        Layout.fillWidth: true
        implicitHeight: kpiCol.implicitHeight + Kirigami.Units.largeSpacing * 2
        radius: Kirigami.Units.cornerRadius * 1.5
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 3
            radius: 2
            color: accentColor
        }

        ColumnLayout {
            id: kpiCol
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                QQC2.Label {
                    Layout.fillWidth: true
                    text: label
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
                Kirigami.Icon {
                    source: iconName
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Layout.preferredWidth
                    color: accentColor
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: value
                font.bold: true
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.6
                elide: Text.ElideRight
            }

            QQC2.Label {
                Layout.fillWidth: true
                text: subtitle
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
            }
        }
    }

    QQC2.ScrollView {
        id: scroll
        anchors.fill: parent
        contentWidth: availableWidth
        QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Kirigami.Units.largeSpacing

            Item { Layout.preferredHeight: Kirigami.Units.smallSpacing }

            // ---------- Period Filter & Quick Focus Studio ----------
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.largeSpacing

                QQC2.ButtonGroup { id: periodGroup }
                RowLayout {
                    spacing: 4
                    Repeater {
                        model: [
                            { days: 1, label: i18nc("analytics period", "Today") },
                            { days: 7, label: i18nc("analytics period", "7 Days") },
                            { days: 30, label: i18nc("analytics period", "30 Days") }
                        ]
                        QQC2.ToolButton {
                            required property var modelData
                            checkable: true
                            checked: App.analytics.periodDays === modelData.days
                            QQC2.ButtonGroup.group: periodGroup
                            text: modelData.label
                            onClicked: App.analytics.periodDays = modelData.days
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                // Quick Pomodoro / Timer launcher right on dashboard
                Rectangle {
                    visible: page.width >= Kirigami.Units.gridUnit * 40
                    Layout.preferredHeight: studioRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                    Layout.preferredWidth: Math.min(Kirigami.Units.gridUnit * 28, page.width * 0.52)
                    radius: Kirigami.Units.cornerRadius * 1.5
                    color: Kirigami.Theme.alternateBackgroundColor
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.highlightColor, 0.25)

                    RowLayout {
                        id: studioRow
                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing
                        spacing: Kirigami.Units.smallSpacing

                        QQC2.ComboBox {
                            id: studioTaskCombo
                            Layout.fillWidth: true
                            model: page.openFocusChoices
                            textRole: "label"
                        }
                        QQC2.Button {
                            icon.name: "media-playback-start"
                            text: i18nc("@action:button", "Pomodoro (%1m)", App.settings.pomodoroFocusMinutes)
                            onClicked: {
                                const item = page.openFocusChoices[studioTaskCombo.currentIndex];
                                App.focus.startPomodoro(item ? item.id : 0);
                            }
                        }
                        QQC2.Button {
                            icon.name: "chronometer-lap"
                            text: i18nc("@action:button", "Timer")
                            onClicked: {
                                const item = page.openFocusChoices[studioTaskCombo.currentIndex];
                                App.focus.startStopwatch(item ? item.id : 0);
                            }
                        }
                    }
                }
            }

            // ---------- KPI Cards ----------
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                columns: page.width < Kirigami.Units.gridUnit * 34 ? 1 : (page.width < Kirigami.Units.gridUnit * 54 ? 2 : 4)
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                KpiCard {
                    label: i18nc("kpi label", "Tasks Completed")
                    value: String(page.s.completedTasks ?? 0)
                    subtitle: i18nc("kpi sub", "%1 open · %2% rate", page.s.openTasks ?? 0, page.s.completionRate ?? 0)
                    iconName: "task-complete"
                    accentColor: "#27ae60"
                }

                KpiCard {
                    label: i18nc("kpi label", "Execution Time")
                    value: page.s.trackedText ?? "0 min"
                    subtitle: i18nc("kpi sub", "%1 Pomodoros · %2 min/day", page.s.pomodorosCount ?? 0, page.s.avgDailyFocusMinutes ?? 0)
                    iconName: "chronometer"
                    accentColor: "#3daee9"
                }

                KpiCard {
                    label: i18nc("kpi label", "Strategic Focus (Q1+Q2)")
                    value: (page.s.strategicFocusPct ?? 0) + "%"
                    subtitle: i18nc("kpi sub", "%1% in Q2 planning", page.s.q2PlanningPct ?? 0)
                    iconName: "view-grid"
                    accentColor: "#9b59b6"
                }

                KpiCard {
                    label: i18nc("kpi label", "Routine & Streak")
                    value: (page.s.streakDays ?? 0) + " " + i18ncp("days unit", "day", "days", page.s.streakDays ?? 0)
                    subtitle: (page.s.estimatedTasksCount ?? 0) > 0
                              ? i18nc("kpi sub", "Routine %1% · Est. %2% acc.", page.s.routineConsistencyPct ?? 0, page.s.estimationAccuracyPct ?? 0)
                              : i18nc("kpi sub", "Routine consistency: %1%", page.s.routineConsistencyPct ?? 0)
                    iconName: "emblem-favorite"
                    accentColor: "#f67400"
                }
            }

            // ---------- Actionable Insights ----------
            Rectangle {
                visible: App.analytics.insights.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                implicitHeight: insCardCol.implicitHeight + Kirigami.Units.largeSpacing * 2
                radius: Kirigami.Units.cornerRadius * 1.5
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                ColumnLayout {
                    id: insCardCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18nc("@title:group", "Productivity Insights")
                    }

                    Repeater {
                        model: App.analytics.insights
                        delegate: Rectangle {
                            required property var modelData
                            readonly property color tone: modelData.severity === "positive" ? Kirigami.Theme.positiveTextColor
                                                        : modelData.severity === "warning" ? Kirigami.Theme.negativeTextColor
                                                        : Kirigami.Theme.highlightColor
                            Layout.fillWidth: true
                            implicitHeight: insRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                            radius: Kirigami.Units.cornerRadius
                            color: Qt.alpha(tone, 0.08)
                            border.width: 1
                            border.color: Qt.alpha(tone, 0.25)

                            RowLayout {
                                id: insRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: Kirigami.Units.smallSpacing
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: modelData.icon
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: tone
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        font.bold: true
                                        wrapMode: Text.Wrap
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: modelData.detail
                                        font: Kirigami.Theme.smallFont
                                        color: Kirigami.Theme.disabledTextColor
                                        wrapMode: Text.Wrap
                                    }
                                }

                                QQC2.Button {
                                    visible: (modelData.actionSection ?? "") !== ""
                                    flat: true
                                    icon.name: "go-next"
                                    text: modelData.actionSection === "matrix" ? i18nc("@action:button", "Open Matrix")
                                        : modelData.actionSection === "routine" ? i18nc("@action:button", "Open Routine")
                                        : i18nc("@action:button", "Open Tasks")
                                    onClicked: page.navigateRequested(modelData.actionSection)
                                }
                            }
                        }
                    }
                }
            }

            // ---------- Daily Focus & Completions Chart + Quadrant Breakdown ----------
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                columns: page.width < Kirigami.Units.gridUnit * 48 ? 1 : 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                // Chart 1: Daily Focus Minutes + Completed Tasks (fills entire card width cleanly)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: page.width >= Kirigami.Units.gridUnit * 48
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: chart1Col.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.cornerRadius * 1.5
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                    ColumnLayout {
                        id: chart1Col
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            Kirigami.Heading {
                                Layout.fillWidth: true
                                level: 3
                                text: i18nc("@title:group", "Daily Activity & Focus")
                            }
                            RowLayout {
                                spacing: Kirigami.Units.largeSpacing
                                RowLayout {
                                    spacing: 5
                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 3
                                        color: "#3daee9"
                                    }
                                    QQC2.Label {
                                        font: Kirigami.Theme.smallFont
                                        text: i18nc("chart legend blue bar", "Focus time (min)")
                                    }
                                }
                                RowLayout {
                                    spacing: 5
                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 3
                                        color: "#27ae60"
                                    }
                                    QQC2.Label {
                                        font: Kirigami.Theme.smallFont
                                        text: i18nc("chart legend green bar", "Tasks completed")
                                    }
                                }
                            }
                        }

                        RowLayout {
                            id: chartRow
                            Layout.fillWidth: true
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 11
                            clip: true
                            spacing: page.seriesCount > 14 ? 2 : 6

                            readonly property int barW: Math.max(3, Math.min(18, Math.floor(((width - Math.max(0, page.seriesCount - 1) * spacing) / Math.max(1, page.seriesCount) - 4) * 0.44)))

                            Repeater {
                                model: page.seriesData
                                delegate: ColumnLayout {
                                    required property int index
                                    required property var modelData
                                    readonly property int maxMin: Math.max(1, page.s.maxDailyMinutes ?? 1)
                                    readonly property int maxCmp: Math.max(1, page.s.maxDailyCompleted ?? 1)
                                    readonly property bool showLabel: page.seriesCount <= 14 || index === 0 || index === page.seriesCount - 1 || (index % 5 === 0) || !!modelData.isToday
                                    readonly property bool showValues: page.seriesCount <= 14

                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: 3

                                    Item {
                                        id: barPlotArea
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        readonly property int maxBarH: Math.max(20, height - (showValues ? 20 : 4))

                                        Item {
                                            width: chartRow.barW * 2 + 4
                                            height: parent.height
                                            anchors.horizontalCenter: parent.horizontalCenter

                                            Rectangle {
                                                id: blueBar
                                                x: 0
                                                anchors.bottom: parent.bottom
                                                width: chartRow.barW
                                                height: Math.max(modelData.focusMinutes > 0 ? 6 : 2,
                                                                 Math.round((modelData.focusMinutes / maxMin) * barPlotArea.maxBarH))
                                                radius: Math.min(3, width / 2)
                                                color: modelData.focusMinutes > 0 ? "#3daee9" : Qt.alpha(Kirigami.Theme.textColor, 0.14)
                                            }
                                            QQC2.Label {
                                                visible: showValues && modelData.focusMinutes > 0
                                                anchors.horizontalCenter: blueBar.horizontalCenter
                                                anchors.bottom: blueBar.top
                                                anchors.bottomMargin: 2
                                                text: modelData.focusMinutes >= 60
                                                    ? Math.floor(modelData.focusMinutes / 60) + "h"
                                                    : modelData.focusMinutes + "m"
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.9
                                                font.bold: true
                                                color: "#3daee9"
                                            }
                                            Rectangle {
                                                id: greenBar
                                                x: chartRow.barW + 4
                                                anchors.bottom: parent.bottom
                                                width: chartRow.barW
                                                height: Math.max(modelData.completedCount > 0 ? 6 : 2,
                                                                 Math.round((modelData.completedCount / maxCmp) * barPlotArea.maxBarH))
                                                radius: Math.min(3, width / 2)
                                                color: modelData.completedCount > 0 ? "#27ae60" : Qt.alpha(Kirigami.Theme.textColor, 0.14)
                                            }
                                            QQC2.Label {
                                                visible: showValues && modelData.completedCount > 0
                                                anchors.horizontalCenter: greenBar.horizontalCenter
                                                anchors.bottom: greenBar.top
                                                anchors.bottomMargin: 2
                                                text: modelData.completedCount
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.9
                                                font.bold: true
                                                color: "#27ae60"
                                            }
                                        }

                                        Rectangle {
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.bottom: parent.bottom
                                            height: 1
                                            color: Qt.alpha(Kirigami.Theme.textColor, 0.12)
                                        }
                                    }

                                    QQC2.Label {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: showLabel ? modelData.shortLabel : ""
                                        font.pointSize: page.seriesCount > 14 ? Kirigami.Theme.smallFont.pointSize * 0.82 : Kirigami.Theme.smallFont.pointSize
                                        font.bold: !!modelData.isToday
                                        color: modelData.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                    }

                                    QQC2.ToolTip.text: i18nc("chart tooltip", "%1\nFocus: %2 min (%3 Pomodoros)\nCompleted: %4 tasks · Routine: %5",
                                                             modelData.fullLabel, modelData.focusMinutes, modelData.pomodoros,
                                                             modelData.completedCount, modelData.routineDone)
                                    QQC2.ToolTip.visible: barHover.hovered
                                    HoverHandler { id: barHover }
                                }
                            }
                        }
                    }
                }

                // Chart 2: Eisenhower Quadrant Distribution & List Breakdown
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: page.width >= Kirigami.Units.gridUnit * 48
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: chart2Col.implicitHeight + Kirigami.Units.largeSpacing * 2
                    radius: Kirigami.Units.cornerRadius * 1.5
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                    ColumnLayout {
                        id: chart2Col
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            Layout.fillWidth: true
                            level: 3
                            text: i18nc("@title:group", "Time & Effort by Quadrant")
                        }

                        Repeater {
                            model: App.analytics.quadrantBreakdown
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 3

                                RowLayout {
                                    Layout.fillWidth: true
                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    QQC2.Label {
                                        font: Kirigami.Theme.smallFont
                                        color: Kirigami.Theme.disabledTextColor
                                        text: i18nc("quadrant stats", "%1 open · %2 done · %3 (%4%)",
                                                    modelData.openCount, modelData.completedCount, modelData.trackedText, modelData.sharePct)
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 8
                                    radius: 4
                                    color: Qt.alpha(Kirigami.Theme.textColor, 0.1)

                                    Rectangle {
                                        width: Math.max(modelData.sharePct > 0 ? 6 : 0, Math.round(parent.width * (modelData.sharePct / 100.0)))
                                        height: parent.height
                                        radius: 4
                                        color: modelData.color
                                    }
                                }
                            }
                        }

                        Kirigami.Separator {
                            visible: App.analytics.listBreakdown.length > 0
                            Layout.fillWidth: true
                            Layout.topMargin: Kirigami.Units.smallSpacing
                        }

                        QQC2.Label {
                            visible: App.analytics.listBreakdown.length > 0
                            text: i18nc("@title:group", "Top Lists")
                            font.pointSize: Kirigami.Theme.smallFont.pointSize
                            font.bold: true
                            color: Kirigami.Theme.disabledTextColor
                        }

                        Repeater {
                            model: App.analytics.listBreakdown
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Rectangle {
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: modelData.color
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: modelData.name
                                    elide: Text.ElideRight
                                }
                                QQC2.Label {
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                    text: i18nc("list breakdown", "%1 done · %2", modelData.completedCount, modelData.trackedText)
                                }
                            }
                        }
                    }
                }
            }

            // ---------- Recent Execution Sessions (proper padding + bounded scroll list, never clips) ----------
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                implicitHeight: sessionsCol.implicitHeight + Kirigami.Units.largeSpacing * 2
                radius: Kirigami.Units.cornerRadius * 1.5
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                ColumnLayout {
                    id: sessionsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: i18nc("@title:group", "Recent Focus & Execution Log")
                    }

                    QQC2.Label {
                        visible: App.analytics.recentSessions.length === 0
                        Layout.fillWidth: true
                        color: Kirigami.Theme.disabledTextColor
                        text: i18nc("@info", "No focus sessions logged yet. Start a Pomodoro timer or execution stopwatch on any task to measure effort and calibrate estimates.")
                        wrapMode: Text.Wrap
                    }

                    ListView {
                        visible: App.analytics.recentSessions.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(contentHeight, Kirigami.Units.gridUnit * 18)
                        clip: true
                        spacing: Kirigami.Units.smallSpacing / 2
                        boundsBehavior: Flickable.StopAtBounds
                        model: App.analytics.recentSessions
                        QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view ? (ListView.view.width - 6) : 0
                            implicitHeight: sessRow.implicitHeight + Kirigami.Units.smallSpacing * 1.4
                            radius: Kirigami.Units.cornerRadius
                            color: Qt.alpha(Kirigami.Theme.alternateBackgroundColor, 0.45)
                            border.width: 1
                            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.08)

                            RowLayout {
                                id: sessRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: Kirigami.Units.smallSpacing
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: modelData.completedPomodoro ? "chronometer" : "chronometer-lap"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: modelData.completedPomodoro ? "#e93d58" : Kirigami.Theme.highlightColor
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: modelData.taskTitle
                                    elide: Text.ElideRight
                                    font.bold: !!modelData.completedPomodoro
                                }
                                Chip {
                                    visible: (modelData.listName ?? "") !== ""
                                    text: modelData.listName ?? ""
                                    tint: (modelData.listColor ?? "") !== "" ? modelData.listColor : "#607d8b"
                                }
                                Chip {
                                    text: modelData.durationText + (modelData.completedPomodoro ? " · 🍅" : "")
                                    filled: !!modelData.completedPomodoro
                                    tint: modelData.completedPomodoro ? "#e93d58" : "#3daee9"
                                }
                                QQC2.Label {
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                    text: modelData.endedText
                                }
                                QQC2.ToolButton {
                                    icon.name: "edit-delete"
                                    icon.width: Kirigami.Units.iconSizes.small
                                    icon.height: Kirigami.Units.iconSizes.small
                                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Remove log entry")
                                    QQC2.ToolTip.visible: hovered
                                    onClicked: App.focus.deleteSession(modelData.id)
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.preferredHeight: Kirigami.Units.largeSpacing }
        }
    }
}
