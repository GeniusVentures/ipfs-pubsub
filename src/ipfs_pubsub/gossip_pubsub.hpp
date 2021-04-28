#ifndef GRPC_FOR_SUPERGENIUS_GOSSIBSUB
#define GRPC_FOR_SUPERGENIUS_GOSSIBSUB

#include <string>

// boost/asio/io_context.hpp must be included before libp2p.
// Otherwise an error of the second including of winsock.h is thrown on Windows
#include <boost/asio/io_context.hpp>

#include <libp2p/peer/peer_info.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/protocol/gossip/gossip.hpp>
#include <libp2p/common/logger.hpp>
#include <libp2p/outcome/outcome.hpp>

namespace sgns::ipfs_pubsub
{
/**
* Codes for errors that originate in gossip pubsub
*/
enum class GossipPubSubError 
{
    INVALID_LOCAL_ADDRESS = 1,
    FAILED_LOCAL_ADDRESS_LISTENING,
    FAILED_SERVICE_START,
};

/**
* Gossip pubsub service.
* Allows to subscribe to a named channel (topic) using linp2p Gossip pub-sub protocol
* and publish short messages to the named channels
*/
class GossipPubSub final
{
public:
    typedef libp2p::protocol::gossip::Gossip::Message Message;
    typedef libp2p::protocol::Subscription Subscription;
    typedef libp2p::protocol::gossip::Gossip::SubscriptionCallback MessageCallback;

    /** Creates a gossip subscription service.
    */
    GossipPubSub();
    ~GossipPubSub();

    /** Subscribes the service to a specified topic.
    * @param topic - a topic to subscribe to
    * @param onMessageCallback - callback which is invoked when the topic messages are received
    * @return subscription handler.
    */ 
    std::future<Subscription> Subscribe(const std::string& topic, MessageCallback onMessageCallback);

    /** Publish a string message to specific topic.
    * @param topic - a topic to publish a message to.
    * @param message - published message
    */
    void Publish(const std::string& topic, const std::vector<uint8_t>& message);

    /** Starts the gossip pubsub service.
    * @param listeningPort - is a port for incoming connections.
    * @param booststrapPeers - a list of remote peers that are connected to the current mesh
    * @return future object that indicates if the service is started.
    */
    std::future<std::error_code>
        Start(int listeningPort, const std::vector<std::string>& booststrapPeers);

    /** Stops the gossip pubsub service.
    */
    void Stop();

    /** Checks if the service is started.
    * @return flag tha indicates if the service is started
    */
    bool IsStarted() const;

    /** Returns the current service local address.
    * @return a string representation of the current peer local multi-address
    */
    const std::string& GetLocalAddress();

private:

    std::shared_ptr<boost::asio::io_context> m_context;
    std::unique_ptr<boost::asio::io_context::strand> m_strand;
    std::shared_ptr<libp2p::Host> m_host;
    std::shared_ptr<libp2p::protocol::gossip::Gossip> m_gossip;
    std::unique_ptr<std::thread> _serviceThread;
    std::string m_localAddress;
    libp2p::common::Logger m_logger;
};

inline bool GossipPubSub::IsStarted() const
{
    return _serviceThread ? true : false;
}

inline const std::string& GossipPubSub::GetLocalAddress()
{
    return m_localAddress;
}

}

OUTCOME_HPP_DECLARE_ERROR_2(sgns::ipfs_pubsub, GossipPubSubError);

#endif // GRPC_FOR_SUPERGENIUS_GOSSIBSUB
