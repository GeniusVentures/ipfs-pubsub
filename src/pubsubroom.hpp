///=====================================================================
/// \file pubsubroom.h
/// \brief  Create room and subscribe and publish messages in room 
/// 
/// \date 06/09/2020
/// \author Ruymaster
///=====================================================================

#ifndef PUBSUBROOM_HPP
#define PUBSUBROOM_HPP

#include <string>
#include <cstdint>
#include <memory>
#include <vector>
#include <array>
#include <iostream>

#include <spdlog/fmt/fmt.h>
#include <boost/program_options.hpp>
#include <libp2p/protocol/gossip/impl/local_subscriptions.hpp>
#include "utility.hpp"

using namespace std;

#include <libp2p/protocol/gossip/gossip.hpp>


/// \max number that can be connected in one room
#define MAX_PEER_COUNT 1024


namespace g = libp2p::protocol::gossip;

namespace pubsubroom
{
    class  PubsubRoom
    {
    private:
        /// \variable for storing topic name
        std::string m_topicName;
        /// \variable for room id. each room has unique roomId and topic name
        int_least64_t roomId;
        /// \varialbe for room topic variable in libp2p
        g::TopicId  topicId;
        /// \variable save info for peers in room.
        std::array<std::string, MAX_PEER_COUNT> m_peers={};
        g::Config config;    
        std::int_least16_t peerPortNum = 5001;
        /* data */
    public:
        /// \construncter and destrunctor for pubsubroom
        // PubsubRoom( bool bLocalNode=true );
        PubsubRoom( std::string topicName, std::int_least16_t nodePeerPort );
        ~ PubsubRoom();
        /// \interfaces 
        std::array<std::string, MAX_PEER_COUNT> getPeers();
        /// \interface for joining specific room
        void joinRoom(std::string topicName);
        void leaveRoom();
        bool hasPeer(string peer);
        void sendTo(std::string peer, std::string message);
        void broadcastToRoom(std::string message);
    };
}

#endif