###
#   brief  : CMake file for ipfs pubsub project
#   date   : 05/30/2020
#   author : Ruymaster
###

add_executable(ipfs_pubsub_test  test.cpp)
target_link_libraries(ipfs_pubsub_test pubsubroom)
