///=====================================================================
/// \file pubsubroom.cpp
/// \brief  Implement class to create room and subscribe and publish messages in room 
/// 
/// \date 06/09/2020
/// \author Ruymaster
///=====================================================================

#include "pubsubroom.hpp"
//#include <spdlog/fmt/fmt.h>
#include <libp2p/protocol/gossip/impl/local_subscriptions.hpp>
#include <libp2p/protocol/gossip/gossip.hpp>
#include <libp2p/injector/gossip_injector.hpp>

#define VERBOSE 0
using namespace std;
namespace g = libp2p::protocol::gossip;

namespace pubsubroom
{  
       
    /// \param overriding default config to see local messages as well (echo mode)
    
    // PubsubRoom:: PubsubRoom( bool bLocalNode=true )
    // {
    //     this->config.echo_forward_mode = true;
    // }

    /// \param @node_peer_port: port number for peer 
    /// \param @topicName
    PubsubRoom:: PubsubRoom( std::string topicName, std::int_least16_t nodePeerPort=0 ){
        
        if(nodePeerPort>0) peerPortNum = nodePeerPort;

        config.echo_forward_mode = true;

        /// injector creates and ties dependent objects
        auto injector = libp2p::injector::makeGossipInjector( libp2p::injector::useGossipConfig(config) );

        /// create gossip node
        auto gossip =   injector.create<std::shared_ptr<libp2p::protocol::gossip::Gossip>>();

        /// create asio context
        auto io = injector.create<std::shared_ptr<boost::asio::io_context>>();

        /// host is our local libp2p node
        auto host = injector.create<std::shared_ptr<libp2p::Host>>();
         
        /// make peer uri of local node
        auto local_address_str =  fmt::format("/ip4/{}/tcp/{}/p2p/{}", utility::getLocalIP(*io), peerPortNum, host->getId().toBase58());


    }
    PubsubRoom::~ PubsubRoom()
    {

    }
    /// \public functions 
    std::array<std::string, MAX_PEER_COUNT>  PubsubRoom:: getPeers()
    {        
        return this->m_peers;
    }
    bool PubsubRoom:: hasPeer(string peer)
    {
        return true;        
    }
    void PubsubRoom:: joinRoom(std::string topicName){

    }
    void PubsubRoom:: broadcastToRoom(std::string message){

    }
    void PubsubRoom:: leaveRoom()
    {
        
    }
    void PubsubRoom:: sendTo(std::string peer, std::string message){

    }

} // namespace PubsubRoom