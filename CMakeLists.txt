cmake_minimum_required(VERSION 3.20)
project(ksipphone VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

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

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Concurrent Network Multimedia)
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
endif()
set_package_properties(KF6Wallet PROPERTIES TYPE OPTIONAL PURPOSE "migrate passwords from KSipPhone 0.1")

set(BARESIP_MODULE_PATH "/usr/lib/baresip/modules" CACHE PATH "Where baresip's modules are installed")
set(BARESIP_SHARE_PATH "/usr/share/baresip" CACHE PATH "Where baresip's sound files are installed")

find_library(LDAP_LIBRARY NAMES ldap REQUIRED)
find_library(LBER_LIBRARY NAMES lber REQUIRED)
find_path(LDAP_INCLUDE_DIR ldap.h REQUIRED)

add_subdirectory(src)
ki18n_install(po)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
