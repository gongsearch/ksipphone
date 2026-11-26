cmake_minimum_required(VERSION 3.20)
project(ksipphone VERSION 0.2.4 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

# ON: Plasma integration (KStatusNotifierItem, KNotifications with buttons). OFF: plain Qt tray and
# messages (Windows, macOS). KI18n is used in both cases.
if (WIN32 OR APPLE)
    set(_kde_default OFF)
else()
    set(_kde_default ON)
endif()
option(KSIPPHONE_WITH_KDE "Use KDE Frameworks for tray and notifications" ${_kde_default})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

# We use no deprecated KF API; hiding it also avoids KI18n 6.32 headers that older GCC (11) cannot parse.
add_compile_definitions(KF_DISABLE_DEPRECATED_BEFORE_AND_AT=0x060800)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Multimedia)
if (UNIX AND NOT APPLE)
    find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS DBus) # GNOME / Evolution address book
endif()
if (APPLE)
    enable_language(OBJCXX) # macOS Contacts
endif()
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS I18n)
if (KSIPPHONE_WITH_KDE)
    find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons Notifications StatusNotifierItem)
else()
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Svg) # bundled SVG icons
endif()
find_package(Qt6Keychain REQUIRED)

# Optional: read passwords stored by KSipPhone 0.1 in KWallet once and move them to the keychain.
if (KSIPPHONE_WITH_KDE)
    find_package(KF6Wallet ${KF_MIN_VERSION} QUIET)
    # Optional: KDE address book (Akonadi, KDE Connect phone contacts, vCards) as second contact source
    find_package(KF6People ${KF_MIN_VERSION} QUIET)
    set_package_properties(KF6People PROPERTIES TYPE OPTIONAL PURPOSE "names and photos from the KDE address book")
endif()
set_package_properties(KF6Wallet PROPERTIES TYPE OPTIONAL PURPOSE "migrate passwords from KSipPhone 0.1")

set(BARESIP_MODULE_PATH "/usr/lib/baresip/modules" CACHE PATH "Where baresip's modules are installed")
set(BARESIP_SHARE_PATH "/usr/share/baresip" CACHE PATH "Where baresip's sound files are installed")
set(BARESIP_EXECUTABLE "baresip" CACHE STRING "baresip executable (name in PATH or absolute path)")
option(KSIPPHONE_BUNDLE_BARESIP "Build a private static baresip and install it with KSipPhone" OFF)
set(KSIPPHONE_STATIC_BARESIP_MODULES "" CACHE STRING "Modules compiled into a shipped static baresip (';' separated); empty = look for module files")

find_library(LDAP_LIBRARY NAMES ldap REQUIRED)
find_library(LBER_LIBRARY NAMES lber REQUIRED)
find_path(LDAP_INCLUDE_DIR ldap.h REQUIRED)

add_subdirectory(src)
if (KSIPPHONE_BUNDLE_BARESIP)
    include(BundledBaresip)
endif()
# ';' would split the compiler definition into several arguments: pass the module list comma separated
string(REPLACE ";" "," _static_modules "${KSIPPHONE_STATIC_BARESIP_MODULES}")
target_compile_definitions(ksipphone PRIVATE
    KSIPPHONE_BARESIP_MODULE_PATH="${BARESIP_MODULE_PATH}" KSIPPHONE_BARESIP_SHARE_PATH="${BARESIP_SHARE_PATH}"
    KSIPPHONE_BARESIP_EXECUTABLE="${BARESIP_EXECUTABLE}"
    KSIPPHONE_STATIC_BARESIP_MODULES="${_static_modules}")
include(Packaging)
ki18n_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
