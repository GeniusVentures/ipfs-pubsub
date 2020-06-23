#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#
cmake_minimum_required(VERSION 3.12)

find_program(CCACHE_FOUND ccache)
if (CCACHE_FOUND)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif (CCACHE_FOUND)

set(CMAKE_TOOLCHAIN_FILE
    "${CMAKE_SOURCE_DIR}/cmake/toolchain/cxx17.cmake"
    CACHE
    FILEPATH
    "Default toolchain"
    )

project(gossip_test C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TESTING "Build tests" ON)
option(BUILD_EXAMPLES "Build examples" ON)
option(CLANG_FORMAT "Enable clang-format target" ON)
option(CLANG_TIDY "Enable clang-tidy checks during compilation" OFF)
option(COVERAGE "Enable generation of coverage info" OFF)
# sanitizers will be enabled only for libp2p, and will be disabled for dependencies
option(ASAN "Enable address sanitizer" OFF)
option(LSAN "Enable leak sanitizer" OFF)
option(MSAN "Enable memory sanitizer" OFF)
option(TSAN "Enable thread sanitizer" OFF)
option(UBSAN "Enable UB sanitizer" OFF)

include(CheckCXXCompilerFlag)
include(cmake/dependencies.cmake)
include(cmake/functions.cmake)
include(cmake/san.cmake)
include(cmake/print.cmake)
print("C flags: ${CMAKE_C_FLAGS}")
print("CXX flags: ${CMAKE_CXX_FLAGS}")
print("Using CMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")


## setup compilation flags
if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "^(AppleClang|Clang|GNU)$")
  # enable those flags
  add_flag(-Wall)
  add_flag(-Wextra)
  add_flag(-Woverloaded-virtual)     # warn if you overload (not override) a virtual function
  add_flag(-Wformat=2)               # warn on security issues around functions that format output (ie printf)
  add_flag(-Wmisleading-indentation) # (only in GCC >= 6.0) warn if indentation implies blocks where blocks do not exist
  add_flag(-Wduplicated-cond)        # (only in GCC >= 6.0) warn if if / else chain has duplicated conditions
  add_flag(-Wduplicated-branches)    # (only in GCC >= 7.0) warn if if / else branches have duplicated code
  add_flag(-Wnull-dereference)       # (only in GCC >= 6.0) warn if a null dereference is detected
  add_flag(-Wdouble-promotion)       # (GCC >= 4.6, Clang >= 3.8) warn if float is implicit promoted to double
  add_flag(-Wsign-compare)
  add_flag(-Wtype-limits)            # size_t - size_t >= 0 -> always true

  # disable those flags
  add_flag(-Wno-unused-command-line-argument)    # clang: warning: argument unused during compilation: '--coverage' [-Wunused-command-line-argument]
  add_flag(-Wno-unused-parameter)    # prints too many useless warnings
  add_flag(-Wno-format-nonliteral)   # prints way too many warnings from spdlog
  add_flag(-Wno-gnu-zero-variadic-macro-arguments)   # https://stackoverflow.com/questions/21266380/is-the-gnu-zero-variadic-macro-arguments-safe-to-ignore

  # promote to errors
  add_flag(-Werror-unused-lambda-capture)  # error if lambda capture is unused
  add_flag(-Werror-return-type)      # warning: control reaches end of non-void function [-Wreturn-type]
  add_flag(-Werror-non-virtual-dtor) # warn the user if a class with virtual functions has a non-virtual destructor. This helps catch hard to track down memory errors
  add_flag(-Werror-sign-compare)     # warn the user if they compare a signed and unsigned numbers
  add_flag(-Werror-reorder)          # field '$1' will be initialized after field '$2'
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  # using Visual Studio C++
  # TODO(warchant): add flags https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md#msvc
endif ()

if (COVERAGE)
  include(cmake/coverage.cmake)
endif ()
if (CLANG_TIDY)
  include(cmake/clang-tidy.cmake)
endif ()
if (CLANG_FORMAT)
  include(cmake/clang-format.cmake)
endif ()

#message("${EXTERNAL_BUILD_DIR}/hat-trie/include")

include_directories(${EXTERNAL_BUILD_DIR}/hat-trie/include)
include_directories(
    SYSTEM
    # project includes    
    ${libp2p_INCLUDE_DIR}
    ${libp2p_INCLUDE_DIR}/platform/${CMAKE_SYSTEM_NAME}        
)

link_directories(${Boost_LIB_DIR_STR})
link_directories(${EXTERNAL_BUILD_DIR}/libp2p/lib)
if(Boost_FOUND)
    message("Boost is found!")
endif()

add_executable(gossip_test
    gossip_chat_example.cpp
    )

set_target_properties(gossip_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
target_link_libraries(gossip_test PRIVATE
    gossip_common
    p2p_basic_host
    p2p_default_network
    p2p_peer_repository
    p2p_inmem_address_repository
    p2p_inmem_key_repository
    p2p_inmem_protocol_repository
    #---gossip
    p2p_gossip
    p2p_gossip_proto
    #---crypto -
    p2p_aes_provider
    p2p_crypto_provider
    p2p_ecdsa_provider
    p2p_ed25519_provider
    p2p_hmac_provider
    p2p_random_generator
    p2p_secp256k1_provider
    p2p_key_marshaller
    p2p_key_validator
    p2p_aes_provider
    p2p_rsa_provider
    p2p_sha
    p2p_crypto_key
    p2p_crypto_error
    p2p_crypto_common
    p2p_keys_proto
    #-- multihash --
    p2p_multihash
    p2p_uvarint
    p2p_multiaddress
    p2p_cid
    p2p_multibase_codec
    p2p_converters
    p2p_address_repository
    p2p_peer_errors
    p2p_peer_id
    p2p_peer_address
    p2p_identity_manager
    p2p_peer_repository
    p2p_mplex
    p2p_mplexed_connection
    p2p_yamux    
    p2p_yamuxed_connection
    p2p_hexutil
    p2p_logger
    p2p_byteutil    
    p2p_literals
    p2p_default_network
    p2p_router
    p2p_dialer
    p2p_network
    p2p_transport_manager
    p2p_connection_manager
    p2p_listener_manager
    p2p_kad
    p2p_multiselect
    scheduler
    subscription
    #--- security ---
    p2p_security_error
    p2p_secio
    p2p_secio_protobuf
    p2p_secio_propose_message_marshaller
    p2p_secio_exchange_message_marshaller
    p2p_plaintext_exchange_message_marshaller
    p2p_plaintext
    p2p_plaintext_protobuf
    # ---transport --
    p2p_transport_parser
    p2p_upgrader
    p2p_upgrader_session
    p2p_tcp_listener
    p2p_tcp_connection
    p2p_tcp
    #--- p2p basic ---
    p2p_varint_reader
    p2p_message_read_writer_error
    p2p_message_read_writer
    p2p_protobuf_message_read_writer
    #--- p2p protocol ----

    # --- spdlog ----
    spdlog::spdlog
    protobuf::libprotobuf
    protobuf::libprotoc

    asio_scheduler
    Boost::program_options 
    OpenSSL::Crypto
    )
