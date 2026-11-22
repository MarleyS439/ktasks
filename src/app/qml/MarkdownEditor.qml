// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Reusable Markdown editor with formatting toolbar (bold, italic, headings,
// lists, checklist, quote, code, links, tables, HR) and Edit / Preview / Split modes.
ColumnLayout {
    id: root

    property alias text: area.text
    property alias editor: area
    property bool readOnly: false
    property bool compact: false
    readonly property bool narrowToolbar: compact || width < Kirigami.Units.gridUnit * 26
    property string placeholderText: i18nc("@info:placeholder", "Write in Markdown…")
    property int viewMode: 0 // 0 = Edit, 1 = Preview, 2 = Split
    signal contentEdited()
    signal focusLost()

    // Preserve selection even if focus momentarily shifts.
    property int savedSelStart: 0
    property int savedSelEnd: 0

    function activeRange() {
        if (area.selectionEnd > area.selectionStart) {
            return { start: area.selectionStart, end: area.selectionEnd };
        }
        if (savedSelEnd > savedSelStart && savedSelEnd <= area.text.length) {
            return { start: savedSelStart, end: savedSelEnd };
        }
        const pos = Math.min(area.cursorPosition, area.text.length);
        return { start: pos, end: pos };
    }

    function wrapSelection(prefix, suffix, fallback) {
        if (root.readOnly) {
            return;
        }
        if (root.viewMode === 1) {
            root.viewMode = 0;
        }
        const range = activeRange();
        const start = range.start;
        const end = range.end;
        const sel = area.text.slice(start, end);

        if (sel.length > 0) {
            // Toggle off if already wrapped
            if (sel.startsWith(prefix) && sel.endsWith(suffix) && sel.length >= prefix.length + suffix.length) {
                const unwrapped = sel.slice(prefix.length, sel.length - suffix.length);
                area.remove(start, end);
                area.insert(start, unwrapped);
                area.select(start, start + unwrapped.length);
            } else if (sel.indexOf("\n") >= 0 && !prefix.startsWith("\n")) {
                // Multi-line selection: format each non-empty line cleanly so Markdown renders every paragraph.
                const formatted = sel.split("\n").map(line => line.trim() === "" ? line : prefix + line + suffix).join("\n");
                area.remove(start, end);
                area.insert(start, formatted);
                area.select(start, start + formatted.length);
            } else {
                const wrapped = prefix + sel + suffix;
                area.remove(start, end);
                area.insert(start, wrapped);
                area.select(start, start + wrapped.length);
            }
        } else {
            const wrapped = prefix + fallback + suffix;
            area.insert(start, wrapped);
            area.select(start + prefix.length, start + prefix.length + fallback.length);
        }
        area.forceActiveFocus();
        root.contentEdited();
    }

    function prefixLines(prefix) {
        if (root.readOnly) {
            return;
        }
        if (root.viewMode === 1) {
            root.viewMode = 0;
        }
        const full = area.text;
        const range = activeRange();
        const lineStart = full.lastIndexOf("\n", Math.max(0, range.start - 1)) + 1;
        let lineEnd = full.indexOf("\n", range.end);
        if (lineEnd < 0) {
            lineEnd = full.length;
        }
        const block = full.slice(lineStart, lineEnd);
        const rawLines = block.split("\n");
        const allPrefixed = prefix !== "1. " && rawLines.every(l => l.trim() === "" || l.startsWith(prefix));
        const lines = rawLines.map((l, idx) => {
            if (allPrefixed) {
                return l.startsWith(prefix) ? l.slice(prefix.length) : l;
            }
            if (prefix === "1. ") {
                return (idx + 1) + ". " + l;
            }
            return prefix + l;
        });
        const joined = lines.join("\n");
        area.remove(lineStart, lineEnd);
        area.insert(lineStart, joined);
        area.select(lineStart, lineStart + joined.length);
        area.forceActiveFocus();
        root.contentEdited();
    }

    function insertAtCursor(snippet) {
        if (root.readOnly) {
            return;
        }
        if (root.viewMode === 1) {
            root.viewMode = 0;
        }
        const range = activeRange();
        area.remove(range.start, range.end);
        area.insert(range.start, snippet);
        area.cursorPosition = range.start + snippet.length;
        area.forceActiveFocus();
        root.contentEdited();
    }

    function insertTable(rows, cols) {
        const headers = [];
        const sep = [];
        for (let c = 1; c <= cols; ++c) {
            headers.push(i18nc("markdown table header", "Column %1", c));
            sep.push("---");
        }
        let md = "\n| " + headers.join(" | ") + " |\n| " + sep.join(" | ") + " |\n";
        for (let r = 0; r < rows; ++r) {
            const cells = [];
            for (let c = 0; c < cols; ++c) {
                cells.push("   ");
            }
            md += "| " + cells.join(" | ") + " |\n";
        }
        insertAtCursor(md);
    }

    spacing: Kirigami.Units.smallSpacing

    // Formatting toolbar (focusPolicy: Qt.NoFocus keeps TextArea selection intact!)
    QQC2.ToolBar {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        padding: Kirigami.Units.smallSpacing / 2

        contentItem: RowLayout {
            spacing: 2

            Flow {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 80
                spacing: 2
                enabled: !root.readOnly && root.viewMode !== 1

                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "format-text-bold"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Bold (Ctrl+B)")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.wrapSelection("**", "**", i18n("bold"))
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "format-text-italic"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Italic (Ctrl+I)")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.wrapSelection("*", "*", i18n("italic"))
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "format-text-strikethrough"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Strikethrough")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.wrapSelection("~~", "~~", i18n("text"))
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    text: "H"
                    font.bold: true
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Headings")
                    QQC2.ToolTip.visible: hovered
                    onClicked: headingMenu.popup(this, 0, height)

                    QQC2.Menu {
                        id: headingMenu
                        QQC2.MenuItem { text: i18n("Heading 1 (#)"); onTriggered: root.prefixLines("# ") }
                        QQC2.MenuItem { text: i18n("Heading 2 (##)"); onTriggered: root.prefixLines("## ") }
                        QQC2.MenuItem { text: i18n("Heading 3 (###)"); onTriggered: root.prefixLines("### ") }
                    }
                }
                QQC2.ToolSeparator {}
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "format-list-unordered"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Bulleted list")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.prefixLines("- ")
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "format-list-ordered"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Numbered list")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.prefixLines("1. ")
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "task-complete"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Checklist item")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.prefixLines("- [ ] ")
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "format-text-blockquote"
                    text: "❝"
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Quote")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.prefixLines("> ")
                }
                QQC2.ToolSeparator {}
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "code-context"
                    text: "</>"
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Inline code")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.wrapSelection("`", "`", "code")
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    visible: !root.compact
                    icon.name: "text-x-script"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Code block")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.wrapSelection("\n```\n", "\n```\n", "// code")
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "insert-link"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Insert link (Ctrl+K)")
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        linkDialogLoader.active = true;
                        linkDialogLoader.item.openForSelection();
                    }
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    icon.name: "insert-table"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Insert table")
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        tableDialogLoader.active = true;
                        tableDialogLoader.item.open();
                    }
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    visible: !root.compact
                    icon.name: "insert-horizontal-rule"
                    text: "―"
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Horizontal rule")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.insertAtCursor("\n---\n")
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    visible: !root.compact
                    icon.name: "clock"
                    display: QQC2.AbstractButton.IconOnly
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Insert date & time")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.insertAtCursor(Qt.formatDateTime(new Date(), "dd/MM/yyyy HH:mm"))
                }
            }

            // View mode switcher
            RowLayout {
                spacing: 2
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    checkable: true
                    checked: root.viewMode === 0
                    icon.name: "document-edit"
                    text: root.narrowToolbar ? "" : i18nc("@action:button", "Edit")
                    display: root.narrowToolbar ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextBesideIcon
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Edit Markdown")
                    QQC2.ToolTip.visible: hovered
                    onClicked: {
                        root.viewMode = 0;
                        area.forceActiveFocus();
                    }
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    checkable: true
                    checked: root.viewMode === 1
                    icon.name: "view-preview"
                    text: root.narrowToolbar ? "" : i18nc("@action:button", "Preview")
                    display: root.narrowToolbar ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextBesideIcon
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Preview rendered Markdown")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.viewMode = 1
                }
                QQC2.ToolButton {
                    focusPolicy: Qt.NoFocus
                    visible: !root.compact
                    checkable: true
                    checked: root.viewMode === 2
                    icon.name: "view-split-left-right"
                    text: root.narrowToolbar ? "" : i18nc("@action:button", "Split")
                    display: root.narrowToolbar ? QQC2.AbstractButton.IconOnly : QQC2.AbstractButton.TextBesideIcon
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Side-by-side live preview")
                    QQC2.ToolTip.visible: hovered
                    onClicked: root.viewMode = 2
                }
            }
        }
    }

    // Editor + Preview area
    RowLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        spacing: Kirigami.Units.smallSpacing

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.viewMode === 0 || root.viewMode === 2

            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: area.activeFocus ? Kirigami.Theme.highlightColor : Qt.alpha(Kirigami.Theme.textColor, 0.22)
            radius: Kirigami.Units.cornerRadius

            TapHandler {
                onTapped: area.forceActiveFocus()
            }

            QQC2.ScrollView {
                id: editorScroll
                anchors.fill: parent
                anchors.margins: 1
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                QQC2.TextArea {
                    id: area
                    padding: Kirigami.Units.largeSpacing
                    readOnly: root.readOnly
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    placeholderText: root.placeholderText
                    selectByMouse: true
                    activeFocusOnTab: true
                    background: null
                    font.family: root.compact ? Kirigami.Theme.defaultFont.family : "monospace"
                    onSelectionStartChanged: {
                        if (activeFocus) {
                            root.savedSelStart = selectionStart;
                        }
                    }
                    onSelectionEndChanged: {
                        if (activeFocus) {
                            root.savedSelEnd = selectionEnd;
                        }
                    }
                    onTextChanged: {
                        if (activeFocus) {
                            root.contentEdited();
                        }
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus) {
                            root.focusLost();
                        }
                    }

                    Shortcut {
                        sequence: "Ctrl+B"
                        enabled: area.activeFocus
                        onActivated: root.wrapSelection("**", "**", i18n("bold"))
                    }
                    Shortcut {
                        sequence: "Ctrl+I"
                        enabled: area.activeFocus
                        onActivated: root.wrapSelection("*", "*", i18n("italic"))
                    }
                    Shortcut {
                        sequence: "Ctrl+K"
                        enabled: area.activeFocus
                        onActivated: {
                            linkDialogLoader.active = true;
                            linkDialogLoader.item.openForSelection();
                        }
                    }
                }
            }
        }

        Kirigami.Separator {
            Layout.fillHeight: true
            visible: root.viewMode === 2
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.viewMode === 1 || root.viewMode === 2

            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.backgroundColor
            border.width: 1
            border.color: Qt.alpha(Kirigami.Theme.textColor, 0.22)
            radius: Kirigami.Units.cornerRadius

            Flickable {
                id: previewFlick
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                contentWidth: width
                contentHeight: previewLabel.implicitHeight + Kirigami.Units.largeSpacing * 2
                boundsBehavior: Flickable.StopAtBounds
                QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

                QQC2.Label {
                    id: previewLabel
                    x: Kirigami.Units.largeSpacing
                    y: Kirigami.Units.largeSpacing
                    width: previewFlick.width - Kirigami.Units.largeSpacing * 2
                    text: area.text.trim() === "" ? ("*" + i18n("Nothing to preview yet") + "*") : area.text
                    textFormat: Text.MarkdownText
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.05
                    wrapMode: Text.Wrap
                    onLinkActivated: link => Qt.openUrlExternally(link)
                    HoverHandler {
                        cursorShape: parent.hoveredLink !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }
                }
            }
        }
    }

    Loader {
        id: tableDialogLoader
        active: false
        sourceComponent: Kirigami.Dialog {
            title: i18nc("@title:window", "Insert Markdown Table")
            padding: Kirigami.Units.largeSpacing
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel
            onAccepted: root.insertTable(rowsSpin.value, colsSpin.value)

            Kirigami.FormLayout {
                PaddedSpinBox {
                    id: rowsSpin
                    Kirigami.FormData.label: i18nc("@label", "Data rows:")
                    from: 1
                    to: 20
                    value: 3
                }
                PaddedSpinBox {
                    id: colsSpin
                    Kirigami.FormData.label: i18nc("@label", "Columns:")
                    from: 1
                    to: 10
                    value: 3
                }
            }
        }
    }

    Loader {
        id: linkDialogLoader
        active: false
        sourceComponent: Kirigami.Dialog {
            id: linkDlg
            title: i18nc("@title:window", "Insert Link")
            padding: Kirigami.Units.largeSpacing
            standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

            function openForSelection() {
                const r = root.activeRange();
                linkLabel.text = area.text.slice(r.start, r.end);
                linkUrl.text = "https://";
                open();
                linkUrl.forceActiveFocus();
            }

            onAccepted: {
                const label = linkLabel.text.trim() !== "" ? linkLabel.text.trim() : linkUrl.text.trim();
                root.insertAtCursor("[" + label + "](" + linkUrl.text.trim() + ")");
            }

            Kirigami.FormLayout {
                QQC2.TextField {
                    id: linkLabel
                    Kirigami.FormData.label: i18nc("@label", "Text:")
                    Layout.fillWidth: true
                }
                QQC2.TextField {
                    id: linkUrl
                    Kirigami.FormData.label: i18nc("@label", "URL:")
                    Layout.fillWidth: true
                    onAccepted: linkDlg.accept()
                }
            }
        }
    }
}
