add_executable(ksipphone
    main.cpp
    engine.cpp
    phonebook.cpp
    mainwindow.cpp
    settingsdialog.cpp
    phoneapp.cpp
    ringer.cpp
    secrets.cpp
    desktop.cpp
)

target_include_directories(ksipphone PRIVATE ${LDAP_INCLUDE_DIR})
target_compile_definitions(ksipphone PRIVATE LDAP_DEPRECATED=0 KSIPPHONE_VERSION="${PROJECT_VERSION}")
# baresip paths are added in the top-level CMakeLists (after an optional bundled baresip set them)

target_link_libraries(ksipphone PRIVATE
    Qt6::Widgets Qt6::Concurrent Qt6::Network Qt6::Multimedia Qt6Keychain::Qt6Keychain
    KF6::I18n
    ${LDAP_LIBRARY} ${LBER_LIBRARY}
)

if (KSIPPHONE_WITH_KDE)
    target_link_libraries(ksipphone PRIVATE KF6::CoreAddons KF6::Notifications KF6::StatusNotifierItem)
    target_compile_definitions(ksipphone PRIVATE KSIPPHONE_WITH_KDE)
else()
    target_sources(ksipphone PRIVATE icons.qrc)
    target_link_libraries(ksipphone PRIVATE Qt6::Svg)
endif()
if (APPLE)
    set_target_properties(ksipphone PROPERTIES
        MACOSX_BUNDLE ON
        OUTPUT_NAME KSipPhone
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER io.github.gongsearch.KSipPhone
        MACOSX_BUNDLE_BUNDLE_NAME KSipPhone
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_ICON_FILE ksipphone.icns)
    if (EXISTS ${CMAKE_SOURCE_DIR}/packaging/macos/ksipphone.icns)
        target_sources(ksipphone PRIVATE ${CMAKE_SOURCE_DIR}/packaging/macos/ksipphone.icns)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/macos/ksipphone.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    endif()
endif()
if (WIN32)
    target_sources(ksipphone PRIVATE ${CMAKE_SOURCE_DIR}/packaging/windows/ksipphone.rc)
    target_link_libraries(ksipphone PRIVATE ws2_32)
    set_target_properties(ksipphone PROPERTIES WIN32_EXECUTABLE ON)
endif()

if (KF6Wallet_FOUND)
    target_link_libraries(ksipphone PRIVATE KF6::Wallet)
    target_compile_definitions(ksipphone PRIVATE HAVE_KWALLET)
endif()

install(TARGETS ksipphone ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
if (KSIPPHONE_WITH_KDE)
    install(FILES ksipphone.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
endif()
configure_file(io.github.gongsearch.KSipPhone.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.gongsearch.KSipPhone.desktop @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/io.github.gongsearch.KSipPhone.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES io.github.gongsearch.KSipPhone.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES icons/app/io.github.gongsearch.KSipPhone.svg DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor/scalable/apps)
