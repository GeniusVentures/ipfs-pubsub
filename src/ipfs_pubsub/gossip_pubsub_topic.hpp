#ifndef GRPC_FOR_SUPERGENIUS_GOSSIBSUB_TOPIC
#define GRPC_FOR_SUPERGENIUS_GOSSIBSUB_TOPIC

#include "ipfs_pubsub/gossip_pubsub.hpp"

namespace sgns::ipfs_pubsub
{
/**
* Gossip pubsub service.
* Allows to subscribe to a named channel (topic) using linp2p Gossip pub-sub protocol
* and publish short messages to the named channels
*/
class GossipPubSubTopic final
{
public:
    typedef libp2p::protocol::gossip::Gossip::Message Message;
    typedef libp2p::protocol::Subscription Subscription;
    typedef libp2p::protocol::gossip::Gossip::SubscriptionCallback MessageCallback;

    /** Creates a gossip topic wrapper.
    * gossipPubSub - pubsub service
    * @param topic - a topic id
    */
    GossipPubSubTopic(std::shared_ptr<GossipPubSub> gossipPubSub, std::string topic);

    /** Subscribes the object to specified topic.
    * @param onMessageCallback - callback which is invoked when the topic messages are received
    * @return subscription handler.
    */
    void Subscribe(MessageCallback onMessageCallback);

    /** Cancels subscription.
    */
    void Unsubscribe();

    /** Publish a string message to specific topic.
    * @param topic - a topic to publish a message to.
    * @param message - published message
    */
    void Publish(const std::string& message);

    const std::string& GetTopic() const;
private:
    std::shared_ptr<GossipPubSub> m_gossipPubSub;
    std::string m_topic;
    std::future<Subscription> m_subscription;
};

inline const std::string& GossipPubSubTopic::GetTopic() const
{
    return m_topic;
}

}

#endif // GRPC_FOR_SUPERGENIUS_GOSSIBSUB
