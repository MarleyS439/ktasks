// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// SpinBox with generous internal padding so the value never crowds the '-' and '+' buttons.
QQC2.SpinBox {
    id: control

    property int minWidthUnits: 6

    implicitWidth: Math.max(Kirigami.Units.gridUnit * minWidthUnits,
                            (contentItem ? contentItem.implicitWidth : 40) + leftPadding + rightPadding + Kirigami.Units.largeSpacing * 2)
    Layout.minimumWidth: Kirigami.Units.gridUnit * minWidthUnits
    editable: true
    padding: Kirigami.Units.smallSpacing
    leftPadding: (down.indicator ? down.indicator.width : 28) + Kirigami.Units.smallSpacing
    rightPadding: (up.indicator ? up.indicator.width : 28) + Kirigami.Units.smallSpacing

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        font: control.font
        color: Kirigami.Theme.textColor
        selectionColor: Kirigami.Theme.highlightColor
        selectedTextColor: Kirigami.Theme.highlightedTextColor
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        leftPadding: Kirigami.Units.smallSpacing
        rightPadding: Kirigami.Units.smallSpacing
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
    }
}
