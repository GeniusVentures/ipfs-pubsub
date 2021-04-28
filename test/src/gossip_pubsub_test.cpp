#include "ipfs_pubsub/gossip_pubsub.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/basic_file_sink.h>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;

class GossipPubSubTest : public ::testing::Test
{
public:
    virtual void SetUp() override
    {
        m_logger = spdlog::get("GossipPubSub");
        if (!m_logger)
        {
            m_logger = spdlog::basic_logger_mt("GossipPubSub", "GossipPubSub.log", true);
            m_logger->set_pattern("[%Y-%m-%d %H:%M:%S][%l] %v");
            m_logger->set_level(spdlog::level::debug);
        }
    }

    virtual void TearDown() override
    {
        m_logger->flush();
    }

    libp2p::common::Logger m_logger;
};

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A message is published to a topic that the service is subscribed to.
 * @then The messages is received by the service.
 */
TEST_F(GossipPubSubTest, SendMessageToSingleSubscribedTopic)
{
    std::vector<std::string> receivedMessages;
    GossipPubSub pubs;
    pubs.Start(40001, {});
    auto pubsTopic1 = pubs.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
    {
        if (message)
        {
            std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
            receivedMessages.push_back(std::move(message));
        }
    });

    std::string message("topic1_message");
    pubs.Publish("topic1", std::vector<uint8_t>(message.begin(), message.end()));

    pubs.Stop();

    ASSERT_EQ(receivedMessages.size(), 1);
    EXPECT_EQ(receivedMessages[0], message);
}

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A message is published to a topic that the service is not subscribed to.
 * @then No messages received.
 */
TEST_F(GossipPubSubTest, SendMessageToUnsubscribedTopic)
{
    std::vector<std::string> receivedMessages;
    GossipPubSub pubs;
    pubs.Start(40001, {});
    auto pubsTopic1 = pubs.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessages.push_back(std::move(message));
            }
        });

    std::string message("topic2_message");
    pubs.Publish("topic2", std::vector<uint8_t>(message.begin(), message.end()));

    ASSERT_EQ(receivedMessages.size(), 0);
}

/**
 * @given A pubsub service which is subscribed to 2 different topics 
 * @when A message is published to a specific topic.
 * @then The message processor linked to the topic is executed to process the received message
 */
TEST_F(GossipPubSubTest, MessagesMutiplexing)
{
    std::vector<std::string> receivedMessagesTopic1;
    std::vector<std::string> receivedMessagesTopic2;
    GossipPubSub pubs;
    pubs.Start(40001, {});
    auto pubsTopic1 = pubs.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessagesTopic1.push_back(std::move(message));
            }
        });

    auto pubsTopic2 = pubs.Subscribe("topic2", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessagesTopic2.push_back(std::move(message));
            }
        });

    std::string messageTopic1("topic1_message");
    pubs.Publish("topic1", std::vector<uint8_t>(messageTopic1.begin(), messageTopic1.end()));

    std::string messageTopic2("topic2_message");
    pubs.Publish("topic2", std::vector<uint8_t>(messageTopic2.begin(), messageTopic2.end()));

    pubs.Stop();

    ASSERT_EQ(receivedMessagesTopic1.size(), 1);
    EXPECT_EQ(receivedMessagesTopic1[0], messageTopic1);

    ASSERT_EQ(receivedMessagesTopic2.size(), 1);
    EXPECT_EQ(receivedMessagesTopic2[0], messageTopic2);
}

/**
 * @given 2 pubsub services that are subscribed to 2 different topics
 * @when A message is published to a specific topic.
 * @then The message processor linked to the topic is executed to process the received message
 */
TEST_F(GossipPubSubTest, MutipleGossipSubObjectsOnDifferentChannels)
{
    std::vector<std::string> receivedMessagesTopic1;
    std::vector<std::string> receivedMessagesTopic2;
    GossipPubSub pubs1;
    pubs1.Start(40001, {});
    GossipPubSub pubs2;
    pubs2.Start(40002, {});
    auto pubsTopic1 = pubs1.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessagesTopic1.push_back(std::move(message));
            }
        });

    auto pubsTopic2 = pubs2.Subscribe("topic2", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessagesTopic2.push_back(std::move(message));
            }
        });

    std::string messageTopic1("topic1_message");
    pubs1.Publish("topic1", std::vector<uint8_t>(messageTopic1.begin(), messageTopic1.end()));

    std::string messageTopic2("topic2_message");
    pubs2.Publish("topic2", std::vector<uint8_t>(messageTopic2.begin(), messageTopic2.end()));

    pubs1.Stop();
    pubs2.Stop();

    ASSERT_EQ(receivedMessagesTopic1.size(), 1);
    EXPECT_EQ(receivedMessagesTopic1[0], messageTopic1);

    ASSERT_EQ(receivedMessagesTopic2.size(), 1);
    EXPECT_EQ(receivedMessagesTopic2[0], messageTopic2);
}

/**
 * @given 2 pubsub services that are subscribed to a single topic
 * @when A message is published to a specific topic.
 * @then 2 message processor linked to the topic are xecuted to process the received message
 */
TEST_F(GossipPubSubTest, MutipleGossipSubObjectsOnSingleChannel)
{
    std::vector<std::string> receivedMessagesPubs1Topic1;
    std::vector<std::string> receivedMessagesPubs2Topic1;
    GossipPubSub pubs1;
    pubs1.Start(40001, {});
    GossipPubSub pubs2;
    pubs2.Start(40001, { pubs1.GetLocalAddress() } );

    auto pubs1Topic1 = pubs1.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessagesPubs1Topic1.push_back(std::move(message));
            }
        });

    auto pubs2Topic1 = pubs2.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessagesPubs2Topic1.push_back(std::move(message));
            }
        });

    // Wait for subscriptions
    pubs1Topic1.wait();
    pubs2Topic1.wait();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string messageTopic1("topic1_message");
    pubs2.Publish("topic1", std::vector<uint8_t>(messageTopic1.begin(), messageTopic1.end()));

    // Wait for message transmitting
    std::this_thread::sleep_for(std::chrono::seconds(2));

    pubs2.Stop();
    pubs1.Stop();

    ASSERT_EQ(receivedMessagesPubs1Topic1.size(), 1);
    EXPECT_EQ(receivedMessagesPubs1Topic1[0], messageTopic1);

    ASSERT_EQ(receivedMessagesPubs2Topic1.size(), 1);
    EXPECT_EQ(receivedMessagesPubs2Topic1[0], messageTopic1);
}

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when Cancel a subscription before a maesage publishing
 * @then No messages should be received
 */
TEST_F(GossipPubSubTest, CancelSubscription)
{
    std::vector<std::string> receivedMessages;

    GossipPubSub pubs;
    pubs.Start(40001, {});
    auto pubsTopic1 = pubs.Subscribe("topic1", [&](boost::optional<const GossipPubSub::Message&> message)
        {
            if (message)
            {
                std::string message(reinterpret_cast<const char*>(message->data.data()), message->data.size());
                receivedMessages.push_back(std::move(message));
            }
        });

    // Cancel sunscription before message publishing
    pubsTopic1.get().cancel();

    std::string message("topic1_message");
    pubs.Publish("topic1", std::vector<uint8_t>(message.begin(), message.end()));

    pubs.Stop();

    ASSERT_EQ(receivedMessages.size(), 0);
}

/**
 * @given A pubsub service
 * @when The service is started twice
 * @then The second start failed
 */
TEST_F(GossipPubSubTest, DISABLED_SecondStartFailed)
{
    std::vector<std::string> receivedMessages;

    GossipPubSub pubs;
    pubs.Start(40001, {});
    auto futureResult = pubs.Start(40001, {});

    auto result = futureResult.get();
    ASSERT_EQ(bool(result), true);
    EXPECT_EQ(result.message(), "");
}