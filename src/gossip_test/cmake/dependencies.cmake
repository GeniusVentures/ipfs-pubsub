#
# Copyright Soramitsu Co., Ltd. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0
#

if (TESTING)
  find_package(GTest CONFIG REQUIRED)
  # find_package(GMock CONFIG REQUIRED)
endif()

# find_package(Boost CONFIG REQUIRED  random filesystem program_options)
find_package(Boost REQUIRED  random filesystem program_options)
find_package(OpenSSL REQUIRED)

#---- updated by ruymaster ----#
find_package(Protobuf CONFIG REQUIRED)
#find_package(Protobuf REQUIRED)

find_package(spdlog CONFIG REQUIRED)
# find_package(spdlog REQUIRED)
find_package(tsl_hat_trie CONFIG REQUIRED)
#find_package(tsl_hat_trie REQUIRED)

find_package(Boost.DI CONFIG REQUIRED)

find_package(libp2p CONFIG REQUIRED)
