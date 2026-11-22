// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

GuardedDialog {
    id: dialog

    property var listId: 0
    property var info: ({})
    property string color: ""
    property var accounts: []
    property string _initialState: ""

    function currentState() {
        return {
            name: nameField.text,
            color: color,
            accIdx: accountCombo.currentIndex
        };
    }

    dirty: opened && JSON.stringify(currentState()) !== _initialState
    canSave: nameField.text.trim() !== ""
    saveActionText: listId > 0 ? i18nc("@action:button", "Save") : i18nc("@action:button", "Create")
    onSaveRequested: save()

    function openFor(id) {
        listId = id;
        info = id > 0 ? App.listInfo(id) : ({});
        nameField.text = info.name ?? "";
        color = info.color ?? "";
        accounts = [{ id: 0, name: i18nc("where a list is stored", "On this device") }].concat(App.sync.accounts);
        const curAcc = info.accountId ?? 0;
        accountCombo.currentIndex = Math.max(0, accounts.findIndex(a => a.id === curAcc));
        _initialState = JSON.stringify(currentState());
        open();
        nameField.forceActiveFocus();
    }
    function save() {
        if (nameField.text.trim() === "") {
            return;
        }
        const targetAccId = (accountCombo.currentIndex >= 0 && accountCombo.currentIndex < accounts.length)
            ? accounts[accountCombo.currentIndex].id : (info.accountId ?? 0);
        if (listId > 0) {
            App.updateList(listId, nameField.text, color);
            if (!info.readOnly && targetAccId !== (info.accountId ?? 0)) {
                App.moveListToAccount(listId, targetAccId);
            }
        } else {
            const id = App.addList(nameField.text, color, targetAccId);
            if (id > 0) {
                App.tasks.searchText = "";
                App.tasks.view = "list:" + id;
            }
        }
        forceClose();
    }

    title: listId > 0 ? i18nc("@title:window", "Edit List") : i18nc("@title:window", "New List")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 22
    standardButtons: Kirigami.Dialog.NoButton

    customFooterActions: [
        Kirigami.Action {
            visible: dialog.listId > 0
            icon.name: "edit-delete"
            text: i18nc("@action:button", "Delete List…")
            onTriggered: confirmDelete.open()
        },
        Kirigami.Action {
            icon.name: "dialog-cancel"
            text: i18nc("@action:button", "Cancel")
            onTriggered: dialog.reject()
        },
        Kirigami.Action {
            icon.name: "dialog-ok"
            text: dialog.listId > 0 ? i18nc("@action:button", "Save") : i18nc("@action:button", "Create")
            enabled: nameField.text.trim() !== ""
            onTriggered: dialog.save()
        }
    ]

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.FormLayout {
            Layout.fillWidth: true

            QQC2.TextField {
                id: nameField
                Kirigami.FormData.label: i18nc("@label:textbox", "Name:")
                Layout.fillWidth: true
                onAccepted: dialog.save()
            }
            QQC2.ComboBox {
                id: accountCombo
                Kirigami.FormData.label: i18nc("@label:listbox", "Stored in:")
                Layout.fillWidth: true
                visible: !dialog.info.readOnly && dialog.accounts.length > 1
                model: dialog.accounts
                textRole: "name"
            }
            QQC2.Label {
                Kirigami.FormData.label: i18nc("@label", "Stored in:")
                visible: dialog.listId > 0 && !!dialog.info.readOnly
                text: (dialog.info.account ?? "") !== "" ? dialog.info.account : i18nc("where a list is stored", "On this device")
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.neutralTextColor
                visible: dialog.listId > 0 && (dialog.info.accountId ?? 0) > 0
                         && accountCombo.currentIndex >= 0 && accountCombo.currentIndex < dialog.accounts.length
                         && dialog.accounts[accountCombo.currentIndex].id === 0
                text: i18nc("@info", "Converting to local keeps all tasks on this device and removes the copy from the Nextcloud server.")
            }
            Flow {
                Kirigami.FormData.label: i18nc("@label", "Color:")
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: ["", "#e93d58", "#f67400", "#fdbc4b", "#27ae60", "#1abc9c", "#3daee9", "#2980b9", "#9b59b6", "#e056a0", "#7f8c8d"]
                    Rectangle {
                        required property string modelData
                        readonly property bool current: dialog.color.toLowerCase() === modelData
                        width: Kirigami.Units.iconSizes.medium
                        height: width
                        radius: width / 2
                        color: modelData !== "" ? modelData : "transparent"
                        border.width: current ? 3 : 1
                        border.color: current ? Kirigami.Theme.highlightColor : Kirigami.Theme.disabledTextColor
                        Accessible.role: Accessible.RadioButton
                        Accessible.name: modelData !== "" ? modelData : i18nc("@option:radio", "No color")

                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: Kirigami.Units.iconSizes.small
                            height: width
                            visible: parent.modelData === ""
                            source: "edit-none"
                        }
                        TapHandler {
                            onTapped: dialog.color = parent.modelData
                        }
                    }
                }
            }
        }
    }

    Kirigami.PromptDialog {
        id: confirmDelete
        title: i18nc("@title:window", "Delete List?")
        subtitle: (dialog.info.account ?? "") !== ""
                  ? i18ncp("@info", "“%2” and its %1 task will be deleted here and on the server. This cannot be undone.",
                           "“%2” and its %1 tasks will be deleted here and on the server. This cannot be undone.", dialog.info.taskCount ?? 0, dialog.info.name ?? "")
                  : i18ncp("@info", "“%2” and its %1 task will be deleted. This cannot be undone.", "“%2” and its %1 tasks will be deleted. This cannot be undone.",
                           dialog.info.taskCount ?? 0, dialog.info.name ?? "")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Delete")
                onTriggered: {
                    App.removeList(dialog.listId);
                    confirmDelete.close();
                    dialog.forceClose();
                }
            }
        ]
    }
}
