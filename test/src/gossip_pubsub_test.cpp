#include "ipfs_pubsub/gossip_pubsub.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;
const std::string logger_config(R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: gossip_pubsub_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )");

class GossipPubSubTest : public ::testing::Test
{
public:
    virtual void SetUp() override
    {
        // prepare log system
        auto logging_system = std::make_shared<soralog::LoggingSystem>(
            std::make_shared<soralog::ConfiguratorFromYAML>(
                // Original LibP2P logging config
                std::make_shared<libp2p::log::Configurator>(),
                // Additional logging config for application
                logger_config));
        logging_system->configure();

        libp2p::log::setLoggingSystem(logging_system);
        libp2p::log::setLevelOfGroup("gossip_pubsub_test", soralog::Level::DEBUG);
    }

    virtual void TearDown() override
    {
    }
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
    std::this_thread::sleep_for(std::chrono::seconds(2));

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
    std::string publicKey = "z5b3BTS9wEgJxi9E8NHH6DT8Pj9xTmxBRgTaRUpBVox9a";
    std::string privateKey = "zGRXH26ag4k9jxTGXp2cg8n31CEkR2HN1SbHaKjaHnFTu";

    libp2p::crypto::KeyPair keyPair;
    auto codec = libp2p::multi::MultibaseCodecImpl();
    keyPair.publicKey = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode(publicKey).value() };
    keyPair.privateKey = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode(privateKey).value() };

    std::vector<std::string> receivedMessages;

    GossipPubSub pubs(keyPair);
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