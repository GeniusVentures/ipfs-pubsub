
add_library(gossip_common
    utility.cpp
    console_async_reader.cpp
    )

target_link_libraries(gossip_common
    p2p_basic_host
    p2p_default_network
    p2p_peer_repository
    p2p_inmem_address_repository
    p2p_inmem_key_repository
    p2p_inmem_protocol_repository
    p2p_gossip
    asio_scheduler
    Boost::program_options    
    )

add_executable(gossip_test
    gossip_chat_test.cpp
    )

set_target_properties(gossip_test PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
target_link_libraries(gossip_test PUBLIC
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
install(TARGETS gossip_test)