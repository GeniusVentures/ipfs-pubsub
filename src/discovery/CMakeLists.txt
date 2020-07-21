
#link_directories(${Boost_LIB_DIR_STR})

add_executable(kad_peer_discovery
    kad_peer_discovery.cpp
    factory.cpp
    )
set_target_properties(kad_peer_discovery PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
target_link_libraries(kad_peer_discovery
    p2p_basic_host
    p2p_default_network
    p2p_peer_repository
    p2p_inmem_address_repository
    p2p_inmem_key_repository
    p2p_inmem_protocol_repository
    p2p_literals
    p2p_kad
    asio_scheduler
    )
