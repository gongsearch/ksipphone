add_executable(ksipphone
    main.cpp
    engine.cpp
    phonebook.cpp
    mainwindow.cpp
    settingsdialog.cpp
    phoneapp.cpp
    ringer.cpp
)

target_include_directories(ksipphone PRIVATE ${LDAP_INCLUDE_DIR})
target_compile_definitions(ksipphone PRIVATE LDAP_DEPRECATED=0 KSIPPHONE_VERSION="${PROJECT_VERSION}")

target_link_libraries(ksipphone PRIVATE
    Qt6::Widgets Qt6::DBus Qt6::Concurrent Qt6::Network Qt6::Multimedia
    KF6::CoreAddons KF6::I18n KF6::ConfigCore KF6::Notifications
    KF6::StatusNotifierItem KF6::DBusAddons KF6::Wallet KF6::WidgetsAddons
    ${LDAP_LIBRARY} ${LBER_LIBRARY}
)

install(TARGETS ksipphone ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ksipphone.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
configure_file(org.ksipphone.KSipPhone.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/org.ksipphone.KSipPhone.desktop @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/org.ksipphone.KSipPhone.desktop DESTINATION ${KDE_INSTALL_APPDIR})
