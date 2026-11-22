// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.dateandtime as DateTime
import app.ktasks.backend

Item {
    id: page

    property var currentEntryId: 0
    property bool editMode: false
    signal printEntryRequested(var entryId)

    readonly property var currentEntry: {
        entryRevision;
        currentEntryId;
        return currentEntryId > 0 ? App.journal.entry(currentEntryId) : ({ exists: false });
    }
    property int entryRevision: 0

    readonly property string pageTitle: i18nc("@title", "Journal")
    // Top bar holds only collection/global actions; single-page actions live in the open note header.
    property list<Kirigami.Action> pageActions: [
        Kirigami.Action {
            icon.name: "list-add"
            text: i18nc("@action:button", "New Page")
            onTriggered: page.createNewAndFocus()
        },
        Kirigami.Action {
            icon.name: "address-book-new"
            text: i18nc("@action:button", "New Notebook")
            onTriggered: notebookDialog.openCreate()
        },
        Kirigami.Action {
            icon.name: "view-refresh"
            text: i18nc("@action:button", "Sync Notes")
            visible: App.sync.hasAccounts
            enabled: !App.sync.syncing
            onTriggered: App.sync.syncNotesNow()
        },
        Kirigami.Action {
            icon.name: "object-locked"
            text: i18nc("@action:button", "Lock All")
            visible: App.journal.hasUnlockedProtected
            onTriggered: {
                page.flushContent();
                page.editMode = false;
                App.journal.lockAll();
            }
        },
        Kirigami.Action {
            icon.name: "document-print"
            text: i18nc("@action:button", "Export PDF…")
            visible: !!page.currentEntry.exists && (!page.currentEntry.locked || !!page.currentEntry.unlocked)
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: page.printEntryRequested(page.currentEntryId)
        }
    ]

    function openRenameDialog() {
        if (!currentEntry.exists) {
            return;
        }
        renameField.text = currentEntry.title ?? "";
        renameDialog.open();
        renameField.forceActiveFocus();
        renameField.selectAll();
    }

    function closeEntry() {
        singleClickToggleTimer.stop();
        flushContent();
        editMode = false;
        currentEntryId = 0;
        entryRevision++;
        loadEntry();
    }

    function openEntryReadOnly(id) {
        if (currentEntryId !== id) {
            selectEntry(id);
        }
        editMode = false;
    }

    function openEntryForEditing(id) {
        if (currentEntryId !== id) {
            selectEntry(id);
        }
        const e = App.journal.entry(id);
        if (!e.exists) {
            return;
        }
        if (e.locked && !e.unlocked) {
            unlockDialog.openForEntry(id);
        } else {
            editMode = true;
            mdEditor.viewMode = 0;
            Qt.callLater(() => mdEditor.editor.forceActiveFocus());
        }
    }

    function finishEditing() {
        flushTitle();
        flushContent();
        editMode = false;
    }

    function createNewAndFocus() {
        flushContent();
        const id = App.journal.createEntry();
        if (id > 0) {
            entryRevision++;
            selectEntry(id);
            editMode = true;
            mdEditor.viewMode = 0;
            Qt.callLater(() => {
                mdEditor.editor.forceActiveFocus();
                mdEditor.editor.cursorPosition = mdEditor.editor.text.length;
            });
        }
    }

    function selectEntry(id) {
        flushTitle();
        flushContent();
        currentEntryId = id;
        entryRevision++;
        loadEntry();
    }

    function flushTitle() {
        if (currentEntryId > 0 && editMode) {
            const trimmed = titleField.text.trim();
            if (trimmed !== "" && trimmed !== currentEntry.title) {
                App.journal.renameEntry(currentEntryId, trimmed);
            }
        }
    }

    function flushContent() {
        saveTimer.stop();
        if (currentEntryId > 0) {
            const e = App.journal.entry(currentEntryId);
            if (e.exists && (!e.locked || e.unlocked) && mdEditor.text !== e.content) {
                App.journal.updateEntry(currentEntryId, { content: mdEditor.text });
            }
        }
    }

    function loadEntry() {
        const e = currentEntryId > 0 ? App.journal.entry(currentEntryId) : ({ exists: false });
        if (!e.exists) {
            titleField.text = "";
            mdEditor.text = "";
            return;
        }
        titleField.text = e.title;
        mdEditor.text = (!e.locked || e.unlocked) ? e.content : "";
    }

    Component.onDestruction: {
        flushTitle();
        flushContent();
    }

    Timer {
        id: singleClickToggleTimer
        property var targetId: 0
        interval: 220
        onTriggered: {
            if (page.currentEntryId === targetId) {
                page.closeEntry();
            }
        }
    }

    Connections {
        target: App.journal
        function onEntriesChanged() {
            page.entryRevision++;
            if (page.currentEntryId <= 0) {
                return;
            }
            const e = App.journal.entry(page.currentEntryId);
            if (!e.exists) {
                page.editMode = false;
                page.currentEntryId = 0;
                page.loadEntry();
            } else {
                if (!titleField.activeFocus) {
                    titleField.text = e.title;
                }
                if (!mdEditor.editor.activeFocus && !saveTimer.running) {
                    mdEditor.text = (!e.locked || e.unlocked) ? e.content : "";
                }
                if (e.locked && !e.unlocked) {
                    page.editMode = false;
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // -------- Pane 1: Notebooks ("Cadernos") --------
        ColumnLayout {
            Layout.fillWidth: false
            Layout.preferredWidth: page.currentEntryId <= 0 ? Kirigami.Units.gridUnit * 13 : Kirigami.Units.gridUnit * 11
            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
            Layout.maximumWidth: Kirigami.Units.gridUnit * 15
            Layout.fillHeight: true
            visible: page.width >= Kirigami.Units.gridUnit * (page.currentEntryId > 0 ? 48 : 34)
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 4
                    text: i18nc("@title", "Notebooks")
                }
                QQC2.ToolButton {
                    icon.name: "list-add"
                    QQC2.ToolTip.text: i18nc("@action:button", "New Notebook")
                    QQC2.ToolTip.visible: hovered
                    onClicked: notebookDialog.openCreate()
                }
            }

            Kirigami.Separator { Layout.fillWidth: true }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: [{ id: 0, name: i18nc("all notebooks", "All Notebooks"), color: "", count: -1, synced: false, syncAccountId: 0 }].concat(App.journal.notebooks)

                delegate: QQC2.ItemDelegate {
                    id: nbRow
                    required property var modelData
                    width: ListView.view.width
                    highlighted: App.journal.selectedNotebookId === modelData.id
                    onClicked: {
                        page.flushContent();
                        App.journal.selectedNotebookId = modelData.id;
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        enabled: nbRow.modelData.id > 0
                        onTapped: {
                            nbMenu.nb = nbRow.modelData;
                            nbMenu.popup();
                        }
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        Rectangle {
                            visible: nbRow.modelData.id > 0
                            width: 10
                            height: 10
                            radius: 5
                            color: nbRow.modelData.color || "#3daee9"
                        }
                        Kirigami.Icon {
                            visible: nbRow.modelData.id === 0
                            source: "view-list-details"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: nbRow.modelData.name
                            elide: Text.ElideRight
                            color: nbRow.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        }
                        Kirigami.Icon {
                            visible: !!nbRow.modelData.synced
                            source: "cloud-upload"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.75
                            color: nbRow.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            QQC2.ToolTip.text: i18nc("@info:tooltip", "Synced with Nextcloud Notes")
                        }
                        QQC2.Label {
                            visible: nbRow.modelData.count >= 0
                            text: nbRow.modelData.count
                            font: Kirigami.Theme.smallFont
                            color: nbRow.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            visible: page.width >= Kirigami.Units.gridUnit * (page.currentEntryId > 0 ? 48 : 34)
        }

        // -------- Pane 2: Notebook Pages / Entries --------
        ColumnLayout {
            Layout.fillWidth: page.currentEntryId <= 0
            Layout.preferredWidth: page.currentEntryId <= 0 ? 400
                                 : (page.width >= Kirigami.Units.gridUnit * 48 ? Kirigami.Units.gridUnit * 14 : Kirigami.Units.gridUnit * 12)
            Layout.minimumWidth: Kirigami.Units.gridUnit * 11
            Layout.maximumWidth: page.currentEntryId <= 0 ? 100000 : Kirigami.Units.gridUnit * 16
            Layout.fillHeight: true
            spacing: 0

            // Compact notebook selector when Pane 1 is collapsed
            QQC2.ComboBox {
                visible: page.width < Kirigami.Units.gridUnit * (page.currentEntryId > 0 ? 48 : 34)
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                readonly property var items: [{ id: 0, name: i18nc("all notebooks", "All Notebooks") }].concat(App.journal.notebooks)
                model: items
                textRole: "name"
                currentIndex: Math.max(0, items.findIndex(n => n.id === App.journal.selectedNotebookId))
                onActivated: index => {
                    page.flushContent();
                    App.journal.selectedNotebookId = items[index].id;
                }
            }

            Kirigami.SearchField {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                placeholderText: i18nc("@info:placeholder", "Search pages…")
                text: App.journal.searchText
                onTextChanged: App.journal.searchText = text
            }

            Kirigami.Separator { Layout.fillWidth: true }

            ListView {
                id: entriesList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: App.journal
                clip: true

                delegate: QQC2.ItemDelegate {
                    id: row
                    required property int index
                    required property var entryId
                    required property string title
                    required property string entryDateText
                    required property bool locked
                    required property bool unlocked
                    required property string excerpt
                    required property var tags
                    required property string notebookName
                    required property string notebookColor
                    required property var syncAccountId
                    required property string syncAccountName
                    required property bool synced
                    required property bool syncPending

                    width: entriesList.width
                    highlighted: page.currentEntryId === entryId

                    onClicked: {
                        if (page.currentEntryId === row.entryId) {
                            singleClickToggleTimer.targetId = row.entryId;
                            singleClickToggleTimer.restart();
                        } else {
                            singleClickToggleTimer.stop();
                            page.openEntryReadOnly(row.entryId);
                        }
                    }
                    onDoubleClicked: {
                        singleClickToggleTimer.stop();
                        page.openEntryForEditing(row.entryId);
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            singleClickToggleTimer.stop();
                            if (page.currentEntryId !== row.entryId) {
                                page.openEntryReadOnly(row.entryId);
                            }
                            entryContextMenu.popup();
                        }
                    }

                    contentItem: ColumnLayout {
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            Kirigami.Icon {
                                visible: row.locked
                                source: row.unlocked ? "object-unlocked" : "object-locked"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                color: row.highlighted ? Kirigami.Theme.highlightedTextColor : (row.unlocked ? "#22c55e" : "#f59e0b")
                            }
                            QQC2.Label {
                                Layout.fillWidth: true
                                text: row.title
                                font.bold: true
                                elide: Text.ElideRight
                                color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }
                            Kirigami.Icon {
                                visible: row.synced
                                source: row.syncPending ? "view-refresh" : "cloud-upload"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.75
                                color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing
                            QQC2.Label {
                                text: row.entryDateText
                                font: Kirigami.Theme.smallFont
                                color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                            }
                            Rectangle {
                                visible: row.notebookName !== ""
                                width: nbLbl.implicitWidth + 8
                                height: nbLbl.implicitHeight + 2
                                radius: 3
                                color: Qt.alpha(row.notebookColor || "#3daee9", 0.22)
                                QQC2.Label {
                                    id: nbLbl
                                    anchors.centerIn: parent
                                    text: row.notebookName
                                    font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.85
                                    color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                                }
                            }
                            QQC2.Label {
                                visible: !row.synced
                                text: i18nc("offline badge", "Local")
                                font.pointSize: Kirigami.Theme.smallFont.pointSize * 0.85
                                opacity: 0.65
                                color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.disabledTextColor
                            }
                        }
                        QQC2.Label {
                            Layout.fillWidth: true
                            text: row.excerpt
                            font: Kirigami.Theme.smallFont
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                            elide: Text.ElideRight
                            opacity: 0.8
                            color: row.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                        }
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 2
                    visible: entriesList.count === 0
                    icon.name: "story-editor"
                    text: i18nc("@info:placeholder", "No pages in this notebook")
                    helpfulAction: Kirigami.Action {
                        icon.name: "list-add"
                        text: i18nc("@action:button", "Create First Page")
                        onTriggered: page.createNewAndFocus()
                    }
                }
            }

            Kirigami.Separator { Layout.fillWidth: true }

            QQC2.Label {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                elide: Text.ElideRight
                text: page.currentEntryId <= 0
                    ? i18nc("@info", "Click a page to read • Double-click to edit")
                    : i18nc("@info", "Click selected page again to close • Double-click to edit")
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            visible: pane3.visible
        }

        // -------- Pane 3: Open Page (Read View or Edit Panel; collapsed when closed) --------
        Item {
            id: pane3
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumWidth: Kirigami.Units.gridUnit * 16
            Layout.preferredWidth: Kirigami.Units.gridUnit * 32
            visible: page.currentEntryId > 0 && !!page.currentEntry.exists
            clip: true

            // Locked gate
            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 22)
                visible: !!page.currentEntry.exists && !!page.currentEntry.locked && !page.currentEntry.unlocked
                spacing: Kirigami.Units.largeSpacing

                Kirigami.Icon {
                    Layout.alignment: Qt.AlignHCenter
                    source: "object-locked"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                    Layout.preferredHeight: Kirigami.Units.iconSizes.huge
                    color: Kirigami.Theme.highlightColor
                }
                Kirigami.Heading {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    level: 3
                    text: page.currentEntry.title ?? ""
                    wrapMode: Text.Wrap
                }
                QQC2.Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: i18nc("@info", "This page is encrypted and protected with a password.")
                    wrapMode: Text.Wrap
                    color: Kirigami.Theme.disabledTextColor
                }
                QQC2.TextField {
                    id: unlockPassField
                    Layout.fillWidth: true
                    echoMode: TextInput.Password
                    placeholderText: i18nc("@info:placeholder", "Enter password…")
                    onAccepted: unlockBtn.clicked()
                }
                QQC2.Label {
                    id: unlockErrorLabel
                    Layout.fillWidth: true
                    visible: text !== ""
                    color: Kirigami.Theme.negativeTextColor
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Kirigami.Units.smallSpacing
                    QQC2.Button {
                        id: unlockBtn
                        icon.name: "object-unlocked"
                        text: i18nc("@action:button", "Unlock")
                        onClicked: {
                            if (App.journal.unlockEntry(page.currentEntryId, unlockPassField.text)) {
                                unlockPassField.text = "";
                                unlockErrorLabel.text = "";
                                page.entryRevision++;
                                page.loadEntry();
                            } else {
                                unlockErrorLabel.text = App.journal.lastError();
                            }
                        }
                    }
                    QQC2.Button {
                        icon.name: "edit-delete"
                        text: i18nc("@action:button", "Delete")
                        onClicked: confirmDeleteEntry.open()
                    }
                    QQC2.Button {
                        icon.name: "window-close"
                        text: i18nc("@action:button", "Close")
                        onClicked: page.closeEntry()
                    }
                }
            }

            // Unlocked Page Sheet (Read View OR Edit Mode)
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                visible: !!page.currentEntry.exists && (!page.currentEntry.locked || !!page.currentEntry.unlocked)
                spacing: Kirigami.Units.smallSpacing

                // Single clean header bar (no duplicate controls elsewhere)
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        visible: !page.editMode
                        Layout.fillWidth: true
                        level: 2
                        text: page.currentEntry.title ?? ""
                        elide: Text.ElideRight
                    }

                    QQC2.TextField {
                        id: titleField
                        visible: page.editMode
                        Layout.fillWidth: true
                        Layout.minimumWidth: Kirigami.Units.gridUnit * 6
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.15
                        font.bold: true
                        placeholderText: i18nc("@info:placeholder", "Page title…")
                        onEditingFinished: page.flushTitle()
                    }

                    QQC2.Button {
                        visible: !page.editMode
                        icon.name: "document-edit"
                        text: i18nc("@action:button", "Edit")
                        onClicked: page.openEntryForEditing(page.currentEntryId)
                    }

                    QQC2.Button {
                        visible: page.editMode
                        icon.name: "dialog-ok-apply"
                        text: i18nc("@action:button", "Done")
                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Finish editing and close editor")
                        QQC2.ToolTip.visible: hovered
                        onClicked: page.finishEditing()
                    }

                    QQC2.ToolButton {
                        icon.name: page.currentEntry.locked ? "object-locked" : "object-unlocked"
                        QQC2.ToolTip.text: page.currentEntry.locked ? i18nc("@info:tooltip", "Protection & Password") : i18nc("@info:tooltip", "Lock with password")
                        QQC2.ToolTip.visible: hovered
                        onClicked: lockMenu.popup(this, 0, height)

                        QQC2.Menu {
                            id: lockMenu
                            QQC2.MenuItem {
                                visible: !page.currentEntry.locked
                                icon.name: "object-locked"
                                text: i18nc("@action:inmenu", "Lock with Password…")
                                onTriggered: setPassDialog.openForMode("set")
                            }
                            QQC2.MenuItem {
                                visible: !!page.currentEntry.locked && !!page.currentEntry.unlocked
                                icon.name: "object-locked"
                                text: i18nc("@action:inmenu", "Lock Now")
                                onTriggered: {
                                    page.flushContent();
                                    page.editMode = false;
                                    App.journal.relockEntry(page.currentEntryId);
                                }
                            }
                            QQC2.MenuItem {
                                visible: !!page.currentEntry.locked
                                icon.name: "key-enter"
                                text: i18nc("@action:inmenu", "Change Password…")
                                onTriggered: setPassDialog.openForMode("change")
                            }
                            QQC2.MenuItem {
                                visible: !!page.currentEntry.locked
                                icon.name: "object-unlocked"
                                text: i18nc("@action:inmenu", "Remove Password Protection…")
                                onTriggered: setPassDialog.openForMode("remove")
                            }
                        }
                    }

                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Delete entry")
                        QQC2.ToolTip.visible: hovered
                        onClicked: confirmDeleteEntry.open()
                    }

                    QQC2.ToolButton {
                        icon.name: "overflow-menu"
                        onClicked: entryActionsMenu.popup(this, 0, height)

                        QQC2.Menu {
                            id: entryActionsMenu
                            QQC2.MenuItem {
                                icon.name: "edit-rename"
                                text: i18nc("@action:inmenu", "Rename…")
                                onTriggered: page.openRenameDialog()
                            }
                            QQC2.MenuItem {
                                icon.name: "edit-copy"
                                text: i18nc("@action:inmenu", "Duplicate")
                                onTriggered: {
                                    page.flushContent();
                                    const copyId = App.journal.duplicateEntry(page.currentEntryId);
                                    if (copyId > 0) {
                                        page.selectEntry(copyId);
                                    }
                                }
                            }
                            QQC2.MenuItem {
                                visible: !!page.currentEntry.synced
                                icon.name: "network-offline"
                                text: i18nc("@action:inmenu", "Keep Offline Only (Local)")
                                onTriggered: App.journal.setEntrySyncAccount(page.currentEntryId, 0)
                            }
                            QQC2.MenuItem {
                                visible: !page.currentEntry.synced && App.sync.hasAccounts
                                icon.name: "cloud-upload"
                                text: i18nc("@action:inmenu", "Sync with Nextcloud Notes")
                                onTriggered: {
                                    if (App.sync.accounts.length === 1) {
                                        App.journal.setEntrySyncAccount(page.currentEntryId, App.sync.accounts[0].id);
                                    } else {
                                        entrySyncMenu.popup();
                                    }
                                }
                            }
                            QQC2.MenuSeparator {}
                            QQC2.MenuItem {
                                icon.name: "document-export"
                                text: i18nc("@action:inmenu", "Export Markdown (.md)…")
                                onTriggered: exportMdDialog.open()
                            }
                            QQC2.MenuItem {
                                icon.name: "document-print"
                                text: i18nc("@action:inmenu", "Export PDF / Print…")
                                onTriggered: page.printEntryRequested(page.currentEntryId)
                            }
                        }
                    }

                    QQC2.ToolButton {
                        icon.name: "window-close"
                        QQC2.ToolTip.text: i18nc("@info:tooltip", "Close page")
                        QQC2.ToolTip.visible: hovered
                        onClicked: page.closeEntry()
                    }
                }

                // Metadata & storage bar (Notebook, Date, Offline / Nextcloud Notes toggle, Tags)
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    // Read-only notebook chip when viewing
                    Rectangle {
                        visible: !page.editMode
                        width: readNbRow.implicitWidth + 12
                        height: 26
                        radius: 4
                        color: Qt.alpha(page.currentEntry.notebookColor || "#3daee9", 0.2)
                        RowLayout {
                            id: readNbRow
                            anchors.centerIn: parent
                            spacing: 4
                            Rectangle {
                                width: 8
                                height: 8
                                radius: 4
                                color: page.currentEntry.notebookColor || "#3daee9"
                            }
                            QQC2.Label {
                                text: page.currentEntry.notebookName ?? ""
                                font: Kirigami.Theme.smallFont
                            }
                        }
                    }

                    // Editable notebook selector when editing
                    QQC2.ComboBox {
                        visible: page.editMode
                        readonly property var nbList: App.journal.notebooks
                        model: nbList
                        textRole: "name"
                        currentIndex: Math.max(0, nbList.findIndex(n => n.id === page.currentEntry.notebookId))
                        onActivated: index => {
                            if (nbList[index] && nbList[index].id !== page.currentEntry.notebookId) {
                                App.journal.updateEntry(page.currentEntryId, { notebookId: nbList[index].id });
                            }
                        }
                    }

                    QQC2.Button {
                        enabled: page.editMode
                        flat: !page.editMode
                        icon.name: "view-calendar-day"
                        text: page.currentEntry.entryDateText ?? ""
                        onClicked: {
                            const iso = page.currentEntry.entryDate ?? "";
                            entryDatePopup.value = iso !== "" ? new Date(iso + "T00:00:00") : new Date();
                            entryDatePopup.open();
                        }
                    }

                    // Explicit Offline Local vs Nextcloud Notes Sync toggle button
                    QQC2.ToolButton {
                        icon.name: page.currentEntry.synced ? "cloud-upload" : "drive-harddisk"
                        text: page.currentEntry.synced
                            ? i18nc("@action:button", "Nextcloud Notes")
                            : i18nc("@action:button", "Offline Only")
                        QQC2.ToolTip.text: page.currentEntry.synced
                            ? i18nc("@info:tooltip", "Synced with %1 — click to switch storage mode", page.currentEntry.syncAccountName || "Nextcloud")
                            : i18nc("@info:tooltip", "Stored offline only on this computer — click to sync with Nextcloud Notes")
                        QQC2.ToolTip.visible: hovered
                        enabled: !page.currentEntry.locked
                        onClicked: entrySyncMenu.popup(this, 0, height)
                    }

                    Repeater {
                        model: page.currentEntry.tags ?? []
                        Kirigami.Chip {
                            required property string modelData
                            text: "#" + modelData
                            checkable: false
                            closable: page.editMode
                            onRemoved: {
                                const tags = (page.currentEntry.tags ?? []).filter(t => t !== modelData);
                                App.journal.updateEntry(page.currentEntryId, { tags: tags });
                            }
                        }
                    }

                    QQC2.TextField {
                        id: jTagField
                        visible: page.editMode
                        width: Kirigami.Units.gridUnit * 8
                        placeholderText: i18nc("@info:placeholder", "Add tag…")
                        onAccepted: {
                            const clean = text.trim().replace(/^#+/, "");
                            if (clean !== "") {
                                const tags = (page.currentEntry.tags ?? []).slice();
                                tags.push(clean);
                                App.journal.updateEntry(page.currentEntryId, { tags: tags });
                            }
                            text = "";
                        }
                    }
                }

                Kirigami.Separator { Layout.fillWidth: true }

                // Reading View (shown on single-click; clean Markdown without edit clutter)
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !page.editMode
                    Kirigami.Theme.colorSet: Kirigami.Theme.View
                    Kirigami.Theme.inherit: false
                    color: Kirigami.Theme.backgroundColor
                    border.width: 1
                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.16)
                    radius: Kirigami.Units.cornerRadius

                    Flickable {
                        id: readFlick
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        contentWidth: width
                        contentHeight: readMdLabel.implicitHeight + Kirigami.Units.largeSpacing * 2
                        boundsBehavior: Flickable.StopAtBounds
                        QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                        QQC2.Label {
                            id: readMdLabel
                            x: Kirigami.Units.largeSpacing
                            y: Kirigami.Units.largeSpacing
                            width: readFlick.width - Kirigami.Units.largeSpacing * 2
                            text: {
                                const raw = page.currentEntry.content ?? "";
                                return raw.trim() === "" ? ("*" + i18n("Empty page — double-click to start writing in Markdown") + "*") : raw;
                            }
                            textFormat: Text.MarkdownText
                            font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.05
                            wrapMode: Text.Wrap
                            onLinkActivated: link => Qt.openUrlExternally(link)
                            HoverHandler {
                                cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                            }
                            TapHandler {
                                onDoubleTapped: page.openEntryForEditing(page.currentEntryId)
                            }
                        }
                    }
                }

                // Edit Panel (opened ONLY on double-click, "Edit" button, or New Page)
                MarkdownEditor {
                    id: mdEditor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: page.editMode
                    placeholderText: i18nc("@info:placeholder", "Start typing your note or journal entry in Markdown…")
                    onContentEdited: saveTimer.restart()
                    onFocusLost: page.flushContent()
                    Timer {
                        id: saveTimer
                        interval: 900
                        onTriggered: page.flushContent()
                    }
                }
            }
        }
    }

    // Storage mode menu (Offline Local vs Nextcloud Notes accounts)
    QQC2.Menu {
        id: entrySyncMenu
        QQC2.MenuItem {
            icon.name: "drive-harddisk"
            text: i18nc("@action:inmenu", "Offline Only (this computer)")
            checkable: true
            checked: !page.currentEntry.synced
            onTriggered: App.journal.setEntrySyncAccount(page.currentEntryId, 0)
        }
        QQC2.MenuSeparator {
            visible: App.sync.accounts.length > 0
        }
        Repeater {
            model: App.sync.accounts
            QQC2.MenuItem {
                required property var modelData
                icon.name: "cloud-upload"
                text: i18nc("@action:inmenu %1 is Nextcloud account name", "Sync with Nextcloud (%1)", modelData.name)
                checkable: true
                checked: page.currentEntry.syncAccountId === modelData.id
                onTriggered: App.journal.setEntrySyncAccount(page.currentEntryId, modelData.id)
            }
        }
        QQC2.MenuItem {
            visible: App.sync.accounts.length === 0
            enabled: false
            text: i18nc("@info", "Connect a Nextcloud account in Settings to sync notes.")
        }
    }

    // Unlock popup triggered on double-clicking a locked entry
    Kirigami.Dialog {
        id: unlockDialog
        property var targetId: 0

        function openForEntry(id) {
            targetId = id;
            dlgPass.text = "";
            dlgPassErr.text = "";
            open();
            dlgPass.forceActiveFocus();
        }

        title: i18nc("@title:window", "Unlock Protected Page")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 20
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "object-unlocked"
                text: i18nc("@action:button", "Unlock")
                onTriggered: {
                    if (App.journal.unlockEntry(unlockDialog.targetId, dlgPass.text)) {
                        unlockDialog.close();
                        page.entryRevision++;
                        page.loadEntry();
                        page.editMode = true;
                        mdEditor.viewMode = 0;
                        Qt.callLater(() => mdEditor.editor.forceActiveFocus());
                    } else {
                        dlgPassErr.text = App.journal.lastError();
                    }
                }
            }
        ]

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            QQC2.TextField {
                id: dlgPass
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: i18nc("@info:placeholder", "Enter password…")
                onAccepted: unlockDialog.customFooterActions[0].trigger()
            }
            QQC2.Label {
                id: dlgPassErr
                visible: text !== ""
                color: Kirigami.Theme.negativeTextColor
                wrapMode: Text.Wrap
            }
        }
    }

    // Notebook Create / Rename Dialog
    Kirigami.Dialog {
        id: notebookDialog
        property var editId: 0
        property string nbColor: "#3daee9"
        property var syncAccountId: 0

        function syncChoices() {
            const list = [{ id: 0, label: i18nc("storage option", "Offline Only (local)") }];
            for (const a of App.sync.accounts) {
                list.push({ id: a.id, label: i18nc("storage option", "Nextcloud Notes (%1)", a.name) });
            }
            return list;
        }

        function openCreate() {
            editId = 0;
            nbNameField.text = "";
            nbColor = "#3daee9";
            syncAccountId = 0;
            applyExistingCheck.checked = true;
            open();
            nbNameField.forceActiveFocus();
        }

        function openRename(nb) {
            editId = nb.id;
            nbNameField.text = nb.name;
            nbColor = nb.color || "#3daee9";
            syncAccountId = nb.syncAccountId ?? 0;
            applyExistingCheck.checked = true;
            open();
            nbNameField.forceActiveFocus();
            nbNameField.selectAll();
        }

        title: editId > 0 ? i18nc("@title:window", "Edit Notebook") : i18nc("@title:window", "New Notebook")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 24
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: {
            if (editId > 0) {
                App.journal.renameNotebook(editId, nbNameField.text, nbColor);
                App.journal.setNotebookSyncAccount(editId, syncAccountId, applyExistingCheck.checked);
            } else {
                const newId = App.journal.createNotebook(nbNameField.text, nbColor, syncAccountId);
                if (newId > 0) {
                    App.journal.selectedNotebookId = newId;
                }
            }
        }

        Kirigami.FormLayout {
            QQC2.TextField {
                id: nbNameField
                Kirigami.FormData.label: i18nc("@label", "Name:")
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "e.g. Work, Personal, Studies…")
                onAccepted: notebookDialog.accept()
            }
            Flow {
                Kirigami.FormData.label: i18nc("@label", "Color:")
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: ["#3daee9", "#27ae60", "#f67400", "#e93d58", "#9b59b6", "#1abc9c", "#fdbc4b"]
                    Rectangle {
                        id: nbSwatch
                        required property string modelData
                        width: Kirigami.Units.iconSizes.smallMedium
                        height: width
                        radius: width / 2
                        color: nbSwatch.modelData
                        border.width: notebookDialog.nbColor === nbSwatch.modelData ? 3 : 1
                        border.color: notebookDialog.nbColor === nbSwatch.modelData ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        TapHandler { onTapped: notebookDialog.nbColor = nbSwatch.modelData }
                    }
                }
            }
            QQC2.ComboBox {
                id: nbSyncCombo
                Kirigami.FormData.label: i18nc("@label", "Storage:")
                visible: App.sync.hasAccounts
                Layout.fillWidth: true
                readonly property var choices: notebookDialog.syncChoices()
                model: choices
                textRole: "label"
                currentIndex: Math.max(0, choices.findIndex(c => c.id === notebookDialog.syncAccountId))
                onActivated: index => notebookDialog.syncAccountId = choices[index].id
            }
            QQC2.CheckBox {
                id: applyExistingCheck
                visible: notebookDialog.editId > 0 && App.sync.hasAccounts
                text: i18nc("@option:check", "Apply storage mode to existing unlocked pages")
                checked: true
            }
        }
    }

    QQC2.Menu {
        id: nbMenu
        property var nb: ({ id: 0, name: "", color: "#3daee9", syncAccountId: 0 })
        QQC2.MenuItem {
            icon.name: "edit-rename"
            text: i18nc("@action:inmenu", "Edit Notebook…")
            onTriggered: notebookDialog.openRename(nbMenu.nb)
        }
        QQC2.MenuItem {
            visible: !!nbMenu.nb.synced
            icon.name: "drive-harddisk"
            text: i18nc("@action:inmenu", "Make Notebook Offline Only")
            onTriggered: App.journal.setNotebookSyncAccount(nbMenu.nb.id, 0, true)
        }
        QQC2.MenuItem {
            visible: !nbMenu.nb.synced && App.sync.hasAccounts
            icon.name: "cloud-upload"
            text: i18nc("@action:inmenu", "Sync Notebook with Nextcloud Notes")
            onTriggered: {
                if (App.sync.accounts.length > 0) {
                    App.journal.setNotebookSyncAccount(nbMenu.nb.id, App.sync.accounts[0].id, true);
                }
            }
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            icon.name: "edit-delete"
            text: i18nc("@action:inmenu", "Delete Notebook…")
            onTriggered: confirmDeleteNotebook.open()
        }
    }

    Kirigami.PromptDialog {
        id: confirmDeleteNotebook
        title: i18nc("@title:window", "Delete Notebook?")
        subtitle: i18nc("@info", "“%1” and all pages inside it will be permanently deleted.", nbMenu.nb.name ?? "")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Delete")
                onTriggered: {
                    App.journal.deleteNotebook(nbMenu.nb.id);
                    confirmDeleteNotebook.close();
                }
            }
        ]
    }

    QQC2.Menu {
        id: entryContextMenu
        QQC2.MenuItem {
            icon.name: "document-edit"
            text: i18nc("@action:inmenu", "Edit in Markdown")
            onTriggered: page.openEntryForEditing(page.currentEntryId)
        }
        QQC2.MenuItem {
            icon.name: "edit-rename"
            text: i18nc("@action:inmenu", "Rename…")
            onTriggered: page.openRenameDialog()
        }
        QQC2.MenuItem {
            visible: !page.currentEntry.locked
            icon.name: "object-locked"
            text: i18nc("@action:inmenu", "Lock with Password…")
            onTriggered: setPassDialog.openForMode("set")
        }
        QQC2.MenuItem {
            visible: !!page.currentEntry.locked && !page.currentEntry.unlocked
            icon.name: "object-unlocked"
            text: i18nc("@action:inmenu", "Unlock…")
            onTriggered: unlockDialog.openForEntry(page.currentEntryId)
        }
        QQC2.MenuItem {
            visible: !!page.currentEntry.locked && !!page.currentEntry.unlocked
            icon.name: "object-locked"
            text: i18nc("@action:inmenu", "Lock Now")
            onTriggered: {
                page.flushContent();
                page.editMode = false;
                App.journal.relockEntry(page.currentEntryId);
            }
        }
        QQC2.MenuItem {
            visible: !!page.currentEntry.locked
            icon.name: "object-unlocked"
            text: i18nc("@action:inmenu", "Remove Password Protection…")
            onTriggered: setPassDialog.openForMode("remove")
        }
        QQC2.MenuItem {
            visible: !!page.currentEntry.synced
            icon.name: "drive-harddisk"
            text: i18nc("@action:inmenu", "Keep Offline Only (Local)")
            onTriggered: App.journal.setEntrySyncAccount(page.currentEntryId, 0)
        }
        QQC2.MenuItem {
            visible: !page.currentEntry.synced && App.sync.hasAccounts
            icon.name: "cloud-upload"
            text: i18nc("@action:inmenu", "Sync with Nextcloud Notes")
            onTriggered: {
                if (App.sync.accounts.length > 0) {
                    App.journal.setEntrySyncAccount(page.currentEntryId, App.sync.accounts[0].id);
                }
            }
        }
        QQC2.MenuItem {
            icon.name: "edit-copy"
            text: i18nc("@action:inmenu", "Duplicate")
            onTriggered: {
                page.flushContent();
                const copyId = App.journal.duplicateEntry(page.currentEntryId);
                if (copyId > 0) {
                    page.selectEntry(copyId);
                }
            }
        }
        QQC2.MenuItem {
            icon.name: "window-close"
            text: i18nc("@action:inmenu", "Close Page Panel")
            onTriggered: page.closeEntry()
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            icon.name: "edit-delete"
            text: i18nc("@action:inmenu", "Delete Entry…")
            onTriggered: confirmDeleteEntry.open()
        }
    }

    DateTime.DatePopup {
        id: entryDatePopup
        onAccepted: App.journal.updateEntry(page.currentEntryId, { entryDate: Qt.formatDate(value, "yyyy-MM-dd") })
    }

    Kirigami.Dialog {
        id: renameDialog
        title: i18nc("@title:window", "Rename Entry")
        padding: Kirigami.Units.largeSpacing
        standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
        onAccepted: App.journal.renameEntry(page.currentEntryId, renameField.text)

        QQC2.TextField {
            id: renameField
            Layout.fillWidth: true
            onAccepted: renameDialog.accept()
        }
    }

    Kirigami.Dialog {
        id: setPassDialog
        property string mode: "set"

        function openForMode(m) {
            mode = m;
            curPass.text = "";
            newPass1.text = "";
            newPass2.text = "";
            passErr.text = "";
            open();
        }

        title: mode === "set" ? i18nc("@title:window", "Protect Entry with Password")
             : mode === "change" ? i18nc("@title:window", "Change Entry Password")
             : i18nc("@title:window", "Remove Password Protection")
        padding: Kirigami.Units.largeSpacing
        preferredWidth: Kirigami.Units.gridUnit * 22
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "dialog-ok"
                text: i18nc("@action:button", "Confirm")
                onTriggered: {
                    if (setPassDialog.mode !== "remove" && newPass1.text !== newPass2.text) {
                        passErr.text = i18n("Passwords do not match.");
                        return;
                    }
                    if (setPassDialog.mode === "set") {
                        page.flushContent();
                        if (!App.journal.lockWithPassword(page.currentEntryId, newPass1.text)) {
                            passErr.text = App.journal.lastError();
                            return;
                        }
                        page.editMode = false;
                    } else if (setPassDialog.mode === "change") {
                        if (!App.journal.changePassword(page.currentEntryId, curPass.text, newPass1.text)) {
                            passErr.text = App.journal.lastError();
                            return;
                        }
                    } else {
                        if (!App.journal.removePassword(page.currentEntryId, curPass.text)) {
                            passErr.text = App.journal.lastError();
                            return;
                        }
                    }
                    setPassDialog.close();
                }
            }
        ]

        Kirigami.FormLayout {
            QQC2.TextField {
                id: curPass
                visible: setPassDialog.mode !== "set"
                Kirigami.FormData.label: i18nc("@label", "Current password:")
                echoMode: TextInput.Password
            }
            QQC2.TextField {
                id: newPass1
                visible: setPassDialog.mode !== "remove"
                Kirigami.FormData.label: i18nc("@label", "New password:")
                echoMode: TextInput.Password
            }
            QQC2.TextField {
                id: newPass2
                visible: setPassDialog.mode !== "remove"
                Kirigami.FormData.label: i18nc("@label", "Confirm password:")
                echoMode: TextInput.Password
            }
            QQC2.Label {
                id: passErr
                visible: text !== ""
                color: Kirigami.Theme.negativeTextColor
                wrapMode: Text.Wrap
            }
        }
    }

    Kirigami.PromptDialog {
        id: confirmDeleteEntry
        title: i18nc("@title:window", "Delete Journal Entry?")
        subtitle: i18nc("@info", "“%1” will be permanently deleted.", page.currentEntry.title ?? "")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Delete")
                onTriggered: {
                    App.journal.deleteEntry(page.currentEntryId);
                    page.closeEntry();
                    confirmDeleteEntry.close();
                }
            }
        ]
    }

    Dialogs.FileDialog {
        id: exportMdDialog
        title: i18nc("@title:window", "Export Markdown")
        fileMode: Dialogs.FileDialog.SaveFile
        defaultSuffix: "md"
        nameFilters: [i18nc("file type", "Markdown files (*.md)")]
        onAccepted: App.journal.exportMarkdown(page.currentEntryId, selectedFile)
    }
}
