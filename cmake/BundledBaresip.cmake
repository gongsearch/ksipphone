# Optional: build a private, static baresip (with libre) and ship it with KSipPhone.
# Used for distributions whose baresip is too old (Debian/Ubuntu ship 1.1) - see KSIPPHONE_BUNDLE_BARESIP.
include(ExternalProject)

set(KSIPPHONE_BARESIP_TAG "v4.6.0" CACHE STRING "re/baresip release to bundle")
set(KSIPPHONE_BARESIP_MODULES "g711;g722;opus;l16;pipewire;auconv;auresamp;srtp;menu;mwi;ctrl_tcp;webrtc_aec"
    CACHE STRING "baresip modules to build in (missing optional libraries are skipped by baresip)")
set(_bundle "${CMAKE_BINARY_DIR}/bundled-baresip")
string(REPLACE ";" "|" _mods "${KSIPPHONE_BARESIP_MODULES}")

ExternalProject_Add(bundled_re
    URL https://github.com/baresip/re/archive/refs/tags/${KSIPPHONE_BARESIP_TAG}.tar.gz
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${_bundle}
               -DLIBRE_BUILD_SHARED=OFF -DLIBRE_BUILD_STATIC=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
               "-DCMAKE_C_FLAGS=-Wno-error=incompatible-pointer-types -Wno-deprecated-declarations"
    BUILD_BYPRODUCTS ${_bundle}/lib/libre.a
)
ExternalProject_Add(bundled_baresip
    DEPENDS bundled_re
    URL https://github.com/baresip/baresip/archive/refs/tags/${KSIPPHONE_BARESIP_TAG}.tar.gz
    LIST_SEPARATOR |
    CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${_bundle} -DCMAKE_PREFIX_PATH=${_bundle}
               -DSTATIC=ON -DMODULES=${_mods}
    BUILD_BYPRODUCTS ${_bundle}/bin/baresip
)
add_dependencies(ksipphone bundled_baresip)
set(KSIPPHONE_STATIC_BARESIP_MODULES "${KSIPPHONE_BARESIP_MODULES}" CACHE STRING "" FORCE)

# installed privately; KSipPhone finds it through the compiled-in paths below
set(_privdir "${KDE_INSTALL_LIBDIR}/ksipphone")
install(PROGRAMS ${_bundle}/bin/baresip DESTINATION ${_privdir})
install(DIRECTORY ${_bundle}/share/baresip/ DESTINATION ${KDE_INSTALL_DATADIR}/ksipphone/baresip FILES_MATCHING PATTERN "*.wav")
set(BARESIP_EXECUTABLE "${CMAKE_INSTALL_PREFIX}/${_privdir}/baresip" CACHE FILEPATH "" FORCE)
set(BARESIP_MODULE_PATH "${CMAKE_INSTALL_PREFIX}/${_privdir}/modules" CACHE PATH "" FORCE) # static build: unused
if (APPLE)
    # inside KSipPhone.app: Contents/MacOS/baresip, sounds in Contents/Resources/baresip (relative to the executable)
    set(BARESIP_SHARE_PATH "../Resources/baresip" CACHE PATH "" FORCE)
else()
    set(BARESIP_SHARE_PATH "${CMAKE_INSTALL_PREFIX}/${KDE_INSTALL_DATADIR}/ksipphone/baresip" CACHE PATH "" FORCE)
endif()
