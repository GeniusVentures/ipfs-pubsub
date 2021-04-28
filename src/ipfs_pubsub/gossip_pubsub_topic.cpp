#include "ipfs_pubsub/gossip_pubsub_topic.hpp"

namespace sgns::ipfs_pubsub
{
GossipPubSubTopic::GossipPubSubTopic(
    std::shared_ptr<GossipPubSub> gossipPubSub, std::string topic)
    : m_gossipPubSub(gossipPubSub)
    , m_topic(topic)
{
}

void GossipPubSubTopic::Subscribe(MessageCallback onMessageCallback)
{
    m_subscription = m_gossipPubSub->Subscribe(m_topic, onMessageCallback);
    m_subscription.wait();
}

void GossipPubSubTopic::Publish(const std::string & message)
{
    m_gossipPubSub->Publish(m_topic, message);
}

void GossipPubSubTopic::Unsubscribe()
{
    m_subscription.get().cancel();
}
}
