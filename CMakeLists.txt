###
#   brief  : CMake file for ipfs pubsub project
#   date   : 05/30/2020
#   author : Ruymaster
###


cmake_minimum_required(VERSION 3.10)

project(ipfs-pubsub VERSION 1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
include(cmake/functions.cmake)
include(ExternalProject)
#include(${CMAKE_ROOT}/Modules/ExternalProject.cmake)


set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../geniustokens/thirdparty")
set(JSON_FOR_MODERN_CXX_INCLUDE_DIR "${THIRDPARTY_DIR}/json")

# Find json library
find_path(JSON_FOR_MODERN_CXX_INCLUDE_DIR NAMES nlohmann/json.hpp)
include_directories(${JSON_FOR_MODERN_CXX_INCLUDE_DIR})
mark_as_advanced(JSON_FOR_MODERN_CXX_INCLUDE_DIR)

# Build protobuf project from the git submodule.
ExternalProject_Add(protobuf
  PREFIX protobuf
  SOURCE_DIR "${THIRDPARTY_DIR}/grpc/third_party/protobuf/cmake"
  CMAKE_CACHE_ARGS
        -Dprotobuf_BUILD_TESTS:BOOL=OFF
        -Dprotobuf_WITH_ZLIB:BOOL=OFF
        -Dprotobuf_MSVC_STATIC_RUNTIME:BOOL=OFF
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/protobuf
)

# The location where protobuf-config.cmake will be installed varies by platform
if (WIN32)
set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/cmake")
else()
set(_FINDPACKAGE_PROTOBUF_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib/cmake/protobuf")
endif()
set(_FINDPACKAGE_PROTOBUF_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/include")
set(_FINDPACKAGE_PROTOBUF_LIB_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobuf/lib")


# If OPENSSL_ROOT_DIR is set, propagate that hint path to the external projects with OpenSSL dependency.
set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "")
if (OPENSSL_ROOT_DIR)
  set(_CMAKE_ARGS_OPENSSL_ROOT_DIR "-DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR}")
endif()

include_directories(${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/include)
#message(${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/include)
add_executable(ipfs-pubsub  "${CMAKE_SOURCE_DIR}/src/test/test.cpp")

#message("${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/Release/ipfs-http-client")
target_link_libraries(ipfs-pubsub "${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/Release/ipfs-http-client")