// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.components as Components
import org.kde.kirigamiaddons.formcard as FormCard
import app.ktasks.backend

FormCard.FormCardPage {
    id: page

    signal addAccountRequested()

    readonly property var times: {
        const out = [];
        for (let h = 5; h <= 22; ++h) {
            out.push(String(h).padStart(2, "0") + ":00");
            out.push(String(h).padStart(2, "0") + ":30");
        }
        return out;
    }

    title: i18nc("@title", "Settings")

    // ---------- Profile ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Profile")
    }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: App.effectiveProfileName
            description: App.effectiveProfileEmail !== "" ? App.effectiveProfileEmail : i18nc("@info", "Add your name, email, and profile picture")
            leading: Components.Avatar {
                source: App.effectiveAvatarPath !== "" ? ("file://" + App.effectiveAvatarPath) : ""
                name: App.effectiveProfileName
                implicitWidth: Kirigami.Units.iconSizes.large
                implicitHeight: Kirigami.Units.iconSizes.large
            }
            trailing: QQC2.Button {
                visible: !App.settings.useSystemAvatar
                icon.name: "document-open"
                text: i18nc("@action:button", "Choose Photo…")
                onClicked: avatarDialog.open()
            }
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Use system profile picture")
            description: App.systemAvatarPath !== "" ? i18nc("@info", "Uses the user avatar configured in KDE System Settings.")
                                                     : i18nc("@info", "No system avatar detected; choose a custom image below if desired.")
            checked: App.settings.useSystemAvatar
            onToggled: App.settings.useSystemAvatar = checked
        }
        FormCard.FormTextFieldDelegate {
            id: profileNameField
            label: i18nc("@label:textbox", "Name:")
            placeholderText: App.systemFullName
            text: App.settings.profileName
            onAccepted: saveProfileBtn.clicked()
        }
        FormCard.FormTextFieldDelegate {
            id: profileEmailField
            label: i18nc("@label:textbox", "Email:")
            placeholderText: i18nc("@info:placeholder", "name@example.com")
            text: App.settings.profileEmail
            onAccepted: saveProfileBtn.clicked()
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            id: saveProfileBtn
            icon.name: "document-save"
            text: i18nc("@action:button", "Save Profile")
            onClicked: {
                page.forceActiveFocus();
                App.saveProfile(profileNameField.text, profileEmailField.text);
            }
        }
    }

    Dialogs.FileDialog {
        id: avatarDialog
        title: i18nc("@title:window", "Choose Profile Picture")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [i18nc("file type", "Images (*.png *.jpg *.jpeg *.webp *.svg)"), i18nc("file type", "All files (*)")]
        onAccepted: App.setProfileAvatarFromUrl(selectedFile)
    }

    // ---------- Appearance ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Appearance")
    }
    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            text: i18nc("@label:listbox", "Color scheme")
            description: i18nc("@info", "“Default” follows the color scheme chosen in System Settings.")
            model: App.colorSchemes
            currentIndex: App.colorScheme
            onActivated: index => App.colorScheme = index
        }
    }

    // ---------- Accounts ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Synchronization")
    }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            visible: !App.sync.hasAccounts
            text: i18nc("@info", "Tasks are stored on this device only")
            description: i18nc("@info", "Connect a Nextcloud account to see the same tasks on your phone and other computers. Everything keeps working without a connection.")
        }
        Repeater {
            model: App.sync.accounts
            FormCard.FormTextDelegate {
                id: accountRow
                required property var modelData
                text: modelData.name
                description: modelData.error !== "" ? modelData.error
                             : modelData.syncing ? i18nc("@info", "Synchronizing…")
                             : modelData.lastSync !== "" ? i18nc("@info %1 is a date and time", "Last synchronized %1", modelData.lastSync)
                             : i18nc("@info", "Not synchronized yet")
                descriptionItem.color: modelData.error !== "" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.disabledTextColor
                leading: Kirigami.Icon {
                    source: accountRow.modelData.error !== "" ? "data-warning" : "cloud-download"
                    implicitWidth: Kirigami.Units.iconSizes.medium
                    implicitHeight: Kirigami.Units.iconSizes.medium
                }
                trailing: RowLayout {
                    QQC2.ToolButton {
                        visible: accountRow.modelData.needsLogin || accountRow.modelData.error !== ""
                        icon.name: "im-user-online"
                        text: i18nc("@action:button", "Sign In Again")
                        onClicked: page.addAccountRequested()
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-delete"
                        Accessible.name: i18nc("@action:button", "Remove account")
                        QQC2.ToolTip.text: Accessible.name
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                        onClicked: {
                            removeAccount.accountId = accountRow.modelData.id;
                            removeAccount.accountName = accountRow.modelData.name;
                            removeAccount.open();
                        }
                    }
                }
            }
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            icon.name: "list-add"
            text: i18nc("@action:button", "Connect a Nextcloud or CalDAV Account…")
            onClicked: page.addAccountRequested()
        }
        FormCard.FormDelegateSeparator {
            visible: App.sync.hasAccounts
        }
        FormCard.FormComboBoxDelegate {
            visible: App.sync.hasAccounts
            readonly property var values: [0, 5, 15, 30, 60]
            text: i18nc("@label:listbox", "Check the server")
            description: i18nc("@info", "Your own changes are always sent a few seconds after you make them.")
            model: [ i18nc("sync interval", "Only when I ask"), i18nc("sync interval", "Every 5 minutes"), i18nc("sync interval", "Every 15 minutes"),
                     i18nc("sync interval", "Every 30 minutes"), i18nc("sync interval", "Every hour") ]
            currentIndex: Math.max(0, values.indexOf(App.settings.syncInterval))
            onActivated: index => App.settings.syncInterval = values[index]
        }
        FormCard.FormSwitchDelegate {
            visible: App.sync.hasAccounts
            text: i18nc("@option:check", "Synchronize when the application starts")
            checked: App.settings.syncOnStart
            onToggled: App.settings.syncOnStart = checked
        }
        FormCard.FormSwitchDelegate {
            visible: App.sync.hasAccounts
            text: i18nc("@option:check", "Synchronize journal notes with Nextcloud Notes")
            description: i18nc("@info", "Unprotected notes sync as Markdown files with Nextcloud Notes. Individual notebooks and pages can still be marked offline only.")
            checked: App.settings.syncNotes
            onToggled: App.settings.syncNotes = checked
        }
        FormCard.FormSwitchDelegate {
            visible: App.sync.hasAccounts
            text: i18nc("@option:check", "Synchronize routine & planner blocks with Nextcloud Calendar")
            description: i18nc("@info", "Syncs routine templates and planned blocks as VEVENT items (compatible with Nextcloud Calendar and DAVx5 mobile apps). Individual blocks can still stay offline only.")
            checked: App.settings.syncRoutines
            onToggled: App.settings.syncRoutines = checked
        }
        Repeater {
            model: App.sync.accounts
            FormCard.FormComboBoxDelegate {
                id: routineCalDelegate
                required property var modelData
                readonly property var cals: App.sync.routineCalendars(modelData.id)
                readonly property var options: [{ href: "", name: i18nc("routine calendar option", "Auto (“Rotina” calendar)") }].concat(cals)
                visible: App.sync.hasAccounts && App.settings.syncRoutines && cals.length > 0
                text: App.sync.accounts.length > 1
                    ? i18nc("@label:listbox", "Routine calendar (%1)", modelData.name)
                    : i18nc("@label:listbox", "Routine calendar")
                model: options
                textRole: "name"
                valueRole: "href"
                currentIndex: {
                    const cur = App.sync.selectedRoutineCalendar(modelData.id);
                    if (!cur) {
                        return 0;
                    }
                    const idx = options.findIndex(o => o.href === cur);
                    return idx >= 0 ? idx : 0;
                }
                onActivated: index => {
                    if (index >= 0 && index < options.length) {
                        App.sync.setSelectedRoutineCalendar(modelData.id, options[index].href);
                    }
                }
            }
        }
    }

    // ---------- Notifications ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Notifications")
    }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Show reminders")
            description: i18nc("@info", "Desktop notifications with buttons to complete or snooze the task.")
            checked: App.settings.notifyReminders
            onToggled: App.settings.notifyReminders = checked
        }
        FormCard.FormSwitchDelegate {
            enabled: App.settings.notifyReminders
            text: i18nc("@option:check", "Also notify when a task without a reminder becomes due")
            checked: App.settings.notifyAtDue
            onToggled: App.settings.notifyAtDue = checked
        }
        FormCard.FormComboBoxDelegate {
            enabled: App.settings.notifyReminders
            text: i18nc("@label:listbox", "Remind about tasks without a time at")
            model: page.times
            currentIndex: Math.max(0, page.times.indexOf(App.settings.allDayReminderTime))
            onActivated: index => App.settings.allDayReminderTime = page.times[index]
        }
        FormCard.FormComboBoxDelegate {
            enabled: App.settings.notifyReminders
            readonly property var values: [5, 10, 15, 30, 60, 120]
            text: i18nc("@label:listbox", "“Snooze” postpones a reminder by")
            model: [ i18nc("duration", "5 minutes"), i18nc("duration", "10 minutes"), i18nc("duration", "15 minutes"), i18nc("duration", "30 minutes"),
                     i18nc("duration", "1 hour"), i18nc("duration", "2 hours") ]
            currentIndex: Math.max(0, values.indexOf(App.settings.snoozeMinutes))
            onActivated: index => App.settings.snoozeMinutes = values[index]
        }
        FormCard.FormComboBoxDelegate {
            readonly property var values: [-1, 0, 15, 60, 1440]
            text: i18nc("@label:listbox", "Reminder for new tasks with a date")
            model: [ i18nc("default reminder", "None"), i18nc("default reminder", "When it is due"), i18nc("default reminder", "15 minutes before"),
                     i18nc("default reminder", "1 hour before"), i18nc("default reminder", "1 day before") ]
            currentIndex: Math.max(0, values.indexOf(App.settings.defaultReminder))
            onActivated: index => App.settings.defaultReminder = values[index]
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Daily summary")
            description: i18nc("@info", "One notification a day with what is due today and what is overdue.")
            checked: App.settings.dailySummary
            onToggled: App.settings.dailySummary = checked
        }
        FormCard.FormComboBoxDelegate {
            enabled: App.settings.dailySummary
            text: i18nc("@label:listbox", "Show the summary at")
            model: page.times
            currentIndex: Math.max(0, page.times.indexOf(App.settings.dailySummaryTime))
            onActivated: index => App.settings.dailySummaryTime = page.times[index]
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Notify about synchronization problems and conflicts")
            checked: App.settings.notifySyncProblems
            onToggled: App.settings.notifySyncProblems = checked
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Notify about scheduled routine blocks")
            checked: App.settings.notifyRoutines
            onToggled: App.settings.notifyRoutines = checked
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Notify when Pomodoro focus or break completes")
            checked: App.settings.notifyFocus
            onToggled: App.settings.notifyFocus = checked
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            icon.name: "notifications"
            text: i18nc("@action:button", "Send a Test Notification")
            onClicked: App.notifier.sendTestNotification()
        }
        FormCard.FormButtonDelegate {
            icon.name: "preferences-desktop-notification"
            text: i18nc("@action:button", "Sounds and Pop-ups in System Settings…")
            description: i18nc("@info", "Choose per kind of notification whether it shows a pop-up, plays a sound or stays in the history.")
            onClicked: App.openNotificationSettings()
        }
    }

    // ---------- Pomodoro & Focus ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Pomodoro & Focus Timer")
    }
    FormCard.FormCard {
        FormCard.FormComboBoxDelegate {
            readonly property var values: [15, 20, 25, 30, 45, 50, 60]
            text: i18nc("@label:listbox", "Focus session duration")
            model: values.map(v => i18ncp("duration", "%1 minute", "%1 minutes", v))
            currentIndex: Math.max(0, values.indexOf(App.settings.pomodoroFocusMinutes))
            onActivated: index => App.settings.pomodoroFocusMinutes = values[index]
        }
        FormCard.FormComboBoxDelegate {
            readonly property var values: [3, 5, 10, 15]
            text: i18nc("@label:listbox", "Short break duration")
            model: values.map(v => i18ncp("duration", "%1 minute", "%1 minutes", v))
            currentIndex: Math.max(0, values.indexOf(App.settings.pomodoroShortBreakMinutes))
            onActivated: index => App.settings.pomodoroShortBreakMinutes = values[index]
        }
        FormCard.FormComboBoxDelegate {
            readonly property var values: [10, 15, 20, 30]
            text: i18nc("@label:listbox", "Long break duration (after 4 cycles)")
            model: values.map(v => i18ncp("duration", "%1 minute", "%1 minutes", v))
            currentIndex: Math.max(0, values.indexOf(App.settings.pomodoroLongBreakMinutes))
            onActivated: index => App.settings.pomodoroLongBreakMinutes = values[index]
        }
    }

    // ---------- Summary, Weather & External Calendar ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Summary, Weather & External Agenda")
    }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Show KOrganizer / desktop calendars on Summary (read-only)")
            description: i18nc("@info", "Reads active KOrganizer/Akonadi calendars (Google Agenda, Birthdays, personal) and local .ics files into External Agenda without mixing them into Nextcloud Routine sync.")
            checked: App.settings.integrateKOrganizer
            onToggled: App.settings.integrateKOrganizer = checked
        }
        Repeater {
            model: App.settings.extraIcsPaths
            FormCard.FormTextDelegate {
                required property string modelData
                text: modelData
                trailing: QQC2.ToolButton {
                    icon.name: "edit-delete"
                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Remove calendar file")
                    QQC2.ToolTip.visible: hovered
                    onClicked: App.summary.removeExtraIcsPath(modelData)
                }
            }
        }
        FormCard.FormButtonDelegate {
            icon.name: "document-open"
            text: i18nc("@action:button", "Add Extra Local Calendar (.ics)…")
            onClicked: extraIcsDialog.open()
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormTextDelegate {
            text: i18nc("@label", "Weather city")
            description: App.settings.weatherCity !== ""
                         ? App.settings.weatherCity
                         : i18nc("@info", "Automatic (uses KDE KWeather city or approximate IP location)")
            trailing: RowLayout {
                spacing: Kirigami.Units.smallSpacing
                QQC2.Button {
                    icon.name: "mark-location"
                    text: i18nc("@action:button", "Choose City…")
                    onClicked: cityDialog.open()
                }
                QQC2.Button {
                    visible: App.settings.weatherCity !== ""
                    icon.name: "edit-clear"
                    text: i18nc("@action:button", "Reset Auto")
                    onClicked: {
                        App.settings.weatherCity = "";
                        App.settings.weatherLatitude = 999.0;
                        App.settings.weatherLongitude = 999.0;
                        App.summary.refreshWeather(true);
                    }
                }
            }
        }
    }

    Kirigami.Dialog {
        id: cityDialog
        title: i18nc("@title:window", "Weather Location")
        preferredWidth: Kirigami.Units.gridUnit * 24
        standardButtons: Kirigami.Dialog.Close

        onOpened: {
            citySearchField.text = App.settings.weatherCity;
            citySearchField.forceActiveFocus();
        }

        ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing
                Kirigami.SearchField {
                    id: citySearchField
                    Layout.fillWidth: true
                    placeholderText: i18nc("@info:placeholder", "Type city name (e.g. São Paulo)…")
                    onAccepted: App.summary.searchWeatherCities(text)
                }
                QQC2.Button {
                    icon.name: "search"
                    text: i18nc("@action:button", "Search")
                    onClicked: App.summary.searchWeatherCities(citySearchField.text)
                }
            }

            QQC2.Label {
                visible: App.summary.citySearchResults.length === 0
                Layout.fillWidth: true
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                text: i18nc("city search hint", "Enter at least 2 characters and click Search to select your city.")
                wrapMode: Text.Wrap
            }

            Repeater {
                model: App.summary.citySearchResults
                delegate: QQC2.ItemDelegate {
                    required property var modelData
                    Layout.fillWidth: true
                    icon.name: "mark-location"
                    text: modelData.label
                    onClicked: {
                        App.summary.selectWeatherCity(modelData.name, modelData.latitude, modelData.longitude);
                        cityDialog.close();
                    }
                }
            }
        }
    }

    Dialogs.FileDialog {
        id: extraIcsDialog
        title: i18nc("@title:window", "Choose Local iCalendar File")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [i18nc("file type", "iCalendar files (*.ics *.ical)"), i18nc("file type", "All files (*)")]
        onAccepted: App.summary.addExtraIcsFile(selectedFile)
    }

    // ---------- General ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "General")
    }
    FormCard.FormCard {
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Keep running when the window is closed")
            description: i18nc("@info", "Needed for reminders to appear while the window is closed.")
            checked: App.settings.runInBackground
            onToggled: App.settings.runInBackground = checked
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Start with the session")
            description: i18nc("@info", "Starts hidden in the system tray when you log in.")
            checked: App.autostart
            onToggled: App.autostart = checked
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Show an icon in the system tray")
            checked: App.settings.showTrayIcon
            onToggled: App.settings.showTrayIcon = checked
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Show the number of tasks due on the task manager icon")
            checked: App.settings.showBadge
            onToggled: App.settings.showBadge = checked
        }
        FormCard.FormComboBoxDelegate {
            id: defaultList
            readonly property var entries: { App.revision; return App.lists().filter(l => !l.readOnly); }
            text: i18nc("@label:listbox", "List for new tasks")
            description: i18nc("@info", "Used when a task is added outside of a list, e.g. in “Today”.")
            model: entries.map(l => l.account !== "" ? i18nc("list name (account name)", "%1 (%2)", l.name, l.account) : l.name)
            currentIndex: Math.max(0, entries.findIndex(l => l.id === App.settings.defaultListId))
            onActivated: index => App.settings.defaultListId = entries[index].id
        }
    }

    // ---------- Shortcuts ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Keyboard")
    }
    FormCard.FormCard {
        FormCard.FormButtonDelegate {
            icon.name: "configure-shortcuts"
            text: i18nc("@action:button", "Configure Keyboard Shortcuts…")
            onClicked: App.trigger("options_configure_keybinding")
        }
        FormCard.FormButtonDelegate {
            icon.name: "preferences-desktop-keyboard-shortcut"
            text: i18nc("@action:button", "Shortcut to Add a Task from Anywhere")
            description: App.globalShortcut !== "" ? i18nc("@info %1 is a keyboard shortcut", "Currently %1. Change it in System Settings.", App.globalShortcut)
                                                   : i18nc("@info", "Not set. Assign one in System Settings.")
            onClicked: App.openGlobalShortcutSettings()
        }
    }

    // ---------- Data & Local Backup ----------
    FormCard.FormHeader {
        title: i18nc("@title:group", "Data & Local Backup")
    }
    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18nc("@label", "Backup folder")
            description: App.settings.backupDirectory !== "" ? App.settings.backupDirectory : App.backup.defaultDirectory()
            trailing: QQC2.Button {
                icon.name: "folder-open"
                text: i18nc("@action:button", "Choose…")
                onClicked: backupFolderDialog.open()
            }
        }
        FormCard.FormButtonDelegate {
            icon.name: "document-save"
            text: i18nc("@action:button", "Create Local Backup Now")
            description: App.backup.lastBackupText
            onClicked: App.backup.createBackupNow()
        }
        FormCard.FormButtonDelegate {
            icon.name: "document-open"
            text: i18nc("@action:button", "Restore from Backup File…")
            description: i18nc("@info", "Restores tasks, routines, and journal entries from a local .db backup.")
            onClicked: restoreBackupDialog.open()
        }
        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Automatic local backups")
            description: i18nc("@info", "Keeps a local snapshot on your computer alongside Nextcloud sync.")
            checked: App.settings.autoBackupEnabled
            onToggled: App.settings.autoBackupEnabled = checked
        }
        FormCard.FormComboBoxDelegate {
            enabled: App.settings.autoBackupEnabled
            readonly property var values: [1, 24, 168]
            text: i18nc("@label:listbox", "Backup frequency")
            model: [ i18nc("backup interval", "Hourly"), i18nc("backup interval", "Daily"), i18nc("backup interval", "Weekly") ]
            currentIndex: Math.max(0, values.indexOf(App.settings.autoBackupIntervalHours))
            onActivated: index => App.settings.autoBackupIntervalHours = values[index]
        }
        FormCard.FormComboBoxDelegate {
            readonly property var values: [5, 10, 20, 50]
            text: i18nc("@label:listbox", "Keep recent backups")
            model: [ i18nc("retention", "Last 5 backups"), i18nc("retention", "Last 10 backups"), i18nc("retention", "Last 20 backups"), i18nc("retention", "Last 50 backups") ]
            currentIndex: Math.max(0, values.indexOf(App.settings.backupRetentionCount))
            onActivated: index => App.settings.backupRetentionCount = values[index]
        }
        FormCard.FormDelegateSeparator {}
        FormCard.FormButtonDelegate {
            icon.name: "document-import"
            text: i18nc("@action:button", "Import from iCalendar File…")
            onClicked: App.trigger("import_ics")
        }
        FormCard.FormButtonDelegate {
            icon.name: "document-export"
            text: i18nc("@action:button", "Export All Tasks to iCalendar File…")
            onClicked: {
                App.tasks.view = "all";
                App.trigger("export_ics");
            }
        }
        FormCard.FormButtonDelegate {
            icon.name: "document-print"
            text: i18nc("@action:button", "Print / Export PDF…")
            onClicked: App.trigger("print_tasks")
        }
        FormCard.FormButtonDelegate {
            icon.name: "help-about"
            text: i18nc("@action:button", "About KTasks")
            onClicked: App.trigger("open_about_page")
        }
    }

    Dialogs.FolderDialog {
        id: backupFolderDialog
        title: i18nc("@title:window", "Choose Backup Folder")
        onAccepted: App.backup.setDirectoryFromUrl(selectedFolder)
    }
    Dialogs.FileDialog {
        id: restoreBackupDialog
        title: i18nc("@title:window", "Restore Local Backup")
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: [i18nc("file type", "KTasks backup (*.db)"), i18nc("file type", "All files (*)")]
        onAccepted: {
            confirmRestore.fileUrl = selectedFile;
            confirmRestore.open();
        }
    }
    Kirigami.PromptDialog {
        id: confirmRestore
        property url fileUrl
        title: i18nc("@title:window", "Restore Backup?")
        subtitle: i18nc("@info", "Current local data will be replaced by the selected backup (a safety copy of your current database will be saved first).")
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "document-open"
                text: i18nc("@action:button", "Restore")
                onTriggered: {
                    App.backup.restoreFromFile(confirmRestore.fileUrl);
                    confirmRestore.close();
                }
            }
        ]
    }

    Kirigami.PromptDialog {
        id: removeAccount
        property var accountId: 0
        property string accountName: ""
        title: i18nc("@title:window", "Remove Account?")
        subtitle: i18nc("@info", "The lists of “%1” are removed from this device. Nothing is deleted on the server. Changes that were not synchronized yet are lost.", accountName)
        standardButtons: Kirigami.Dialog.Cancel
        customFooterActions: [
            Kirigami.Action {
                icon.name: "edit-delete"
                text: i18nc("@action:button", "Remove")
                onTriggered: {
                    App.sync.removeAccount(removeAccount.accountId);
                    removeAccount.close();
                }
            }
        ]
    }
}
