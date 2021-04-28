#include "ipfs_pubsub/gossip_pubsub_topic.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/basic_file_sink.h>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A message is published to a topic that the service is subscribed to.
 * @then The messages is received by the service.
 */
TEST(GossipPubSubTopicTest, TopicSubscription)
{
    std::vector<std::string> receivedMessages;
    auto pubs = std::make_shared<GossipPubSub>();
    pubs->Start(40001, {});

    GossipPubSubTopic pubsTopic1(pubs, "topic1");
    pubsTopic1.Subscribe([&](boost::optional<const GossipPubSub::Message&> message)
    {
        if (message)
        {
            std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
            receivedMessages.push_back(std::move(message));
        }
    });

    std::string message("topic1_message");
    pubsTopic1.Publish(message);

    pubs->Stop();

    ASSERT_EQ(receivedMessages.size(), 1);
    EXPECT_EQ(receivedMessages[0], message);
}

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A topic is unsubsribed.
 * @then No messages rceived.
 */
TEST(GossipPubSubTopicTest, TopicUnsubscription)
{
    std::vector<std::string> receivedMessages;
    auto pubs = std::make_shared<GossipPubSub>();
    pubs->Start(40001, {});

    GossipPubSubTopic pubsTopic1(pubs, "topic1");
    pubsTopic1.Subscribe([&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessages.push_back(std::move(message));
            }
        });

    std::string message1("topic1_message1");
    pubsTopic1.Publish(message1);

    std::this_thread::sleep_for(std::chrono::microseconds(100));

    pubsTopic1.Unsubscribe();

    std::string message2("topic1_message2");
    pubsTopic1.Publish(message2);

    pubs->Stop();

    ASSERT_EQ(receivedMessages.size(), 1);
    EXPECT_EQ(receivedMessages[0], message1);
}
