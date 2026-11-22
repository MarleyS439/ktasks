// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

Kirigami.Dialog {
    id: dialog

    property string defaultScope: "today"
    property var journalId: 0

    function openFor(scope, jId) {
        defaultScope = scope || "today";
        journalId = jId || 0;
        const scopes = ["today", "week", "overdue", "current", "matrix", "routine", "journal"];
        const idx = scopes.indexOf(defaultScope);
        scopeCombo.currentIndex = idx >= 0 ? idx : 0;
        groupByDayCheck.checked = (defaultScope === "week");
        open();
    }

    function currentOptions() {
        const scopes = ["today", "week", "overdue", "current", "matrix", "routine", "journal"];
        return {
            scope: scopes[scopeCombo.currentIndex],
            includeCompleted: completedCheck.checked,
            includeSubtasks: subtasksCheck.checked,
            includeNotes: notesCheck.checked,
            groupByDay: groupByDayCheck.checked,
            journalId: dialog.journalId
        };
    }

    title: i18nc("@title:window", "Print & Export PDF")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 24
    standardButtons: Kirigami.Dialog.Close

    customFooterActions: [
        Kirigami.Action {
            icon.name: "document-preview"
            text: i18nc("@action:button", "Preview…")
            onTriggered: {
                const opts = dialog.currentOptions();
                dialog.close();
                App.pdf.printPreview(opts);
            }
        },
        Kirigami.Action {
            icon.name: "document-print"
            text: i18nc("@action:button", "Print…")
            onTriggered: {
                const opts = dialog.currentOptions();
                dialog.close();
                App.pdf.print(opts);
            }
        },
        Kirigami.Action {
            icon.name: "application-pdf"
            text: i18nc("@action:button", "Export PDF…")
            onTriggered: pdfSaveDialog.open()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.ComboBox {
                id: scopeCombo
                Kirigami.FormData.label: i18nc("@label:listbox", "Content:")
                Layout.fillWidth: true
                model: [
                    i18nc("print scope", "Today's Tasks"),
                    i18nc("print scope", "Next 7 Days (Week)"),
                    i18nc("print scope", "Overdue Tasks"),
                    i18nc("print scope", "Current View / List"),
                    i18nc("print scope", "Priority Matrix (Eisenhower)"),
                    i18nc("print scope", "Daily Routine"),
                    i18nc("print scope", "Current Journal Entry")
                ]
                onActivated: index => {
                    if (index === 1) {
                        groupByDayCheck.checked = true;
                    }
                }
            }
            QQC2.CheckBox {
                id: groupByDayCheck
                visible: scopeCombo.currentIndex < 4
                text: i18nc("@option:check", "Group tasks by day")
            }
            QQC2.CheckBox {
                id: completedCheck
                visible: scopeCombo.currentIndex <= 4
                text: i18nc("@option:check", "Include completed tasks")
            }
            QQC2.CheckBox {
                id: subtasksCheck
                visible: scopeCombo.currentIndex < 4
                checked: true
                text: i18nc("@option:check", "Include subtasks")
            }
            QQC2.CheckBox {
                id: notesCheck
                visible: scopeCombo.currentIndex < 4
                checked: true
                text: i18nc("@option:check", "Include notes")
            }
        }
    }

    Dialogs.FileDialog {
        id: pdfSaveDialog
        title: i18nc("@title:window", "Export PDF")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: [i18nc("file type", "PDF files (*.pdf)")]
        onAccepted: {
            App.pdf.exportPdf(selectedFile, dialog.currentOptions());
            dialog.close();
        }
    }
}
