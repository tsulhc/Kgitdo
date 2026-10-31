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
    Qt6::Quick
    Qt6::Qml
    Qt6::Gui
    Qt6::QuickControls2
    Qt6::Widgets
    KF6::I18n
    KF6::CoreAddons
    KF6::IconThemes
)

install(TARGETS kgitdo ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
