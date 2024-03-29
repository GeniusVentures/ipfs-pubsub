cmake_minimum_required(VERSION 3.12)

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif (CCACHE_FOUND)

set(
    CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/cmake/toolchain/cxx17.cmake"
    CACHE
    FILEPATH
    "Default toolchain"
)
# Project
project(ipfs-pubsub C CXX)

if (NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
  # https://cgold.readthedocs.io/en/latest/tutorials/toolchain/globals/cxx-standard.html#summary
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif ()

include(cmake/functions.cmake)
include(cmake/print.cmake)
print("C flags: ${CMAKE_C_FLAGS}")
print("CXX flags: ${CMAKE_CXX_FLAGS}")
print("C Debug flags: ${CMAKE_C_FLAGS_DEBUG}")
print("CXX Debug flags: ${CMAKE_CXX_FLAGS_DEBUG}")
print("C Release flags: ${CMAKE_C_FLAGS_RELEASE}")
print("CXX Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
include(cmake/install.cmake)

# Package config ######################################################################################
set (CPACK_PACKAGE_VERSION_MAJOR "1")
set (CPACK_PACKAGE_VERSION_MINOR "0")
set (CPACK_PACKAGE_VERSION_PATCH "0")
set (CPACK_PACKAGE_VERSION_PRE_RELEASE "12")
set (CPACK_PACKAGE_VENDOR "Genius Ventures")

# Export flag    ######################################################################################
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CheckCXXCompilerFlag)

# Boost definition ######################################################################################
# option (SGNS_STACKTRACE_BACKTRACE "Use BOOST_STACKTRACE_USE_BACKTRACE in stacktraces, for POSIX" OFF)
# if (SGNS_STACKTRACE_BACKTRACE)
# 	add_definitions(-DSGNS_STACKTRACE_BACKTRACE=1)
# 	if (BACKTRACE_INCLUDE)
# 		add_definitions(-DBOOST_STACKTRACE_BACKTRACE_INCLUDE_FILE=${BACKTRACE_INCLUDE})
# 	endif()
# endif ()

# BOOST library 
set(Boost_USE_STATIC_LIBS ON) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME ON) 
find_package(Boost REQUIRED COMPONENTS date_time filesystem random regex system thread log log_setup program_options) # header only libraries must not be added here

# OpenSSL library
find_package(OpenSSL REQUIRED)

# Thirdparty libraries
find_package(spdlog CONFIG REQUIRED)
find_package(tsl_hat_trie CONFIG REQUIRED)
find_package(libp2p CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(leveldb CONFIG REQUIRED)
find_package(ipfs-lite-cpp CONFIG REQUIRED)

include_directories(  
  SYSTEM
  ${PROJECT_SOURCE_DIR}/src
  ${CBOR_INCLUDE_DIR}  
  ${leveldb_INCLUDE_DIR}  
  ${libp2p_INCLUDE_DIR}
  ${libp2p_INCLUDE_DIR}/platform/${CMAKE_SYSTEM_NAME}
)


message("----${libp2p_LIBRARY_DIR}")
option(TESTING "Build tests" ON)
link_directories(
  ${BOOST_LIBRARY_DIR}
  ${ipfs-lite-cpp_LIB_DIR}
  ${libp2p_LIBRARY_DIR}
)

add_subdirectory(src)


# if (TESTING)
#   find_package(GTest CONFIG REQUIRED)
#   enable_testing()
#   add_subdirectory(test)
# endif ()