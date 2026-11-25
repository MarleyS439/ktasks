// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import app.ktasks.backend

Item {
    id: page

    signal navigateRequested(string section)
    signal openViewRequested(string view)
    signal taskActivated(var taskId)
    signal journalRequested(var entryId, bool editMode)
    signal settingsRequested()

    readonly property string pageTitle: i18nc("@title", "Summary")
    readonly property var st: App.summary.stats
    readonly property var wx: App.summary.weather
    readonly property var sched: App.summary.nextSchedule

    property list<Kirigami.Action> pageActions: [
        Kirigami.Action {
            icon.name: "list-add"
            text: i18nc("@action:button", "New Task")
            onTriggered: App.trigger("quick_add_window")
        },
        Kirigami.Action {
            icon.name: "view-refresh"
            text: App.sync.hasAccounts ? i18nc("@action:button", "Sync & Refresh") : i18nc("@action:button", "Refresh")
            onTriggered: {
                App.summary.refresh();
                if (App.sync.hasAccounts) {
                    App.sync.syncNow();
                }
            }
        }
    ]

    function quadrantColor(q) {
        switch (q) {
        case 1: return "#e93d58";
        case 2: return "#2980b9";
        case 3: return "#f67400";
        case 4: return "#7f8c8d";
        default: return Kirigami.Theme.highlightColor;
        }
    }

    component DashCard: Rectangle {
        default property alias bodyData: innerCol.data
        property string cardTitle: ""
        property string cardSubtitle: ""
        property string actionLabel: ""
        property string actionIcon: "go-next"
        property bool actionIconOnly: false
        signal actionClicked()

        Layout.fillWidth: true
        implicitHeight: outerCol.implicitHeight + Kirigami.Units.largeSpacing * 1.7
        radius: Kirigami.Units.cornerRadius * 1.5
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

        ColumnLayout {
            id: outerCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Kirigami.Units.largeSpacing * 0.85
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                visible: cardTitle !== "" || actionLabel !== ""
                Layout.fillWidth: true
                spacing: 4

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 3
                        text: cardTitle
                        elide: Text.ElideRight
                    }
                    QQC2.Label {
                        visible: cardSubtitle !== ""
                        Layout.fillWidth: true
                        text: cardSubtitle
                        font: Kirigami.Theme.smallFont
                        color: Kirigami.Theme.disabledTextColor
                        wrapMode: Text.Wrap
                    }
                }

                QQC2.Button {
                    visible: actionLabel !== ""
                    flat: true
                    icon.name: actionIcon
                    text: actionLabel
                    display: actionIconOnly ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextBesideIcon
                    QQC2.ToolTip.text: actionLabel
                    QQC2.ToolTip.visible: actionIconOnly && hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: actionClicked()
                }
            }

            ColumnLayout {
                id: innerCol
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
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
            spacing: Kirigami.Units.largeSpacing * 0.8

            Item { Layout.preferredHeight: 2 }

            // ---------- Hero Welcome Banner ----------
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                implicitHeight: heroOuterCol.implicitHeight + Kirigami.Units.largeSpacing * 1.7
                radius: Kirigami.Units.cornerRadius * 1.5
                color: Qt.alpha(Kirigami.Theme.highlightColor, 0.08)
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.highlightColor, 0.22)

                ColumnLayout {
                    id: heroOuterCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Kirigami.Units.largeSpacing * 0.85
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.largeSpacing

                        Components.Avatar {
                            source: App.effectiveAvatarPath !== "" ? ("file://" + App.effectiveAvatarPath) : ""
                            name: App.effectiveProfileName
                            implicitWidth: Kirigami.Units.iconSizes.large
                            implicitHeight: Kirigami.Units.iconSizes.large
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Chip {
                                    text: App.summary.weekdayText
                                    iconName: "view-calendar-day"
                                    filled: true
                                    tint: Kirigami.Theme.highlightColor
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: App.summary.fullDateText
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                    elide: Text.ElideRight
                                }
                                Chip {
                                    visible: (page.st.streakDays ?? 0) >= 2
                                    text: i18ncp("streak chip", "%1d streak", "%1d streak", page.st.streakDays ?? 0)
                                    iconName: "emblem-favorite"
                                    tint: "#f67400"
                                }
                                Chip {
                                    visible: App.sync.hasAccounts || App.sync.syncing
                                    text: App.sync.statusText
                                    iconName: App.sync.errorText !== "" ? "data-warning"
                                            : App.sync.offline ? "network-disconnect"
                                            : "folder-sync"
                                    tint: App.sync.errorText !== "" ? Kirigami.Theme.negativeTextColor
                                        : App.sync.offline ? "#f67400" : "#27ae60"
                                }
                            }

                            Kirigami.Heading {
                                Layout.fillWidth: true
                                level: 2
                                text: App.summary.greetingText
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.largeSpacing

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                    wrapMode: Text.Wrap
                                    text: (page.st.overdueCount ?? 0) > 0
                                          ? i18ncp("hero summary", "%1 task due today · %2 overdue item needs attention.",
                                                   "%1 tasks due today · %2 overdue items need attention.",
                                                   page.st.todayCount ?? 0, page.st.overdueCount ?? 0)
                                          : i18ncp("hero summary", "%1 task due today · %2 open in total.",
                                                   "%1 tasks due today · %2 open in total.",
                                                   page.st.todayCount ?? 0, page.st.openCount ?? 0)
                                }

                                RowLayout {
                                    visible: ((page.st.todayCount ?? 0) + (page.st.doneTodayCount ?? 0) + (page.st.routineTotalToday ?? 0)) > 0
                                    spacing: 6
                                    QQC2.Label {
                                        font: Kirigami.Theme.smallFont
                                        color: Kirigami.Theme.disabledTextColor
                                        text: i18nc("daily progress pct", "Day: %1%", page.st.dailyProgressPct ?? 0)
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 5
                                        Layout.preferredHeight: 6
                                        radius: 3
                                        color: Qt.alpha(Kirigami.Theme.textColor, 0.14)
                                        Rectangle {
                                            width: Math.round(parent.width * Math.min(100, Math.max(0, page.st.dailyProgressPct ?? 0)) / 100)
                                            height: parent.height
                                            radius: 3
                                            color: (page.st.dailyProgressPct ?? 0) >= 80 ? "#27ae60" : Kirigami.Theme.highlightColor
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---------- Quick KPI Tiles ----------
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                columns: page.width < Kirigami.Units.gridUnit * 28 ? 2 : (page.width < Kirigami.Units.gridUnit * 38 ? 3 : 5)
                columnSpacing: Kirigami.Units.smallSpacing
                rowSpacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: [
                        {
                            title: i18nc("summary tile", "Today"),
                            value: String(page.st.todayCount ?? 0),
                            sub: (page.st.doneTodayCount ?? 0) > 0
                                 ? i18nc("summary sub done today", "%1 completed today", page.st.doneTodayCount ?? 0)
                                 : i18nc("summary sub", "due tasks"),
                            icon: "view-calendar-day",
                            accent: "#3daee9",
                            action: "today"
                        },
                        {
                            title: i18nc("summary tile", "Overdue"),
                            value: String(page.st.overdueCount ?? 0),
                            sub: i18nc("summary sub", "needs review"),
                            icon: "data-warning",
                            accent: (page.st.overdueCount ?? 0) > 0 ? "#e93d58" : "#7f8c8d",
                            action: "overdue"
                        },
                        {
                            title: i18nc("summary tile", "Priorities"),
                            value: String((page.st.q1Count ?? 0) + (page.st.q2Count ?? 0)),
                            sub: (page.st.triageCount ?? 0) > 0
                                 ? i18nc("summary sub triage", "Q1: %1 · Q2: %2 · %3 to triage", page.st.q1Count ?? 0, page.st.q2Count ?? 0, page.st.triageCount ?? 0)
                                 : i18nc("summary sub", "Q1: %1 · Q2: %2", page.st.q1Count ?? 0, page.st.q2Count ?? 0),
                            icon: "view-grid",
                            accent: "#9b59b6",
                            action: "sec:matrix"
                        },
                        {
                            title: i18nc("summary tile", "Routine"),
                            value: (page.st.routineDoneToday ?? 0) + "/" + (page.st.routineTotalToday ?? 0),
                            sub: i18nc("summary sub", "blocks today"),
                            icon: "chronometer",
                            accent: "#27ae60",
                            action: "sec:routine"
                        },
                        {
                            title: i18nc("summary tile", "Focus Today"),
                            value: page.st.focusTextToday ?? "0 min",
                            sub: i18ncp("summary sub", "%1 Pomodoro", "%1 Pomodoros", page.st.pomodorosToday ?? 0),
                            icon: "chronometer-lap",
                            accent: "#f67400",
                            action: "sec:insights"
                        }
                    ]

                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: tileCol.implicitHeight + Kirigami.Units.largeSpacing * 1.25
                        radius: Kirigami.Units.cornerRadius * 1.5
                        color: tileHover.hovered ? Kirigami.Theme.alternateBackgroundColor : Kirigami.Theme.backgroundColor
                        border.width: 1
                        border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 3
                            radius: 2
                            color: modelData.accent
                        }

                        ColumnLayout {
                            id: tileCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: Kirigami.Units.smallSpacing * 1.3
                            spacing: 1

                            RowLayout {
                                Layout.fillWidth: true
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                    elide: Text.ElideRight
                                }
                                Kirigami.Icon {
                                    source: modelData.icon
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: modelData.accent
                                }
                            }
                            QQC2.Label {
                                text: modelData.value
                                font.bold: true
                                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: modelData.sub
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.action.startsWith("sec:")) {
                                    page.navigateRequested(modelData.action.slice(4));
                                } else {
                                    page.openViewRequested(modelData.action);
                                }
                            }
                        }
                        HoverHandler { id: tileHover }
                    }
                }
            }

            // ---------- Compact Weather & Scrollable 10-Day Forecast Strip ----------
            Rectangle {
                id: weatherStrip
                readonly property bool wideWeather: page.width >= Kirigami.Units.gridUnit * 40

                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                implicitHeight: weatherMainCol.implicitHeight + Kirigami.Units.smallSpacing * 2.4
                radius: Kirigami.Units.cornerRadius * 1.5
                color: Kirigami.Theme.backgroundColor
                border.width: 1
                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.12)

                ColumnLayout {
                    id: weatherMainCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Kirigami.Units.smallSpacing * 1.2
                    spacing: Kirigami.Units.smallSpacing

                    // Compact header line
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: page.wx.iconName ?? "weather-few-clouds"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Layout.preferredWidth
                        }
                        QQC2.Label {
                            text: page.wx.city
                                  ? i18nc("weather compact header", "Weather · %1", page.wx.city)
                                  : i18nc("weather compact header", "Weather")
                            font.bold: true
                            elide: Text.ElideRight
                            Layout.maximumWidth: Kirigami.Units.gridUnit * 16
                        }
                        QQC2.Label {
                            visible: !!page.wx.updatedText
                            text: i18nc("weather updated time", "· updated %1 (auto 1h)", page.wx.updatedText ?? "")
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Item { visible: !page.wx.updatedText; Layout.fillWidth: true }

                        QQC2.BusyIndicator {
                            visible: App.summary.weatherLoading
                            running: visible
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Layout.preferredWidth
                        }
                        QQC2.ToolButton {
                            icon.name: "view-refresh"
                            Accessible.name: i18nc("@action:button", "Update weather now")
                            QQC2.ToolTip.text: Accessible.name
                            QQC2.ToolTip.visible: hovered
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            onClicked: App.summary.refreshWeather(true)
                        }
                    }

                    // Compact content: Current pill + Horizontally scrollable forecast strip
                    GridLayout {
                        Layout.fillWidth: true
                        columns: weatherStrip.wideWeather ? 2 : 1
                        columnSpacing: Kirigami.Units.largeSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        // Compact Current Weather Block
                        RowLayout {
                            visible: !!page.wx.available
                            Layout.preferredWidth: weatherStrip.wideWeather ? Kirigami.Units.gridUnit * 15 : -1
                            Layout.fillWidth: !weatherStrip.wideWeather
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: page.wx.iconName ?? "weather-few-clouds"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.large
                                Layout.preferredHeight: Layout.preferredWidth
                            }

                            ColumnLayout {
                                spacing: 1
                                RowLayout {
                                    spacing: 6
                                    QQC2.Label {
                                        text: (page.wx.tempC ?? "--") + "°C"
                                        font.bold: true
                                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.35
                                    }
                                    QQC2.Label {
                                        text: (page.wx.conditionText ?? "") + " · " + (page.wx.minTempC ?? "") + "°/" + (page.wx.maxTempC ?? "") + "°"
                                        font: Kirigami.Theme.smallFont
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    font: Kirigami.Theme.smallFont
                                    color: Kirigami.Theme.disabledTextColor
                                    elide: Text.ElideRight
                                    text: i18nc("weather metrics compact", "Feels %1° · Humidity %2% · Wind %3 km/h",
                                                page.wx.feelsLikeC ?? 0, page.wx.humidity ?? 0, page.wx.windKmh ?? 0)
                                }
                            }
                        }

                        // Offline / loading fallback
                        RowLayout {
                            visible: !page.wx.available
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            QQC2.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                font: Kirigami.Theme.smallFont
                                color: Kirigami.Theme.disabledTextColor
                                text: App.summary.weatherLoading
                                      ? i18nc("weather status", "Loading local forecast…")
                                      : (page.wx.error ?? i18nc("weather status", "Configure your weather city in Settings."))
                            }
                            QQC2.Button {
                                visible: !App.summary.weatherLoading
                                flat: true
                                icon.name: "configure"
                                text: i18nc("@action:button", "Settings")
                                onClicked: page.settingsRequested()
                            }
                        }

                        // Horizontally Scrollable 10-Day Forecast Strip
                        RowLayout {
                            visible: !!page.wx.available
                            Layout.fillWidth: true
                            spacing: 2

                            QQC2.ToolButton {
                                icon.name: "go-previous"
                                visible: fcList.contentWidth > fcList.width
                                enabled: fcList.contentX > 2
                                Accessible.name: i18nc("@action:button", "Previous days")
                                onClicked: fcList.contentX = Math.max(0, fcList.contentX - fcList.width * 0.65)
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 74

                                ListView {
                                    id: fcList
                                    anchors.fill: parent
                                    orientation: ListView.Horizontal
                                    clip: true
                                    spacing: 6
                                    boundsBehavior: Flickable.StopAtBounds
                                    model: page.wx.dailyForecast ?? []

                                    QQC2.ScrollBar.horizontal: QQC2.ScrollBar {
                                        policy: QQC2.ScrollBar.AsNeeded
                                        height: 6
                                    }

                                    WheelHandler {
                                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                                        onWheel: event => {
                                            const delta = Math.abs(event.pixelDelta.x) > 0
                                                        ? -event.pixelDelta.x
                                                        : (event.angleDelta.x !== 0 ? -event.angleDelta.x : -event.angleDelta.y);
                                            const maxX = Math.max(0, fcList.contentWidth - fcList.width);
                                            fcList.contentX = Math.max(0, Math.min(maxX, fcList.contentX + delta * 0.55));
                                            event.accepted = true;
                                        }
                                    }

                                    delegate: Rectangle {
                                        required property var modelData
                                        width: Kirigami.Units.gridUnit * 4.5
                                        height: 64
                                        radius: Kirigami.Units.cornerRadius
                                        color: modelData.isToday
                                               ? Qt.alpha(Kirigami.Theme.highlightColor, 0.12)
                                               : (fcHover.hovered ? Kirigami.Theme.backgroundColor : Kirigami.Theme.alternateBackgroundColor)
                                        border.width: 1
                                        border.color: modelData.isToday
                                                      ? Qt.alpha(Kirigami.Theme.highlightColor, 0.45)
                                                      : Qt.alpha(Kirigami.Theme.textColor, 0.08)

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 4
                                            spacing: 1

                                            RowLayout {
                                                Layout.alignment: Qt.AlignHCenter
                                                spacing: 3
                                                QQC2.Label {
                                                    text: modelData.dayLabel
                                                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                                                    font.bold: true
                                                }
                                                QQC2.Label {
                                                    visible: !modelData.isToday && (modelData.dateShort ?? "") !== ""
                                                    text: modelData.dateShort ?? ""
                                                    font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.85
                                                    color: Kirigami.Theme.disabledTextColor
                                                }
                                            }

                                            RowLayout {
                                                Layout.alignment: Qt.AlignHCenter
                                                spacing: 3
                                                Kirigami.Icon {
                                                    source: modelData.iconName
                                                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                                    Layout.preferredHeight: Layout.preferredWidth
                                                }
                                                QQC2.Label {
                                                    visible: modelData.rainProb > 0
                                                    text: modelData.rainProb + "%"
                                                    font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.85
                                                    color: "#3daee9"
                                                }
                                            }

                                            QQC2.Label {
                                                Layout.alignment: Qt.AlignHCenter
                                                text: modelData.minTemp + "° / " + modelData.maxTemp + "°"
                                                font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.92
                                            }
                                        }

                                        HoverHandler { id: fcHover }
                                        QQC2.ToolTip.text: (modelData.condition ?? "") + (modelData.rainProb > 0 ? " · " + modelData.rainProb + "%" : "")
                                        QQC2.ToolTip.visible: fcHover.hovered
                                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                    }
                                }
                            }

                            QQC2.ToolButton {
                                icon.name: "go-next"
                                visible: fcList.contentWidth > fcList.width
                                enabled: fcList.contentX + fcList.width < fcList.contentWidth - 2
                                Accessible.name: i18nc("@action:button", "Next days")
                                onClicked: fcList.contentX = Math.min(Math.max(0, fcList.contentWidth - fcList.width), fcList.contentX + fcList.width * 0.65)
                            }
                        }
                    }
                }
            }

            // ---------- Two-Column Core Sections ----------
            GridLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                columns: page.width < Kirigami.Units.gridUnit * 48 ? 1 : 2
                columnSpacing: Kirigami.Units.largeSpacing
                rowSpacing: Kirigami.Units.largeSpacing

                // LEFT COLUMN: Priorities + Journal + Insights
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.largeSpacing

                    DashCard {
                        cardTitle: i18nc("@title:group", "Priorities & Upcoming Tasks")
                        cardSubtitle: (page.st.triageCount ?? 0) > 0
                                      ? i18ncp("card sub triage", "Ranked urgent/important tasks · %1 awaits Matrix triage", "Ranked urgent/important tasks · %1 await Matrix triage", page.st.triageCount ?? 0)
                                      : i18nc("card sub", "Overdue, due soon and Q1/Q2 tasks")
                        actionLabel: (page.st.topTaskId ?? 0) > 0 ? i18nc("@action:button", "Focus #1")
                                   : (page.st.triageCount ?? 0) > 0 ? i18nc("@action:button", "Triage (%1)", page.st.triageCount ?? 0)
                                   : ""
                        actionIcon: (page.st.topTaskId ?? 0) > 0 ? "media-playback-start" : "view-grid"
                        onActionClicked: {
                            if ((page.st.topTaskId ?? 0) > 0) {
                                App.focus.startPomodoro(page.st.topTaskId);
                            } else {
                                page.navigateRequested("matrix");
                            }
                        }

                        QQC2.Label {
                            visible: App.summary.priorityTasks.length === 0
                            Layout.fillWidth: true
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.Wrap
                            text: i18nc("empty priorities", "You are all caught up! No urgent or due tasks waiting.")
                        }

                        Repeater {
                            model: App.summary.priorityTasks
                            delegate: Rectangle {
                                id: ptRow
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: ptContent.implicitHeight + Kirigami.Units.smallSpacing * 1.4
                                radius: Kirigami.Units.cornerRadius
                                color: ptHover.hovered ? Kirigami.Theme.alternateBackgroundColor : "transparent"
                                border.width: 1
                                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.08)

                                RowLayout {
                                    id: ptContent
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    QQC2.CheckBox {
                                        enabled: !modelData.readOnly
                                        onToggled: App.setDone(modelData.id, true)
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            text: modelData.title
                                            textFormat: Text.PlainText
                                            elide: Text.ElideRight
                                            font.weight: Font.DemiBold
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            Chip {
                                                visible: modelData.quadrant >= 1 && modelData.quadrant <= 4
                                                text: "Q" + modelData.quadrant
                                                filled: modelData.quadrant === 1
                                                tint: page.quadrantColor(modelData.quadrant)
                                            }
                                            Chip {
                                                visible: modelData.dueText !== ""
                                                text: modelData.dueText
                                                iconName: "view-calendar-day"
                                                filled: modelData.overdue
                                                tint: modelData.overdue ? Kirigami.Theme.negativeTextColor : "#38bdf8"
                                            }
                                            Chip {
                                                visible: modelData.listName !== ""
                                                text: modelData.listName
                                                tint: modelData.listColor !== "" ? modelData.listColor : "#607d8b"
                                            }
                                            Chip {
                                                visible: modelData.trackedText !== ""
                                                text: modelData.trackedText
                                                iconName: "chronometer-lap"
                                                tint: "#3daee9"
                                            }
                                        }
                                    }

                                    QQC2.ToolButton {
                                        icon.name: "media-playback-start"
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Start Pomodoro Focus")
                                        QQC2.ToolTip.visible: hovered
                                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                        onClicked: App.focus.startPomodoro(modelData.id)
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    anchors.leftMargin: 36
                                    anchors.rightMargin: 36
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.taskActivated(modelData.id)
                                }
                                HoverHandler { id: ptHover }
                            }
                        }
                    }

                    // ---------- Journal & Recent Notes Card ----------
                    DashCard {
                        cardTitle: i18nc("@title:group", "Journal & Quick Notes")
                        cardSubtitle: i18nc("card sub journal", "Latest entries from your notebooks")
                        actionLabel: i18nc("@action:button", "Today's Note")
                        actionIcon: "story-editor"
                        onActionClicked: {
                            const id = App.summary.openTodayJournal();
                            if (id > 0) {
                                page.journalRequested(id, true);
                            } else {
                                page.navigateRequested("journal");
                            }
                        }

                        QQC2.Label {
                            visible: App.summary.recentJournals.length === 0
                            Layout.fillWidth: true
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.Wrap
                            text: i18nc("empty journal summary", "No journal notes yet. Click “Today's Note” to record ideas or reflect on your day.")
                        }

                        Repeater {
                            model: App.summary.recentJournals
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: jRow.implicitHeight + Kirigami.Units.smallSpacing * 1.4
                                radius: Kirigami.Units.cornerRadius
                                color: jHover.hovered ? Kirigami.Theme.alternateBackgroundColor : "transparent"
                                border.width: 1
                                border.color: Qt.alpha(Kirigami.Theme.textColor, 0.08)

                                RowLayout {
                                    id: jRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    Rectangle {
                                        Layout.preferredWidth: 4
                                        Layout.preferredHeight: 28
                                        radius: 2
                                        color: modelData.notebookColor !== "" ? modelData.notebookColor : "#3daee9"
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 6
                                            QQC2.Label {
                                                Layout.fillWidth: true
                                                text: modelData.title
                                                textFormat: Text.PlainText
                                                elide: Text.ElideRight
                                                font.weight: Font.DemiBold
                                            }
                                            Kirigami.Icon {
                                                visible: !!modelData.locked && !modelData.unlocked
                                                source: "object-locked"
                                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                                Layout.preferredHeight: Layout.preferredWidth
                                                color: Kirigami.Theme.disabledTextColor
                                            }
                                            Chip {
                                                text: modelData.entryDateText
                                                tint: Kirigami.Theme.disabledTextColor
                                            }
                                            Chip {
                                                visible: (modelData.mood ?? "") !== ""
                                                text: modelData.mood ?? ""
                                                tint: "#9b59b6"
                                            }
                                        }

                                        QQC2.Label {
                                            visible: !modelData.locked || !!modelData.unlocked
                                            Layout.fillWidth: true
                                            text: (modelData.excerpt ?? "") !== "" ? modelData.excerpt : (modelData.notebookName ?? "")
                                            textFormat: Text.PlainText
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: page.journalRequested(modelData.id, false)
                                }
                                HoverHandler { id: jHover }
                            }
                        }
                    }

                    // ---------- Insights Snapshot Card ----------
                    DashCard {
                        cardTitle: i18nc("@title:group", "Insights Snapshot")

                        QQC2.Label {
                            visible: App.summary.topInsights.length === 0
                            Layout.fillWidth: true
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.Wrap
                            text: i18nc("empty insights", "Complete tasks or run Pomodoro sessions to generate productivity insights.")
                        }

                        Repeater {
                            model: App.summary.topInsights
                            delegate: Rectangle {
                                required property var modelData
                                readonly property color tone: modelData.severity === "positive" ? Kirigami.Theme.positiveTextColor
                                                            : modelData.severity === "warning" ? Kirigami.Theme.negativeTextColor
                                                            : Kirigami.Theme.highlightColor
                                Layout.fillWidth: true
                                implicitHeight: iCol.implicitHeight + Kirigami.Units.smallSpacing * 1.6
                                radius: Kirigami.Units.cornerRadius
                                color: Qt.alpha(tone, 0.08)
                                border.width: 1
                                border.color: Qt.alpha(tone, 0.22)

                                RowLayout {
                                    id: iCol
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    Kirigami.Icon {
                                        source: modelData.icon
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
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
                                }
                            }
                        }
                    }
                }

                // RIGHT COLUMN: Today's Routine (+ Next Up / Free Time) & External Agenda
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Kirigami.Units.largeSpacing

                    DashCard {
                        cardTitle: i18nc("@title:group", "Today's Routine")
                        cardSubtitle: i18nc("card sub", "Time-blocked habits & planned blocks")

                        // Spotlight: Next Up / Current block / Free window / Overlap alert
                        Rectangle {
                            visible: !!page.sched.available
                            Layout.fillWidth: true
                            implicitHeight: schedRow.implicitHeight + Kirigami.Units.smallSpacing * 1.4
                            radius: Kirigami.Units.cornerRadius
                            color: page.sched.isConflict
                                   ? Qt.alpha(Kirigami.Theme.negativeTextColor, 0.09)
                                   : Qt.alpha(page.sched.isNow ? "#27ae60" : Kirigami.Theme.highlightColor, 0.09)
                            border.width: 1
                            border.color: page.sched.isConflict
                                          ? Qt.alpha(Kirigami.Theme.negativeTextColor, 0.30)
                                          : Qt.alpha(page.sched.isNow ? "#27ae60" : Kirigami.Theme.highlightColor, 0.25)

                            RowLayout {
                                id: schedRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: Kirigami.Units.smallSpacing
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: page.sched.isConflict ? "data-warning"
                                          : page.sched.isNow ? "chronometer" : "view-calendar-agenda"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: page.sched.isConflict ? Kirigami.Theme.negativeTextColor
                                         : page.sched.isNow ? "#27ae60" : Kirigami.Theme.highlightColor
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Chip {
                                            text: page.sched.badgeText ?? ""
                                            filled: !!page.sched.isNow
                                            tint: page.sched.isNow ? "#27ae60" : Kirigami.Theme.highlightColor
                                        }
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            text: (page.sched.title ?? "") + " (" + (page.sched.timeRange ?? "") + ")"
                                            textFormat: Text.PlainText
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                    }
                                    QQC2.Label {
                                        visible: (page.sched.freeWindowText ?? "") !== "" || !!page.sched.isConflict
                                        Layout.fillWidth: true
                                        font: Kirigami.Theme.smallFont
                                        color: page.sched.isConflict ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                                        text: page.sched.isConflict
                                              ? i18nc("schedule conflict note", "Schedule overlap detected between Routine and External Agenda below.")
                                              : (page.sched.freeWindowText ?? "")
                                        wrapMode: Text.Wrap
                                    }
                                }
                            }
                        }

                        QQC2.Label {
                            visible: App.summary.todayRoutines.length === 0
                            Layout.fillWidth: true
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.Wrap
                            text: i18nc("empty routine", "No routine blocks scheduled for today.")
                        }

                        Repeater {
                            model: App.summary.todayRoutines
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Rectangle {
                                    Layout.preferredWidth: 4
                                    Layout.preferredHeight: 22
                                    radius: 2
                                    color: modelData.color !== "" ? modelData.color : "#3daee9"
                                }
                                QQC2.CheckBox {
                                    checked: modelData.status === 1
                                    onToggled: App.routine.setBlockStatus(modelData.id, checked ? 1 : 0)
                                }
                                Chip {
                                    text: modelData.timeRange
                                    tint: modelData.color !== "" ? modelData.color : "#3daee9"
                                }
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: modelData.title
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight
                                    font.strikeout: modelData.status === 1 || modelData.status === 2
                                    opacity: modelData.status === 0 ? 1.0 : 0.6
                                }
                                Kirigami.Icon {
                                    visible: !!modelData.hasConflict
                                    source: "data-warning"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Layout.preferredWidth
                                    color: Kirigami.Theme.negativeTextColor
                                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Overlaps external event: %1", modelData.conflictWith ?? "")
                                    QQC2.ToolTip.visible: confHover.hovered
                                    HoverHandler { id: confHover }
                                }
                            }
                        }
                    }

                    // ---------- External Agenda (KOrganizer / Google / Akonadi / .ics) ----------
                    DashCard {
                        cardTitle: i18nc("@title:group", "External Agenda · Appointments")
                        cardSubtitle: App.summary.externalCalStatus
                        actionLabel: i18nc("@action:button", "Configure")
                        actionIcon: "configure"
                        actionIconOnly: true
                        onActionClicked: page.settingsRequested()

                        QQC2.Label {
                            visible: App.summary.externalEvents.length === 0
                            Layout.fillWidth: true
                            color: Kirigami.Theme.disabledTextColor
                            wrapMode: Text.Wrap
                            text: App.supportsLinuxDesktop
                                  ? i18nc("empty external cal", "No appointments found for the next 3 days across your enabled KOrganizer / desktop or .ics calendars.")
                                  : i18nc("empty external cal", "No appointments found for the next 3 days across your added .ics calendars.")
                        }

                        Repeater {
                            model: App.summary.externalEvents
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: evRow.implicitHeight + Kirigami.Units.smallSpacing * 1.4
                                radius: Kirigami.Units.cornerRadius
                                color: modelData.hasConflict
                                       ? Qt.alpha(Kirigami.Theme.negativeTextColor, 0.08)
                                       : Qt.alpha(Kirigami.Theme.alternateBackgroundColor, 0.55)
                                border.width: 1
                                border.color: modelData.hasConflict
                                              ? Qt.alpha(Kirigami.Theme.negativeTextColor, 0.35)
                                              : Qt.alpha(Kirigami.Theme.textColor, 0.08)
                                opacity: modelData.isPast ? 0.6 : 1.0

                                RowLayout {
                                    id: evRow
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    spacing: Kirigami.Units.smallSpacing

                                    Kirigami.Icon {
                                        source: modelData.hasConflict ? "data-warning" : "view-calendar-agenda"
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                        Layout.preferredHeight: Layout.preferredWidth
                                        color: modelData.hasConflict ? Kirigami.Theme.negativeTextColor
                                             : modelData.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            text: modelData.title
                                            textFormat: Text.PlainText
                                            elide: Text.ElideRight
                                            font.weight: Font.DemiBold
                                        }
                                        Flow {
                                            Layout.fillWidth: true
                                            spacing: 4
                                            Chip {
                                                text: modelData.dateLabel + " · " + modelData.timeRange
                                                filled: modelData.isToday && !modelData.isPast
                                                tint: modelData.isToday ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                                            }
                                            Chip {
                                                visible: (modelData.remainingText ?? "") !== ""
                                                text: modelData.remainingText ?? ""
                                                tint: "#27ae60"
                                            }
                                            Chip {
                                                text: modelData.sourceName
                                                tint: "#14b8a6"
                                            }
                                            Chip {
                                                visible: !!modelData.hasConflict
                                                text: i18nc("event routine overlap", "Overlaps: %1", modelData.conflictTitle ?? "")
                                                tint: Kirigami.Theme.negativeTextColor
                                            }
                                            QQC2.Label {
                                                visible: (modelData.location ?? "") !== ""
                                                text: modelData.location ?? ""
                                                textFormat: Text.PlainText
                                                font: Kirigami.Theme.smallFont
                                                color: Kirigami.Theme.disabledTextColor
                                            }
                                        }
                                    }

                                    QQC2.ToolButton {
                                        icon.name: "task-new"
                                        Accessible.name: i18nc("@action:button", "Create prep task")
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Create preparation task for this appointment")
                                        QQC2.ToolTip.visible: hovered
                                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                        onClicked: App.summary.createPrepTaskFromEvent(
                                            modelData.title,
                                            modelData.dateIso ?? "",
                                            modelData.startTimeText ?? "",
                                            modelData.sourceName ?? "")
                                    }

                                    QQC2.ToolButton {
                                        icon.name: "media-playback-start"
                                        Accessible.name: i18nc("@action:button", "Start Pomodoro for this appointment")
                                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Start Pomodoro focus for this appointment")
                                        QQC2.ToolTip.visible: hovered
                                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                        onClicked: App.focus.startPomodoroLabeled(modelData.title)
                                    }
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
