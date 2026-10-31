add_library(kgitdoauth STATIC
    AuthSession.cpp
    AuthSession.h
    AuthTransport.h
    DeviceFlowProtocol.cpp
    DeviceFlowProtocol.h
    GitHubAppConfig.cpp
    GitHubAppConfig.h
    KWalletSecureStore.cpp
    KWalletSecureStore.h
    QNetworkAuthTransport.cpp
    QNetworkAuthTransport.h
    SecureStore.h
    WalletBackend.h
)

target_include_directories(kgitdoauth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(kgitdoauth PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    KF6::Wallet
    KF6::I18n
    KF6::CoreAddons
)

add_library(kgitdoissues STATIC
    GitHubClient.cpp
    GitHubClient.h
    Issue.h
    IssueApi.h
    IssueController.cpp
    IssueController.h
    IssueListModel.cpp
    IssueListModel.h
    IssueQuery.cpp
    IssueQuery.h
    QNetworkIssueApi.cpp
    QNetworkIssueApi.h
)

target_include_directories(kgitdoissues PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(kgitdoissues PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    kgitdoauth
)

add_executable(kgitdo)

ecm_add_qml_module(kgitdo
    URI org.kde.kgitdo
)

target_sources(kgitdo PRIVATE
    main.cpp
)

ecm_target_qml_sources(kgitdo SOURCES
    Main.qml
)

target_link_libraries(kgitdo PRIVATE
    kgitdoauth
    kgitdoissues
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::Network
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::I18n
    KF6::CoreAddons
    KF6::IconThemes
)

install(TARGETS kgitdo ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
