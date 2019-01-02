cmake_minimum_required(VERSION 3.5.0 FATAL_ERROR) #CPACK_DEBIAN_<COMPONENT>_PACKAGE_NAME

find_package(IRODS 5.0.0 REQUIRED)

set(CMAKE_C_COMPILER ${IRODS_EXTERNALS_FULLPATH_CLANG}/bin/clang)
set(CMAKE_CXX_COMPILER ${IRODS_EXTERNALS_FULLPATH_CLANG}/bin/clang++)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")

project(irods_database_plugin-cockroachdb CXX)

include(${IRODS_TARGETS_PATH})

set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

if (NOT CPACK_PACKAGING_INSTALL_PREFIX)
  set(CPACK_PACKAGING_INSTALL_PREFIX "/" CACHE STRING "Package root path. \"/\" is correct for normal package builds.." FORCE)
  message(STATUS "Setting unspecified CPACK_PACKAGING_INSTALL_PREFIX to '${CPACK_PACKAGING_INSTALL_PREFIX}'. This is the correct setting for normal builds.")
endif()


if (NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build {Debug, Release}." FORCE)
  message(STATUS "Setting unspecified CMAKE_BUILD_TYPE to '${CMAKE_BUILD_TYPE}'")
endif()

if (NOT IRODS_PYTHON_VERSION)
  set(IRODS_PYTHON_VERSION "2" CACHE STRING "The version of python to link against" FORCE)
  message(STATUS "Setting unspecified IRODS_PYTHON_VERSION to '${IRODS_PYTHON_VERSION}'. This is the correct setting for normal builds.")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} -Wl,-z,defs")
add_compile_options(-nostdinc++)
add_compile_options(-std=c++14)
link_libraries(c++abi)
include_directories(${IRODS_EXTERNALS_FULLPATH_CLANG}/include/c++/v1)

set(
  IRODS_DATABASE_PLUGIN_COMPILE_DEFINITIONS_cockroachdb
  )

set(
  IRODS_DATABASE_PLUGINS
  cockroachdb
  )

#configure_file(
#  ${CMAKE_SOURCE_DIR}/src/icatSysTables.sql.pp.in
#  ${CMAKE_BINARY_DIR}/src/icatSysTables.sql.pp
#  @ONLY
#  )

foreach(PLUGIN ${IRODS_DATABASE_PLUGINS})
  string(TOUPPER ${PLUGIN} PLUGIN_UPPERCASE)

  #  add_custom_command(
  #  OUTPUT ${CMAKE_BINARY_DIR}/icatSysTables_${PLUGIN}.sql
  #  COMMAND cpp -E -P -D${PLUGIN} ${CMAKE_BINARY_DIR}/src/icatSysTables.sql.pp ${CMAKE_BINARY_DIR}/icatSysTables_${PLUGIN}.sql
  #  MAIN_DEPENDENCY ${CMAKE_BINARY_DIR}/src/icatSysTables.sql.pp
  #  )
  #add_custom_target(IRODS_PHONY_TARGET_icatSysTables_${PLUGIN}.sql ALL DEPENDS ${CMAKE_BINARY_DIR}/icatSysTables_${PLUGIN}.sql) # Forces execution of custom_command

  add_library(
    ${PLUGIN}
    SHARED
    ${CMAKE_SOURCE_DIR}/src/db_plugin.cpp
    ${CMAKE_SOURCE_DIR}/src/general_query.cpp
    ${CMAKE_SOURCE_DIR}/src/general_query_setup.cpp
    ${CMAKE_SOURCE_DIR}/src/general_update.cpp
    ${CMAKE_SOURCE_DIR}/src/irods_catalog_properties.cpp
    ${CMAKE_SOURCE_DIR}/src/irods_sql_logger.cpp
    ${CMAKE_SOURCE_DIR}/src/low_level_libpq.cpp
    ${CMAKE_SOURCE_DIR}/src/mid_level_routines.cpp
    )

  target_include_directories(
    ${PLUGIN}
    PRIVATE
    /usr/include/irods
    /usr/include/postgresql
    ${CMAKE_SOURCE_DIR}/include
    ${IRODS_EXTERNALS_FULLPATH_BOOST}/include
    ${IRODS_EXTERNALS_FULLPATH_JANSSON}/include
    ${IRODS_EXTERNALS_FULLPATH_AVRO}/include/
    )

  target_link_libraries(
    ${PLUGIN}
    PRIVATE
    irods_server
    irods_plugin_dependencies
    irods_common
    ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_system.so
    ${IRODS_EXTERNALS_FULLPATH_BOOST}/lib/libboost_regex.so
    pq
    )

  target_compile_definitions(${PLUGIN} PRIVATE ENABLE_RE ${IRODS_DATABASE_PLUGIN_COMPILE_DEFINITIONS_${PLUGIN}} ${IRODS_COMPILE_DEFINITIONS} BOOST_SYSTEM_NO_DEPRECATED)
  target_compile_options(${PLUGIN} PRIVATE -Wno-write-strings)
  set_property(TARGET ${PLUGIN} PROPERTY CXX_STANDARD ${IRODS_CXX_STANDARD})

  set(
    IRODS_DATABASE_PLUGIN_COCKROACHDB_HEADERS
    ${CMAKE_SOURCE_DIR}/include/low_level.hpp
    ${CMAKE_SOURCE_DIR}/include/low_level_libpq.hpp
    ${CMAKE_SOURCE_DIR}/include/mid_level.hpp
    )

  install(
    FILES
    ${IRODS_DATABASE_PLUGIN_COCKROACHDB_HEADERS}
    DESTINATION usr/include/irods/cockroachdb
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    )
    
  install(
    TARGETS
    ${PLUGIN}
    LIBRARY
    DESTINATION ${IRODS_PLUGINS_DIRECTORY}/database
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    )

  install(
    FILES
    ${CMAKE_SOURCE_DIR}/${PLUGIN}/upgrade-3.3.xto4.0.0.sql
    DESTINATION ${IRODS_HOME_DIRECTORY}/packaging
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    )

  install(
    FILES
    ${CMAKE_SOURCE_DIR}/packaging/database_plugin_control.py
    DESTINATION ${IRODS_HOME_DIRECTORY}/scripts/irods
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    )

  install(
    FILES
    ${CMAKE_SOURCE_DIR}/packaging/localhost_setup_${PLUGIN}.input
    DESTINATION ${IRODS_HOME_DIRECTORY}/packaging
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    )

  install(
    FILES
    ${CMAKE_SOURCE_DIR}/src/icatDropSysTables.sql
    ${CMAKE_SOURCE_DIR}/src/icatPurgeRecycleBin.sql
    ${CMAKE_SOURCE_DIR}/src/icatSysTables.sql
    DESTINATION ${IRODS_HOME_DIRECTORY}/packaging/sql
    COMPONENT ${IRODS_PACKAGE_COMPONENT_${PLUGIN_UPPERCASE}_NAME}
    PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    )

endforeach()

set(CPACK_PACKAGE_FILE_NAME "irods-database-plugin-cockroachdb${IRODS_PACKAGE_FILE_NAME_SUFFIX}")

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_COMPONENTS_GROUPING IGNORE)
set(CPACK_PACKAGE_VERSION ${IRODS_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${IRODS_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${IRODS_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${IRODS_VERSION_PATCH})
set(CPACK_PACKAGE_CONTACT "Renaissance Computing Institute <info@irods.org>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "The integrated Rule-Oriented Data System")
set(CPACK_PACKAGE_VENDOR "Renaissance Computing Institute <info@irods.org>")

set(CPACK_DEB_COMPONENT_INSTALL OFF)
set(CPACK_DEBIAN_PACKAGE_SECTION "contrib/science")
set(CPACK_DEBIAN_COMPRESSION_TYPE "gzip")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "extra")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://irods.org")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)

set(CPACK_RPM_COMPONENT_INSTALL OFF)
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_LICENSE "BSD-3-Clause")
set(CPACK_RPM_PACKAGE_VENDOR "iRODS Consortium")
set(CPACK_RPM_PACKAGE_URL "https://irods.org")
set(CPACK_RPM_PACKAGE_AUTOREQ 0)
set(CPACK_RPM_PACKAGE_AUTOPROV 0)

set(CPACK_RPM_PRE_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/preinst")
set(CPACK_RPM_POST_INSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/postinst")
set(CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE "${CMAKE_SOURCE_DIR}/packaging/postrm")

set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)

set(CPACK_DEBIAN_PACKAGE_NAME "irods-database-plugin-cockroachdb")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "${IRODS_PACKAGE_DEPENDENCIES_STRING}, irods-runtime (= ${IRODS_VERSION}), irods-server (= ${IRODS_VERSION}), libpq5, super, libc6")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "${CMAKE_SOURCE_DIR}/packaging/preinst;${CMAKE_SOURCE_DIR}/packaging/postinst;${CMAKE_SOURCE_DIR}/packaging/postrm;")

set(CPACK_RPM_PACKAGE_NAME "irods-database-plugin-cockroachdb")
if (IRODS_LINUX_DISTRIBUTION_NAME STREQUAL "centos" OR IRODS_LINUX_DISTRIBUTION_NAME STREQUAL "centos linux")
  set(CPACK_RPM_PACKAGE_REQUIRES "${IRODS_PACKAGE_DEPENDENCIES_STRING}, irods-runtime = ${IRODS_VERSION}, openssl, python-devel")
elseif (IRODS_LINUX_DISTRIBUTION_NAME STREQUAL "opensuse")
  set(CPACK_RPM_PACKAGE_REQUIRES "${IRODS_PACKAGE_DEPENDENCIES_STRING}, irods-runtime = ${IRODS_VERSION}, libopenssl1_0_0")
endif()

list(APPEND
    CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
     "${CPACK_PACKAGING_INSTALL_PREFIX}etc/irods")

list(APPEND
    CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
     "${CPACK_PACKAGING_INSTALL_PREFIX}var")

list(APPEND
    CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
     "${CPACK_PACKAGING_INSTALL_PREFIX}var/lib")

list(APPEND
    CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
     "${CPACK_PACKAGING_INSTALL_PREFIX}var/lib/irods")

list(APPEND
    CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
     "${CPACK_PACKAGING_INSTALL_PREFIX}var/lib/irods/scripts")

if (NOT CPACK_GENERATOR)
    set(CPACK_GENERATOR ${IRODS_CPACK_GENERATOR} CACHE STRING "CPack generator to use, e.g. {DEB, RPM, TGZ}." FORCE)
    message(STATUS "Setting unspecified CPACK_GENERATOR to ${CPACK_GENERATOR}. This is the correct setting for normal builds.")
endif()

include(CPack)
