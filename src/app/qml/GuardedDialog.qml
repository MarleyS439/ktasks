// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

/*!
 * Modal edit dialog that asks whether to save or discard unsaved changes when
 * dismissed via Escape, outside click, Cancel, or header close button, and closes
 * immediately when no modifications were made.
 */
Kirigami.Dialog {
    id: root
    focus: true

    property bool dirty: false
    property bool canSave: true
    property string discardPromptTitle: i18nc("@title:window", "Save changes?")
    property string discardPromptSubtitle: i18nc("@info", "You have unsaved changes. Would you like to save them before closing?")
    property string saveActionText: i18nc("@action:button", "Save")

    signal saveRequested()
    signal discardRequested()

    property bool _bypassGuard: false

    function _reopenWithPrompt() {
        if (!root.dirty || root._bypassGuard) {
            return;
        }
        if (!root.opened) {
            root.open();
        }
        if (!unsavedPrompt.opened) {
            unsavedPrompt.open();
        }
    }

    function forceClose() {
        _bypassGuard = true;
        close();
        _bypassGuard = false;
    }

    function _focusContent() {
        root.forceActiveFocus();
        if (root.contentItem) {
            const first = root.contentItem.nextItemInFocusChain(true);
            if (first && first !== root.contentItem) {
                first.forceActiveFocus();
                return;
            }
        }
        if (root.footer) {
            root.footer.forceActiveFocus();
        }
    }

    onClosed: _bypassGuard = false

    onRejected: {
        if (root.dirty && !root._bypassGuard) {
            Qt.callLater(root._reopenWithPrompt);
        }
    }

    Kirigami.PromptDialog {
        id: unsavedPrompt
        objectName: "unsavedPrompt"
        focus: true
        onAboutToHide: {
            if (root.opened) {
                Qt.callLater(root._focusContent);
            }
        }
        onClosed: {
            if (root.opened) {
                Qt.callLater(root._focusContent);
            }
        }
        title: root.discardPromptTitle
        subtitle: root.discardPromptSubtitle
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "dialog-ok"
                text: root.saveActionText
                enabled: root.canSave
                onTriggered: {
                    unsavedPrompt.close();
                    root._bypassGuard = true;
                    root.saveRequested();
                    root._bypassGuard = false;
                }
            },
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Discard")
                onTriggered: {
                    unsavedPrompt.close();
                    root.discardRequested();
                    root.forceClose();
                }
            }
        ]
    }
}
