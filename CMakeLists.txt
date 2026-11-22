cmake_minimum_required(VERSION 3.22)
project(ktasks VERSION 1.0.3 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.5.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)
include(ECMInstallIcons)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Sql Network DBus PrintSupport)
find_package(OpenSSL REQUIRED)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons Config I18n CalendarCore Notifications DBusAddons
    StatusNotifierItem ColorScheme GlobalAccel WindowSystem Crash IconThemes)
find_package(KF6Kirigami ${KF_MIN_VERSION} REQUIRED)
find_package(KF6KirigamiAddons 1.4 REQUIRED)
find_package(Qt6Keychain REQUIRED)
find_package(KPim6Akonadi QUIET COMPONENTS Core)

qt_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

remove_definitions(-DQT_USE_QSTRINGBUILDER -DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_TO_ASCII -DQT_NO_CAST_FROM_BYTEARRAY -DQT_NO_URL_CAST_FROM_STRING)

add_subdirectory(src/core)
add_subdirectory(src/app)

option(BUILD_TESTING "Build the tests" ON)
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()

ki18n_install(po)

install(FILES data/io.github.marleys439.ktasks.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/io.github.marleys439.ktasks.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES data/ktasks.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install(FILES data/io.github.marleys439.ktasks.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
install(FILES data/io.github.marleys439.ktasks-symbolic.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/symbolic/apps)
install(FILES data/io.github.marleys439.ktasks-symbolic.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/status)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
