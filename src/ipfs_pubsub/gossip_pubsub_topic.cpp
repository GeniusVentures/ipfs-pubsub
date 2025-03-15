#include "ipfs_pubsub/gossip_pubsub_topic.hpp"

namespace
{
std::vector<uint8_t> StringToArray(const std::string& s)
{
    auto sz = s.size();

    std::vector<uint8_t> data;
    data.reserve( sz );
    data.assign( s.begin(), s.end() );

    return data;
}
}

namespace sgns::ipfs_pubsub
{
    GossipPubSubTopic::GossipPubSubTopic( std::shared_ptr<GossipPubSub> gossipPubSub, std::string topic ) :
        m_gossipPubSub( std::move( gossipPubSub ) ), m_topic( topic )
    {
    }

    std::future<GossipPubSubTopic::Subscription> &GossipPubSubTopic::Subscribe( SubscriptionCallback onMessageCallback, bool subscribeNow )
    {
        m_subscription = m_gossipPubSub->Subscribe(m_topic, onMessageCallback);
        if (subscribeNow)
        {
            m_subscription.wait();
        }
        return m_subscription;
    }

    void GossipPubSubTopic::Publish(const std::string & message)
    {
        Publish( StringToArray( message ) );
    }

    void GossipPubSubTopic::Publish(const std::vector<uint8_t>& message)
    {
        m_gossipPubSub->Publish(m_topic, message);
    }

    void GossipPubSubTopic::Unsubscribe()
    {
        m_subscription.get().cancel();
    }
}
