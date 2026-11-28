add_library(ktasksapp STATIC
    application.h application.cpp
    settings.h settings.cpp
    taskstore.h taskstore.cpp
    taskmodel.h taskmodel.cpp
    eisenhowermanager.h eisenhowermanager.cpp
    discordpresence.h discordpresence.cpp
    focusmanager.h focusmanager.cpp
    analyticsmanager.h analyticsmanager.cpp
    summarymanager.h summarymanager.cpp
    sidebarmodel.h sidebarmodel.cpp
    syncmanager.h syncmanager.cpp
    notifier.h notifier.cpp
    journalmanager.h journalmanager.cpp
    routinemanager.h routinemanager.cpp
    backupmanager.h backupmanager.cpp
    pdfexporter.h pdfexporter.cpp
)

target_compile_definitions(ktasksapp PRIVATE TRANSLATION_DOMAIN="ktasks")
target_include_directories(ktasksapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ktasksapp PUBLIC
    ktaskscore
    Qt6::Qml Qt6::Quick Qt6::Multimedia
    KF6::CoreAddons KF6::ConfigCore KF6::ConfigGui KF6::I18n KF6::Notifications
    KF6::ColorScheme
    KirigamiAddonsStatefulApp
    qt6keychain
)
if(ANDROID)
    target_link_libraries(ktasksapp PUBLIC Qt6::Svg KF6::Kirigami OpenSSL::SSL)
else()
    # Desktop only: tray icon, printing, window activation, icon theme setup.
    target_link_libraries(ktasksapp PUBLIC Qt6::Widgets Qt6::PrintSupport KF6::StatusNotifierItem KF6::WindowSystem KF6::IconThemes)
    target_compile_definitions(ktasksapp PUBLIC KTASKS_HAVE_TRAY=1 KTASKS_HAVE_PRINTER=1 KTASKS_HAVE_WINDOWSYSTEM=1)
endif()
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(ktasksapp PUBLIC Qt6::DBus KF6::GlobalAccel)
    target_compile_definitions(ktasksapp PUBLIC KTASKS_HAVE_DBUS=1 KTASKS_HAVE_GLOBALACCEL=1)
endif()
if(TARGET KPim6::AkonadiCore)
    target_link_libraries(ktasksapp PUBLIC KPim6::AkonadiCore)
    target_compile_definitions(ktasksapp PRIVATE KTASKS_HAVE_AKONADI=1)
endif()

if(WIN32)
    add_executable(ktasks WIN32 main.cpp ${CMAKE_SOURCE_DIR}/data/ktasks.rc)
else()
    add_executable(ktasks main.cpp)
endif()
target_compile_definitions(ktasks PRIVATE
    TRANSLATION_DOMAIN="ktasks"
    KTASKS_VERSION="${PROJECT_VERSION}"
)

qt_add_qml_module(ktasks
    URI app.ktasks
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Sidebar.qml
        qml/SummaryPage.qml
        qml/TasksPage.qml
        qml/TaskDelegate.qml
        qml/TaskDetails.qml
        qml/QuickEntry.qml
        qml/QuickAddWindow.qml
        qml/ListDialog.qml
        qml/SettingsPage.qml
        qml/AccountDialog.qml
        qml/Chip.qml
        qml/MarkdownEditor.qml
        qml/JournalPage.qml
        qml/RoutinePage.qml
        qml/EisenhowerPage.qml
        qml/InsightsPage.qml
        qml/PrintDialog.qml
        qml/PaddedSpinBox.qml
        qml/DurationPicker.qml
        qml/GuardedDialog.qml
)

target_link_libraries(ktasks PRIVATE
    ktasksapp
    Qt6::QuickControls2
    KF6::I18nQml
)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(ktasks PRIVATE KF6::DBusAddons KF6::Crash)
    target_compile_definitions(ktasks PRIVATE KTASKS_HAVE_CRASH=1)
endif()

qt_add_resources(ktasks "sounds"
    PREFIX /sounds
    BASE ${CMAKE_SOURCE_DIR}/data/sounds
    FILES ${CMAKE_SOURCE_DIR}/data/sounds/task-complete.wav
)

install(TARGETS ktasks ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

if(ANDROID)
    include(ECMAddAndroidApk)
    ecm_add_android_apk(ktasks ANDROID_DIR ${CMAKE_SOURCE_DIR}/android)
    include(${CMAKE_CURRENT_SOURCE_DIR}/androidicons.cmake)
endif()
