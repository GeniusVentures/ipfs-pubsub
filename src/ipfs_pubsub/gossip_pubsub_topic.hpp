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
    using Message              = libp2p::protocol::gossip::Gossip::Message;
    using Subscription         = libp2p::protocol::Subscription;
    using SubscriptionCallback = libp2p::protocol::gossip::Gossip::SubscriptionCallback;

    /** Creates a gossip topic wrapper.
    * gossipPubSub - pubsub service
    * @param topic - a topic id
    */
    GossipPubSubTopic( std::shared_ptr<GossipPubSub> gossipPubSub, std::string topic );

    /** Subscribes the object to specified topic.
    * @param onMessageCallback - callback which is invoked when the topic messages are received
    * @param subscribeNow - wait for the subscription to become active
    * @return subscription handler.
    */
    std::future<Subscription> &Subscribe( SubscriptionCallback onMessageCallback, bool subscribeNow = false );

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
    [[nodiscard]] const std::string &GetTopic() const
    {
        return m_topic;
    }

    [[nodiscard]] const std::shared_ptr<GossipPubSub> GetPubsub() const
    {
        return m_gossipPubSub;
    }

private:
    std::shared_ptr<GossipPubSub> m_gossipPubSub;
    std::string                   m_topic;
    std::future<Subscription>     m_subscription;
};
}

#endif // IPFS_PUBSUB_GOSSIP_PUBSUB_TOPIC
