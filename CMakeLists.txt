cmake_minimum_required(VERSION 3.28)

project(kgitdo
    VERSION 0.1.0
    DESCRIPTION "KDE-native to-do list backed by GitHub Issues"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMFindQmlModule)
include(ECMQmlModule)
include(FeatureSummary)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    Widgets
    Test
)

find_package(KF6 REQUIRED COMPONENTS
    Kirigami
    I18n
    CoreAddons
    QQC2DesktopStyle
    IconThemes
    Wallet
)

ecm_find_qmlmodule(org.kde.kirigami REQUIRED)

option(BUILD_TESTING "Build automated tests" ON)
if(BUILD_TESTING)
    enable_testing()
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(PROGRAMS data/org.kde.kgitdo.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/org.kde.kgitdo.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES icons/sc-apps-org.kde.kgitdo.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps/
    RENAME org.kde.kgitdo.svg
)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
