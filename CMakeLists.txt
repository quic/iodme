cmake_minimum_required(VERSION 3.15)
project(iodme LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FIXME: set these from Git
set(IODME_MAJOR_VERSION 0)
set(IODME_MINOR_VERSION 1)
set(IODME_PATCH_VERSION 0)
set(IODME_VERSION "${IODME_MAJOR_VERSION}.${IODME_MINOR_VERSION}.${IODME_PATCH_VERSION}")

# Set default pref to /usr (unless user provided an override)
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "..." FORCE)
endif()

option(WITH_TESTS "enable IODME tests" ON)

find_package(Boost 1.58.0 REQUIRED)
find_package(HOGL REQUIRED)

enable_testing()
add_subdirectory(src)
add_subdirectory(tools)

if (WITH_TESTS)
	add_subdirectory(tests)
endif()

set(CONF_VERSION "${IODME_VERSION}")
set(CONF_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
set(CONF_LIBRARY_DIR "${CMAKE_INSTALL_PREFIX}/lib")
configure_file(iodme-config.cmake.in
	"${PROJECT_BINARY_DIR}/iodme-config.cmake" @ONLY)
configure_file(iodme-config.cmake.in
	"${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/iodme-config.cmake" @ONLY)
configure_file(iodme-config-version.cmake.in
	"${PROJECT_BINARY_DIR}/iodme-config-version.cmake" @ONLY)

install(FILES
	"${PROJECT_BINARY_DIR}${CMAKE_FILES_DIRECTORY}/iodme-config.cmake"
	"${PROJECT_BINARY_DIR}/iodme-config-version.cmake"
	DESTINATION lib/cmake/iodme COMPONENT dev)

# DEB packaging
set(CPACK_PACKAGE_VENDOR  "Max Krasnyansky")
set(CPACK_PACKAGE_CONTACT "quic_maxk@quicinc.com")
set(CPACK_PACKAGE_VERSION_MAJOR "${IODME_MAJOR_VERSION}")
set(CPACK_PACKAGE_VERSION_MINOR "${IODME_MINOR_VERSION}")
set(CPACK_PACKAGE_VERSION_PATCH "${IODME_PATCH_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CMAKE_PROJECT_NAME}-${IODME_MAJOR_VERSION}.${IODME_MINOR_VERSION}.${IODME_PATCH_VERSION}")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SECTION "util")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

set(CPACK_DEBIAN_DEV_PACKAGE_NAME "iodme-dev")
set(CPACK_DEBIAN_DEV_PACKAGE_DESCRIPTION "IODME library and headers")
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "libboost-dev(>= 1.65.1) libhogl-dev(>= 3.0)")

include(CPack)
