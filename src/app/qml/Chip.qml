// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

// A contrast-aware label pill used for task metadata and smart entry snippets.
Rectangle {
    id: chip

    property string text
    property string iconName
    property color tint: Kirigami.Theme.textColor
    property bool filled: false

    readonly property color bgColor: Kirigami.Theme.backgroundColor
    readonly property bool darkBg: (0.2126 * bgColor.r + 0.7152 * bgColor.g + 0.0722 * bgColor.b) < 0.5
    readonly property color effectiveTint: App.adaptiveChipColor(tint, bgColor)

    implicitHeight: row.implicitHeight + Kirigami.Units.smallSpacing
    implicitWidth: row.implicitWidth + Kirigami.Units.smallSpacing * 3
    radius: height / 2
    color: filled ? Qt.alpha(effectiveTint, darkBg ? 0.25 : 0.16) : "transparent"
    border.width: 1
    border.color: Qt.alpha(effectiveTint, filled ? (darkBg ? 0.55 : 0.42) : (darkBg ? 0.45 : 0.35))

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Kirigami.Units.smallSpacing / 2

        Kirigami.Icon {
            visible: chip.iconName !== ""
            source: chip.iconName
            color: chip.effectiveTint
            isMask: true
            Layout.preferredWidth: Kirigami.Units.iconSizes.small * 0.75
            Layout.preferredHeight: Layout.preferredWidth
        }
        QQC2.Label {
            text: chip.text
            color: chip.effectiveTint
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            font.weight: chip.filled ? Font.Medium : Font.Normal
            visible: text !== ""
        }
    }
}
