#include "ipfs_pubsub/gossip_pubsub.hpp"

#include <gtest/gtest.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>

#include <libp2p/multi/multibase_codec/multibase_codec_impl.hpp>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;

namespace
{
    // Several tests are single-node (or use unconnected nodes) and expect the
    // publisher to deliver to its own subscribers. GossipCore::publish
    // forwards locally only when echo_forward_mode is on; the production
    // default is off, so these tests opt in explicitly.
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

class GossipPubSubTest : public ::testing::Test
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
TEST_F( GossipPubSubTest, SendMessageToSingleSubscribedTopic )
{
    std::vector<std::string> receivedMessages;
    GossipPubSub             pubs( MakeEchoConfig() );
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );
    auto pubsTopic1 = pubs.Subscribe(
        "topic1",
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
    // Wait for the subscription: publishing before it completes races the
    // subscribe handler on the strand.
    pubsTopic1.get();

    std::string message( "topic1_message" );
    ASSERT_TRUE( pubs.Publish( "topic1", std::vector<uint8_t>( message.begin(), message.end() ) ) );

    pubs.Stop();

    ASSERT_EQ( receivedMessages.size(), 1 );
    EXPECT_EQ( receivedMessages[0], message );
}

/**
 * @given A pubsub service that has completed shutdown
 * @when A caller attempts to publish a notification
 * @then The attempt returns the concrete non-running error without waiting for
 *       a handler on the stopped io_context.
 */
TEST_F( GossipPubSubTest, PublishAfterStopFailsWithoutBlocking )
{
    GossipPubSub pubs;
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );
    pubs.Stop();

    auto publish = std::async(
        std::launch::async,
        [&pubs]() { return pubs.Publish( "topic", std::vector<uint8_t>{ 'm' } ); } );

    ASSERT_EQ( publish.wait_for( std::chrono::milliseconds( 100 ) ), std::future_status::ready );
    auto result = publish.get();
    ASSERT_FALSE( result );
    EXPECT_EQ( result.error(), make_error_code( sgns::ipfs_pubsub::GossipPubSubError::SERVICE_NOT_RUNNING ) );
}

/**
 * @given A subscriber whose callback is slow (hundreds of milliseconds)
 * @when Further messages are published while the callback is still running
 * @then Publish() returns promptly: consumer work no longer shares the gossip
 *       strand with publishes. (Before the delivery lane, the slow callback
 *       held the strand and Publish queued behind it.)
 */
TEST_F( GossipPubSubTest, SlowConsumerDoesNotBlockPublishes )
{
    std::mutex              received_mutex;
    std::size_t             received = 0;
    std::condition_variable all_received;
    GossipPubSub            pubs( MakeEchoConfig() );
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );

    auto subscription = pubs.Subscribe(
        "slow_consumer_topic",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( !message )
            {
                return;
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
            std::lock_guard<std::mutex> lock( received_mutex );
            if ( ++received == 2 )
            {
                all_received.notify_one();
            }
        } );
    subscription.get();

    const std::vector<uint8_t> payload{ 'm', 's', 'g' };
    ASSERT_TRUE( pubs.Publish( "slow_consumer_topic", payload ) );
    // The first delivery is now sleeping on the lane. This publish must not
    // wait behind it.
    const auto publish_started = std::chrono::steady_clock::now();
    ASSERT_TRUE( pubs.Publish( "slow_consumer_topic", payload ) );
    const auto publish_elapsed = std::chrono::steady_clock::now() - publish_started;
    EXPECT_LT( publish_elapsed, std::chrono::milliseconds( 200 ) );

    std::unique_lock<std::mutex> lock( received_mutex );
    ASSERT_TRUE( all_received.wait_for( lock, std::chrono::seconds( 5 ), [&] { return received == 2; } ) );

    pubs.Stop();
}

/**
 * @given A subscriber and a burst of messages
 * @when Stop() is called immediately after publishing, with deliveries still
 *       queued on the lane
 * @then Every message gossip delivered before shutdown reaches the consumer
 *       in order (the drain marker flushes the lane before it exits).
 */
TEST_F( GossipPubSubTest, ShutdownDrainsInFlightDeliveries )
{
    std::vector<std::size_t> received;
    std::mutex               received_mutex;
    GossipPubSub             pubs( MakeEchoConfig() );
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );

    constexpr std::size_t kBurstSize = 50;
    auto subscription = pubs.Subscribe(
        "drain_topic",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( !message )
            {
                return;
            }
            std::lock_guard<std::mutex> lock( received_mutex );
            received.push_back( message->data.size() );
        } );
    subscription.get();

    for ( std::size_t i = 0; i < kBurstSize; ++i )
    {
        // Distinct payload sizes make ordering observable.
        ASSERT_TRUE( pubs.Publish( "drain_topic", std::vector<uint8_t>( i + 1, 'x' ) ) );
    }
    pubs.Stop();

    std::lock_guard<std::mutex> lock( received_mutex );
    ASSERT_EQ( received.size(), kBurstSize );
    for ( std::size_t i = 0; i < kBurstSize; ++i )
    {
        EXPECT_EQ( received[i], i + 1 ) << "delivery order violated at " << i;
    }
}

/**
 * @given A subscriber whose callback calls Stop() from the delivery lane
 * @when A message is delivered and Stop() runs on the lane's own thread
 * @then shutdown completes without std::terminate: the joinable lane thread
 *       is detached in the self-thread branch instead of surviving into
 *       ~thread(). (Regression test for the detach fix.)
 */
TEST_F( GossipPubSubTest, StopFromConsumerCallbackDetachesLane )
{
    GossipPubSub        pubs( MakeEchoConfig() );
    std::promise<void>  stopped_from_callback;
    auto                stopped_future = stopped_from_callback.get_future();

    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );

    auto subscription = pubs.Subscribe(
        "stop_from_callback_topic",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( !message )
            {
                return;
            }
            // Runs on the delivery lane thread: Stop() must take the detach
            // branch, not join itself and not leave a joinable thread for
            // member destruction to terminate on.
            pubs.Stop();
            stopped_from_callback.set_value();
        } );
    subscription.get();

    ASSERT_TRUE( pubs.Publish( "stop_from_callback_topic", std::vector<uint8_t>{ 's' } ) );
    ASSERT_EQ( stopped_future.wait_for( std::chrono::seconds( 5 ) ), std::future_status::ready );
    // pubs destructs here while the (detached) lane thread finishes.
    SUCCEED();
}

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when A message is published to a topic that the service is not subscribed to.
 * @then No messages received.
 */
TEST_F( GossipPubSubTest, SendMessageToUnsubscribedTopic )
{
    std::vector<std::string> receivedMessages;
    GossipPubSub             pubs;
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );
    auto pubsTopic1 = pubs.Subscribe(
        "topic1",
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
    pubsTopic1.get();

    std::string message( "topic2_message" );
    ASSERT_TRUE( pubs.Publish( "topic2", std::vector<uint8_t>( message.begin(), message.end() ) ) );

    ASSERT_EQ( receivedMessages.size(), 0 );
}

/**
 * @given A pubsub service which is subscribed to 2 different topics 
 * @when A message is published to a specific topic.
 * @then The message processor linked to the topic is executed to process the received message
 */
TEST_F( GossipPubSubTest, MessagesMutiplexing )
{
    std::vector<std::string> receivedMessagesTopic1;
    std::vector<std::string> receivedMessagesTopic2;
    GossipPubSub             pubs( MakeEchoConfig() );
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );
    auto pubsTopic1 = pubs.Subscribe(
        "topic1",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                                      message->data.size() );
                receivedMessagesTopic1.push_back( std::move( received_message ) );
            }
        } );
    pubsTopic1.get();

    auto pubsTopic2 = pubs.Subscribe(
        "topic2",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                                      message->data.size() );
                receivedMessagesTopic2.push_back( std::move( received_message ) );
            }
        } );
    pubsTopic2.get();

    std::string messageTopic1( "topic1_message" );
    ASSERT_TRUE( pubs.Publish( "topic1", std::vector<uint8_t>( messageTopic1.begin(), messageTopic1.end() ) ) );

    std::string messageTopic2( "topic2_message" );
    ASSERT_TRUE( pubs.Publish( "topic2", std::vector<uint8_t>( messageTopic2.begin(), messageTopic2.end() ) ) );

    pubs.Stop();

    ASSERT_EQ( receivedMessagesTopic1.size(), 1 );
    EXPECT_EQ( receivedMessagesTopic1[0], messageTopic1 );

    ASSERT_EQ( receivedMessagesTopic2.size(), 1 );
    EXPECT_EQ( receivedMessagesTopic2[0], messageTopic2 );
}

/**
 * @given 2 pubsub services that are subscribed to 2 different topics
 * @when A message is published to a specific topic.
 * @then The message processor linked to the topic is executed to process the received message
 */
TEST_F( GossipPubSubTest, MutipleGossipSubObjectsOnDifferentChannels )
{
    std::vector<std::string> receivedMessagesTopic1;
    std::vector<std::string> receivedMessagesTopic2;
    GossipPubSub             pubs1( MakeEchoConfig() );
    ASSERT_FALSE( pubs1.Start( 40001, {} ).get() );
    GossipPubSub pubs2( MakeEchoConfig() );
    ASSERT_FALSE( pubs2.Start( 40002, {} ).get() );
    auto pubsTopic1 = pubs1.Subscribe(
        "topic1",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                                      message->data.size() );
                receivedMessagesTopic1.push_back( std::move( received_message ) );
            }
        } );
    pubsTopic1.get();

    auto pubsTopic2 = pubs2.Subscribe(
        "topic2",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                                      message->data.size() );
                receivedMessagesTopic2.push_back( std::move( received_message ) );
            }
        } );
    pubsTopic2.get();

    std::string messageTopic1( "topic1_message" );
    ASSERT_TRUE( pubs1.Publish( "topic1", std::vector<uint8_t>( messageTopic1.begin(), messageTopic1.end() ) ) );

    std::string messageTopic2( "topic2_message" );
    ASSERT_TRUE( pubs2.Publish( "topic2", std::vector<uint8_t>( messageTopic2.begin(), messageTopic2.end() ) ) );

    pubs1.Stop();
    pubs2.Stop();

    ASSERT_EQ( receivedMessagesTopic1.size(), 1 );
    EXPECT_EQ( receivedMessagesTopic1[0], messageTopic1 );

    ASSERT_EQ( receivedMessagesTopic2.size(), 1 );
    EXPECT_EQ( receivedMessagesTopic2[0], messageTopic2 );
}

/**
 * @given 2 pubsub services that are subscribed to a single topic
 * @when A message is published to a specific topic.
 * @then 2 message processor linked to the topic are xecuted to process the received message
 */
TEST_F( GossipPubSubTest, MutipleGossipSubObjectsOnSingleChannel )
{
    std::vector<std::string> receivedMessagesPubs1Topic1;
    std::vector<std::string> receivedMessagesPubs2Topic1;
    GossipPubSub             pubs1( MakeEchoConfig() );
    ASSERT_FALSE( pubs1.Start( 40001, {} ).get() );
    GossipPubSub pubs2( MakeEchoConfig() );
    ASSERT_FALSE( pubs2.Start( 40001, { pubs1.GetLocalAddress() } ).get() );

    auto pubs1Topic1 = pubs1.Subscribe(
        "topic1",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                                      message->data.size() );
                receivedMessagesPubs1Topic1.push_back( std::move( received_message ) );
            }
        } );

    auto pubs2Topic1 = pubs2.Subscribe(
        "topic1",
        [&]( boost::optional<const GossipPubSub::Message &> message )
        {
            if ( message )
            {
                std::string received_message( reinterpret_cast<const char *>( message->data.data() ),
                                                      message->data.size() );
                receivedMessagesPubs2Topic1.push_back( std::move( received_message ) );
            }
        } );

    // Wait for subscriptions
    pubs1Topic1.wait();
    pubs2Topic1.wait();

    // Wait until pubs2 sees pubs1 subscribed on the wire: publishing before
    // the gossip subscription exchange completes drops the message (no mesh
    // and no fanout candidates yet). A fixed sleep races the heartbeat.
    bool peer_seen = false;
    for ( int i = 0; i < 100 && !peer_seen; ++i )
    {
        peer_seen = pubs2.getPeerCount( "topic1" ) > 0;
        if ( !peer_seen )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
        }
    }
    ASSERT_TRUE( peer_seen ) << "pubs2 never learned of pubs1's subscription";

    std::string messageTopic1( "topic1_message" );
    ASSERT_TRUE( pubs2.Publish( "topic1", std::vector<uint8_t>( messageTopic1.begin(), messageTopic1.end() ) ) );

    // Wait for message transmitting
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    pubs2.Stop();
    std::this_thread::sleep_for( std::chrono::seconds( 2 ) );

    pubs1.Stop();

    ASSERT_EQ( receivedMessagesPubs1Topic1.size(), 1 );
    EXPECT_EQ( receivedMessagesPubs1Topic1[0], messageTopic1 );

    ASSERT_EQ( receivedMessagesPubs2Topic1.size(), 1 );
    EXPECT_EQ( receivedMessagesPubs2Topic1[0], messageTopic1 );
}

/**
 * @given A pubsub service which is subscribed to a single topic
 * @when Cancel a subscription before a maesage publishing
 * @then No messages should be received
 */
TEST_F( GossipPubSubTest, CancelSubscription )
{
    std::string publicKey  = "z5b3BTS9wEgJxi9E8NHH6DT8Pj9xTmxBRgTaRUpBVox9a";
    std::string privateKey = "zGRXH26ag4k9jxTGXp2cg8n31CEkR2HN1SbHaKjaHnFTu";

    libp2p::crypto::KeyPair keyPair;
    auto                    codec = libp2p::multi::MultibaseCodecImpl();
    keyPair.publicKey             = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode( publicKey ).value() };
    keyPair.privateKey            = { libp2p::crypto::PublicKey::Type::Ed25519, codec.decode( privateKey ).value() };

    std::vector<std::string> receivedMessages;

    GossipPubSub pubs( keyPair );
    ASSERT_FALSE( pubs.Start( 40001, {} ).get() );
    auto pubsTopic1 = pubs.Subscribe(
        "topic1",
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

    // Cancel sunscription before message publishing
    pubsTopic1.get()->cancel();

    std::string message( "topic1_message" );
    ASSERT_TRUE( pubs.Publish( "topic1", std::vector<uint8_t>( message.begin(), message.end() ) ) );

    pubs.Stop();

    ASSERT_EQ( receivedMessages.size(), 0 );
}

/**
 * @given A pubsub service
 * @when The service is started twice
 * @then The second start failed
 */
TEST_F( GossipPubSubTest, DISABLED_SecondStartFailed )
{
    std::vector<std::string> receivedMessages;

    GossipPubSub pubs;
    pubs.Start( 40001, {} );
    auto futureResult = pubs.Start( 40001, {} );

    auto result = futureResult.get();
    ASSERT_EQ( bool( result ), true );
    EXPECT_EQ( result.message(), "" );
}
