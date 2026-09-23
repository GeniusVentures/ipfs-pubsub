#include "ipfs_pubsub/gossip_pubsub_topic.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

using GossipPubSub      = sgns::ipfs_pubsub::GossipPubSub;
using GossipPubSubTopic = sgns::ipfs_pubsub::GossipPubSubTopic;

namespace
{
    // Single-node tests expect the publisher to deliver to its own
    // subscribers. GossipCore::publish forwards locally only when
    // echo_forward_mode is on; the production default is off.
    libp2p::protocol::gossip::Config MakeEchoConfig()
    {
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode = true;
        return config;
    }
} // namespace

const std::string logger_config( R"(
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
  )" );

class GossipPubSubTopicTest : public ::testing::Test
{
public:
    virtual void SetUp() override
    {
        // prepare log system
        auto logging_system = std::make_shared<soralog::LoggingSystem>( std::make_shared<soralog::ConfiguratorFromYAML>(
            // Original LibP2P logging config
            std::make_shared<libp2p::log::Configurator>(),
            // Additional logging config for application
            logger_config ) );
        logging_system->configure();

        libp2p::log::setLoggingSystem( logging_system );
        libp2p::log::setLevelOfGroup( "gossip_pubsub_test", soralog::Level::DEBUG );
    }

    virtual void TearDown() override {}
};

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A message is published to a topic that the service is subscribed to.
 * @then The messages is received by the service.
 */
TEST_F( GossipPubSubTopicTest, TopicSubscription )
{
    std::vector<std::string> receivedMessages;
    auto                     pubs = std::make_shared<GossipPubSub>( MakeEchoConfig() );
    ASSERT_FALSE( pubs->Start( 40001, {} ).get() );

    GossipPubSubTopic pubsTopic1( pubs, "topic1" );
    pubsTopic1.Subscribe(
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                // Distinct name: a local `message` shadows the optional
                // parameter at its own initializer and fails to compile.
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                              message->data.size() );
                receivedMessages.push_back( std::move( received_message ) );
            }
        } );

    std::string message( "topic1_message" );
    pubsTopic1.Publish( message );

    pubs->Stop();

    ASSERT_EQ( receivedMessages.size(), 1 );
    EXPECT_EQ( receivedMessages[0], message );
}

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A topic is unsubsribed.
 * @then No messages rceived.
 */
TEST_F( GossipPubSubTopicTest, TopicUnsubscription )
{
    std::vector<std::string> receivedMessages;
    auto                     pubs = std::make_shared<GossipPubSub>( MakeEchoConfig() );
    ASSERT_FALSE( pubs->Start( 40001, {} ).get() );

    GossipPubSubTopic pubsTopic1( pubs, "topic1" );
    pubsTopic1.Subscribe(
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                // Distinct name: a local `message` shadows the optional
                // parameter at its own initializer and fails to compile.
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                              message->data.size() );
                receivedMessages.push_back( std::move( received_message ) );
            }
        } );

    std::string message1( "topic1_message1" );
    pubsTopic1.Publish( message1 );

    std::this_thread::sleep_for( std::chrono::microseconds( 100 ) );

    pubsTopic1.Unsubscribe();

    std::string message2( "topic1_message2" );
    pubsTopic1.Publish( message2 );

    pubs->Stop();

    ASSERT_EQ( receivedMessages.size(), 1 );
    EXPECT_EQ( receivedMessages[0], message1 );
}
