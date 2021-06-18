
addtest(ipfs-pubsub-test
    gossip_pubsub_test.cpp
    gossip_pubsub_topic_test.cpp
    )

target_include_directories(ipfs-pubsub-test PRIVATE ${GSL_INCLUDE_DIR})

target_link_libraries(ipfs-pubsub-test
    ipfs-pubsub
    ${WIN_CRYPT_LIBRARY}
    )

