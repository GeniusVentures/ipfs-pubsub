
cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)


project(ipfs-client-test VERSION 1.0)

find_path(JSON_FOR_MODERN_CXX_INCLUDE_DIR NAMES nlohmann/json.hpp)
include_directories(${JSON_FOR_MODERN_CXX_INCLUDE_DIR})
mark_as_advanced(JSON_FOR_MODERN_CXX_INCLUDE_DIR)

include_directories(../thirdparty/cpp-ipfs-http-client/include)

add_executable(ipfs-client-test  ./src/test.cpp)

target_link_libraries(ipfs-client-test ../../thirdparty/cpp-ipfs-http-client/build/Windows/Release/ipfs-http-client)