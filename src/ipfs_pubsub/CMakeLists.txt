add_library(ipfs-pubsub
    gossip_pubsub.cpp
    gossip_pubsub.hpp
    gossip_pubsub_topic.cpp
    gossip_pubsub_topic.hpp
    logger.cpp
    logger.hpp
    )

message("DIRECTORY FOR IPFS LITE : ${ipfs-lite-cpp_INCLUDE_DIR}")

target_include_directories(ipfs-pubsub 
    PRIVATE ${GSL_INCLUDE_DIR} ${ipfs-lite-cpp_INCLUDE_DIR}
    PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
)

target_link_libraries(ipfs-pubsub
    OpenSSL::Crypto
    OpenSSL::SSL
    Boost::Boost.DI
    p2p::p2p_basic_host
    p2p::p2p_default_network
    p2p::p2p_peer_repository
    p2p::p2p_inmem_address_repository
    p2p::p2p_inmem_key_repository
    p2p::p2p_inmem_protocol_repository
    p2p::p2p_gossip
    p2p::asio_scheduler
    p2p::p2p_kademlia
    ipfs-lite-cpp::ipfs_kad_dht
    spdlog::spdlog
    )
