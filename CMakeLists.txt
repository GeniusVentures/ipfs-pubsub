###
#   brief  : CMake file for ipfs pubsub project
#   date   : 05/30/2020
#   author : Ruymaster
###


cmake_minimum_required(VERSION 3.10)

project(ipfs-pubsub C CXX)

include(cmake/functions.cmake)
include(ExternalProject)

# Set Third party directory 
set(THIRDPARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(JSON_FOR_MODERN_CXX_INCLUDE_DIR "${THIRDPARTY_DIR}/json")

# Find json library
find_path(JSON_FOR_MODERN_CXX_INCLUDE_DIR NAMES nlohmann/json.hpp)
include_directories(${JSON_FOR_MODERN_CXX_INCLUDE_DIR})
mark_as_advanced(JSON_FOR_MODERN_CXX_INCLUDE_DIR)

set(PROTOBUF_ROOT_DIR "${THIRDPARTY_DIR}/grpc/third_party/protobuf")
set(PROTOBUF_SRC_DIR "${PROTOBUF_ROOT_DIR}/cmake")

# Build protobuf project from the git submodule.
ExternalProject_Add(protobuf
  PREFIX protobuf
  SOURCE_DIR "${PROTOBUF_SRC_DIR}"
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

# Builds Microsoft.GSL
ExternalProject_Add(Microsoft.GSL
  PREFIX Microsoft.GSL
  SOURCE_DIR "${THIRDPARTY_DIR}/GSL"
  CMAKE_CACHE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/GSL
        -DGSL_TEST:BOOL=OFF
)
set(_FINDPACKAGE_GSL_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/GSL/include")

# Builds spdlog v1.4.2
ExternalProject_Add(spdlog
  PREFIX spdlog
  SOURCE_DIR "${THIRDPARTY_DIR}/spdlog"
  CMAKE_CACHE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/spdlog
)
set(_FINDPACKAGE_SPDLOG_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/spdlog/lib/spdlog/cmake")
set(_FINDPACKAGE_SPDLOG_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/spdlog/include")

# Builds tsl_hat_trie 343e0dac54fc8491065e8a059a02db9a2b1248ab
ExternalProject_Add(tsl_hat_trie
  PREFIX tsl_hat_trie
  SOURCE_DIR "${THIRDPARTY_DIR}/hat-trie"
  CMAKE_CACHE_ARGS
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/hat-trie
)
set(_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/hat-trie/lib/cmake/tsl_hat_trie")

#set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH} ${CMAKE_CURRENT_BINARY_DIR}/hat-trie/lib/cmake/tsl_hat_trie")
#message("${CMAKE_PREFIX_PATH}")

# Builds Boost.DI c5287ee710ad90f5286d0cc2b9e49b72d89267a6
ExternalProject_Add(Boost.DI
  PREFIX Boost.DI
  SOURCE_DIR "${THIRDPARTY_DIR}/Boost.DI"
  CMAKE_CACHE_ARGS
        -DBOOST_DI_OPT_BUILD_TESTS:BOOL=OFF
        -DBOOST_DI_OPT_BUILD_EXAMPLES:BOOL=OFF
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/Boost.DI
)
set(_FINDPACKAGE_BOOST_DI_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/Boost.DI/lib/cmake/Boost.DI")

# BOOST library 
set(Boost_USE_STATIC_LIBS ON) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF)
set(Boost_ROOT ${BOOST_ROOT})
#find_package(Boost REQUIRED COMPONENTS date_time filesystem random) # header only libraries must not be added here

#OpenSSL library
set(CMAKE_USE_OPENSSL ON)
if(CMAKE_USE_OPENSSL)
  find_package(OpenSSL REQUIRED)
  set(SSL_ENABLED ON)
  set(USE_OPENSSL ON)
endif()

#set(tsl_hat_trie_DIR "${_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR}")
#find_package(tsl_hat_trie  REQUIRED)
# Builds libp2p
#message(${CMAKE_CXX_FLAGS})
ExternalProject_Add(libp2p
  PREFIX libp2p
  SOURCE_DIR "${THIRDPARTY_DIR}/libp2p"
  CMAKE_CACHE_ARGS
        ${_CMAKE_ARGS_OPENSSL_ROOT_DIR}
        -DCMAKE_USE_OPENSSL:BOOL=ON
        -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_CURRENT_BINARY_DIR}/libp2p
        -DTESTING:BOOL=OFF 
        -DBUILD_EXAMPLES:BOOL=OFF
        -DBUILD_TESTING:BOOL=OFF
        -DEXPOSE_MOCKS:BOOL=ON
        -DHUNTER_ENABLED:BOOL=OFF
        -DBUILD_EXAMPLES:BOOL=OFF
        -DProtobuf_DIR:PATH=${_FINDPACKAGE_PROTOBUF_CONFIG_DIR}
        -DProtobuf_INCLUDE_DIR:PATH=${_FINDPACKAGE_PROTOBUF_INCLUDE_DIR}
        -DProtobuf_LIBRARIES:PATH=${_FINDPACKAGE_PROTOBUF_LIB_DIR}
        -Dspdlog_DIR:PATH=${_FINDPACKAGE_SPDLOG_CONFIG_DIR}
        -DBoost.DI_DIR:PATH=${_FINDPACKAGE_BOOST_DI_CONFIG_DIR}
        -DBoost_ROOT:PATH=${BOOST_ROOT}
        -DBoost_INCLUDE_DIR:PATH=${BOOST_INCLUDE_DIR}
        -DBoost_LIBRARY_DIR:PATH=${BOOST_LIBRARY_DIR}
        -Dtsl_hat_trie_DIR:PATH=${_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR}
        -DGSL_ROOT:PATH=${CMAKE_CURRENT_BINARY_DIR}/GSL
        -DGSL_DIR:PATH=${CMAKE_CURRENT_BINARY_DIR}/GSL
        -DGSL_INCLUDE_DIR:PATH=${_FINDPACKAGE_GSL_INCLUDE_DIR}
        -DCMAKE_FIND_USE_PACKAGE_ROOT_PATH:BOOL=ON
        -DCLANG_TIDY:BOOL=OFF
        -DCMAKE_CXX_FLAGS:STRING=-I\ ${_FINDPACKAGE_GSL_INCLUDE_DIR}\ -I\ ${_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR}\ -I\ ${_FINDPACKAGE_SPDLOG_INCLUDE_DIR}\ -I\ \\\"${OPENSSL_INCLUDE_DIR}\\\"\ -I\ ${_FINDPACKAGE_PROTOBUF_INCLUDE_DIR}
  DEPENDS protobuf spdlog tsl_hat_trie Boost.DI
)

set(libp2p_DIR "${CMAKE_CURRENT_BINARY_DIR}/libp2p/lib/cmake/libp2p")

# Find packages for common
#message("libp2p_DIR=" ${libp2p_DIR})
#find_package(libp2p CONFIG REQUIRED)

#set(spdlog_DIR "${_FINDPACKAGE_SPDLOG_CONFIG_DIR}")
#find_package(spdlog CONFIG REQUIRED)
#set(tsl_hat_trie_DIR "${_FINDPACKAGE_TSL_HAT_TRIE_CONFIG_DIR}")
#find_package(tsl_hat_trie CONFIG REQUIRED)


include_directories(${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/include)

#message(${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/include)
#add_executable(ipfs-pubsub  "${CMAKE_SOURCE_DIR}/src/test/test.cpp")

#message("${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/Release/ipfs-http-client")

#target_link_libraries(ipfs-pubsub "${THIRDPARTY_DIR}/cpp-ipfs-http-client/build/Windows/Release/ipfs-http-client")