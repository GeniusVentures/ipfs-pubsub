#ifndef IPFS_PUBSUB_GOSSIP_PUBSUB_TOPIC
#define IPFS_PUBSUB_GOSSIP_PUBSUB_TOPIC

#include <ipfs_pubsub/gossip_pubsub.hpp>

namespace sgns::ipfs_pubsub
{
/**
* A wrapper for pubsub topic.
* Allows to subscribe to a named channel (topic) using GossipPubSub service
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
    * @param message - published message
    */
    void Publish(const std::string& message);

    /** Publish binary data to specific topic
    * @param message - published message data
    */
    void Publish(const std::vector<uint8_t>& message);

    /** Returns topic id
    * @return topic id
    */
    const std::string& GetTopic() const;
    const std::shared_ptr<GossipPubSub> GetPubsub() const;

private:
    std::shared_ptr<GossipPubSub> m_gossipPubSub;
    std::string m_topic;
    std::future<Subscription> m_subscription;
};

inline const std::string& GossipPubSubTopic::GetTopic() const
{
    return m_topic;
}
inline const std::shared_ptr<GossipPubSub> GossipPubSubTopic::GetPubsub() const
{
    return m_gossipPubSub;
}

}

#endif // IPFS_PUBSUB_GOSSIP_PUBSUB_TOPIC
