add_executable(ksipphone
    main.cpp
    engine.cpp
    phonebook.cpp
    mainwindow.cpp
    settingsdialog.cpp
    phoneapp.cpp
    ringer.cpp
    secrets.cpp
)

target_include_directories(ksipphone PRIVATE ${LDAP_INCLUDE_DIR})
target_compile_definitions(ksipphone PRIVATE LDAP_DEPRECATED=0 KSIPPHONE_VERSION="${PROJECT_VERSION}"
    KSIPPHONE_BARESIP_MODULE_PATH="${BARESIP_MODULE_PATH}" KSIPPHONE_BARESIP_SHARE_PATH="${BARESIP_SHARE_PATH}")

target_link_libraries(ksipphone PRIVATE
    Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Multimedia Qt6Keychain::Qt6Keychain
    KF6::CoreAddons KF6::I18n KF6::ConfigCore KF6::Notifications
    KF6::StatusNotifierItem KF6::WidgetsAddons
    ${LDAP_LIBRARY} ${LBER_LIBRARY}
)

if (KF6Wallet_FOUND)
    target_link_libraries(ksipphone PRIVATE KF6::Wallet)
    target_compile_definitions(ksipphone PRIVATE HAVE_KWALLET)
endif()

install(TARGETS ksipphone ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES ksipphone.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
configure_file(io.github.gongsearch.KSipPhone.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.gongsearch.KSipPhone.desktop @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/io.github.gongsearch.KSipPhone.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES io.github.gongsearch.KSipPhone.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
