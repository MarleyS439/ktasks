// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Hours + Minutes duration selector backed by a single totalMinutes integer.
RowLayout {
    id: root

    property int totalMinutes: 0
    property int minMinutes: 0
    property int maxMinutes: 1440
    property int minuteStep: 5

    signal durationModified(int minutes)

    readonly property int maxHours: Math.floor(Math.max(0, maxMinutes) / 60)

    spacing: Kirigami.Units.smallSpacing

    function clampedMinutes(raw) {
        return Math.max(minMinutes, Math.min(maxMinutes, raw));
    }

    function syncSpinners(mins) {
        const clamped = clampedMinutes(mins);
        const h = Math.floor(clamped / 60);
        const m = clamped % 60;
        if (hourSpin.value !== h) {
            hourSpin.value = h;
        }
        if (minSpin.value !== m) {
            minSpin.value = m;
        }
    }

    function commitFromSpinners() {
        const next = clampedMinutes(hourSpin.value * 60 + minSpin.value);
        syncSpinners(next);
        if (totalMinutes !== next) {
            totalMinutes = next;
        }
        durationModified(next);
    }

    Component.onCompleted: syncSpinners(totalMinutes)
    onTotalMinutesChanged: syncSpinners(totalMinutes)

    PaddedSpinBox {
        id: hourSpin
        minWidthUnits: 6
        from: 0
        to: root.maxHours
        textFromValue: v => i18nc("duration hours short", "%1 h", v)
        valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
        onValueModified: root.commitFromSpinners()
        Accessible.name: i18nc("@label:spinbox", "Hours")
    }

    QQC2.Label {
        text: ":"
        font.bold: true
    }

    PaddedSpinBox {
        id: minSpin
        minWidthUnits: 6
        from: 0
        to: 59
        stepSize: root.minuteStep
        textFromValue: v => i18nc("duration minutes short", "%1 min", String(v).padStart(2, "0"))
        valueFromText: t => parseInt(t.replace(/\D/g, "")) || 0
        onValueModified: root.commitFromSpinners()
        Accessible.name: i18nc("@label:spinbox", "Minutes")
    }
}
