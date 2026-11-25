cmake_minimum_required(VERSION 3.20)
project(ksipphone VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.6.0")
set(KF_MIN_VERSION "6.0.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets DBus Concurrent Network Multimedia)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons I18n Config Notifications StatusNotifierItem DBusAddons Wallet WidgetsAddons)

find_library(LDAP_LIBRARY NAMES ldap REQUIRED)
find_library(LBER_LIBRARY NAMES lber REQUIRED)
find_path(LDAP_INCLUDE_DIR ldap.h REQUIRED)

add_subdirectory(src)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
