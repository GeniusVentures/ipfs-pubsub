///=====================================================================
/// \file pubsubroom.cpp
/// \brief  Implement class to create room and subscribe and publish messages in room 
/// 
/// \date 06/09/2020
/// \author Ruymaster
///=====================================================================

#include "pubsubroom.hpp"
//#include <spdlog/fmt/fmt.h>

#define VERBOSE 0
using namespace std;
namespace g = libp2p::protocol::gossip;

namespace PubsubRoom
{  
    PubsubRoom:: PubsubRoom(/* args */)
    {
    }
    PubsubRoom:: PubsubRoom(std::string topicName){
        
    }
    PubsubRoom::~ PubsubRoom()
    {
    }
    //----- public functions 
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