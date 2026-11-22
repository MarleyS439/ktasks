// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import app.ktasks.backend

Kirigami.Dialog {
    id: dialog

    property string error: ""

    title: i18nc("@title:window", "Connect an Account")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.Close
    onOpened: {
        error = "";
        serverField.forceActiveFocus();
    }
    onClosed: App.sync.cancelLogin()

    Connections {
        target: App.sync
        function onAccountAdded() {
            dialog.close();
        }
        function onLoginFailed(message) {
            dialog.error = message;
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.error !== ""
            type: Kirigami.MessageType.Error
            text: dialog.error
        }

        QQC2.TabBar {
            id: tabs
            Layout.fillWidth: true
            enabled: !App.sync.loginInProgress
            QQC2.TabButton {
                text: i18nc("@title:tab", "Nextcloud")
            }
            QQC2.TabButton {
                text: i18nc("@title:tab", "Other CalDAV server")
            }
        }

        StackLayout {
            Layout.fillWidth: true
            currentIndex: tabs.currentIndex

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18nc("@info", "Type the address of your Nextcloud. Your browser opens so you can confirm the access there; your password is never typed into this application.")
                }
                QQC2.TextField {
                    id: serverField
                    Layout.fillWidth: true
                    enabled: !App.sync.loginInProgress
                    placeholderText: "https://cloud.example.com"
                    inputMethodHints: Qt.ImhUrlCharactersOnly
                    Accessible.name: i18nc("@label:textbox", "Server address")
                    onAccepted: signIn.clicked()
                }
                RowLayout {
                    Layout.fillWidth: true
                    QQC2.Button {
                        id: signIn
                        icon.name: "internet-web-browser"
                        text: i18nc("@action:button", "Sign In with the Browser")
                        enabled: serverField.text.trim() !== "" && !App.sync.loginInProgress
                        onClicked: {
                            dialog.error = "";
                            App.sync.startLoginFlow(serverField.text);
                        }
                    }
                    QQC2.Button {
                        visible: App.sync.loginInProgress
                        icon.name: "dialog-cancel"
                        text: i18nc("@action:button", "Cancel")
                        onClicked: App.sync.cancelLogin()
                    }
                }
            }

            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18nc("@info", "For any CalDAV server, or a Nextcloud app password you created yourself. The password is stored in the system keyring (KWallet).")
                }
                Kirigami.FormLayout {
                    Layout.fillWidth: true
                    enabled: !App.sync.loginInProgress
                    QQC2.TextField {
                        id: urlField
                        Kirigami.FormData.label: i18nc("@label:textbox", "Address:")
                        Layout.fillWidth: true
                        placeholderText: "https://example.com/dav"
                        inputMethodHints: Qt.ImhUrlCharactersOnly
                    }
                    QQC2.TextField {
                        id: userField
                        Kirigami.FormData.label: i18nc("@label:textbox", "User name:")
                        Layout.fillWidth: true
                    }
                    Kirigami.PasswordField {
                        id: passwordField
                        Kirigami.FormData.label: i18nc("@label:textbox", "Password:")
                        Layout.fillWidth: true
                        onAccepted: connectButton.clicked()
                    }
                }
                QQC2.Button {
                    id: connectButton
                    icon.name: "network-connect"
                    text: i18nc("@action:button", "Connect")
                    enabled: !App.sync.loginInProgress && urlField.text.trim() !== "" && userField.text.trim() !== "" && passwordField.text !== ""
                    onClicked: {
                        dialog.error = "";
                        App.sync.addAccountManually(urlField.text, userField.text, passwordField.text);
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: App.sync.loginInProgress
            QQC2.BusyIndicator {
                running: parent.visible
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
            }
            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: App.sync.loginStatus
            }
        }
    }
}
