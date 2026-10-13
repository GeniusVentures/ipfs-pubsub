// GossipSub resource-bounds tests (Phase 3 — TEST-02(b) from plan 03-01,
// TEST-02(a)/(c) from plan 03-02).
//
// Cases (03-01 Task 1 tracer):
//   - GossipPubSubBoundsTest.SustainedOverflowClosesStreamAndBlocksPeer
//     A remote slow-reader peer can no longer grow the local node's outbound
//     state: every oversized RPC is dropped at Stream::write() entry
//     (drop-new-only, D-05/D-07), K=3 consecutive cap-hit drops close the
//     stream through the existing error funnel (D-06) and fire the
//     setViolationHandler seam (D-08), close 1 is tolerated
//     (IsPeerBlocked == false, D-09 single-episode tolerance), and after
//     re-dial + re-offend, close 2 escalates through the existing BlockPeer()
//     chain into DenyListConnectionGater (D-10) — IsPeerBlocked == true.
//
// Cases (03-02, TEST-02(a)/(c) — completes the five-case suite):
//   - GossipPubSubBoundsTest.SubscribeCapBoundsRemoteSubscriptions
//     GOSS-01 receiver-side cumulative per-peer subscription cap: two hosted
//     nodes, cap = 3 in test config; the subscriber's T1..T3 are learned by
//     the observer (cap-th admitted), T4 stays unknown (cap+1-th ignored at
//     onPeerSubscribed — check-then-insert, no table mutation on rejection,
//     D-03), and triple duplicate re-subscription of T1 changes nothing
//     (dedup precedes the cap check — duplicates never consume headroom).
//   - GossipPubSubBoundsTest.SweepCollectsTopicAfterUnsubscribeAndResubscribeRelearns
//     D-04 sweep/liveness: learn -> empty-after-unsubscribe (dead entry is
//     sweep-eligible; empty() = not self-subscribed, no fanout, no
//     subscribers, no mesh) -> re-learn (the sweep never breaks
//     re-subscription).
//
// Cases (03-01 scope, pre-drafted during 03-01 and committed with 03-02):
//   - GossipPubSubBoundsTest.BelowCapAdmittedAboveCapDroppedAtEntry
//     Egress admission boundary: below-cap writes admitted (peer stays
//     known/unblocked), above-cap series closes via the bounded cycle.
//   - GossipPubSubBoundsTest.BlockedPeerRedialRejected
//     Gater enforcement after a block: the redial is rejected, the
//     subscription never learned, block persists. Runs LAST on purpose —
//     nothing may follow the shared raw fixture's final teardown.
//
// Determinism contract (D-11, documented deviation):
//   Determinism comes from configuration + bounded idempotent waits, NOT from
//   mock-scheduler stepping — a conscious, documented deviation from the
//   Phase 1-2 no-sleeps discipline (and from SC5's "(mock scheduler)"
//   parenthetical). No sleep-dependent orderings exist here: drops are
//   entry-point checks (D-07: an RPC larger than the cap alone is dropped
//   deterministically at entry), and every wait loop below carries an explicit
//   iteration cap. Publishes are heartbeat-paced (>= one heartbeat between
//   publishes, never burst-then-count — local publishes flush at heartbeat /
//   inbound-RPC end, not at Publish()). Ports are distinct from every other
//   test in this suite (Pitfall 1). rw_timeout_msec is raised so the only
//   stream-close path inside the test window is the K-drops close (Pitfall 4).
//
// Raw fixture (Pitfall 3/5):
//   The slow-reader peer is a raw libp2p host built with public injector APIs
//   only. It dials the node under test, opens a gossip stream, and sends a
//   hand-encoded subscribe RPC (UVarint(total_len) || protobuf
//   RPC{subscriptions:[SubOpts{subscribe:true, topicid:T}]} — the
//   message_builder.cpp convention via public libp2p::multi::UVarint). It
//   never reads: inbound gossip streams from the node are held open and
//   untouched, so outbound writes toward it stall/back up. The fixture never
//   touches libp2p's generated gossip protobuf header (not installed —
//   Pitfall 5). The node under test subscribes to the topic itself so mesh grafting forwards
//   full-size RPCs to the fixture (Pitfall 3).
//
// Assertion discipline: escalation is asserted via IsPeerBlocked and topic
// peer tables, never via the death of live connections — the gater rejects
// NEW connections only.

#include "ipfs_pubsub/gossip_pubsub.hpp"

#include <gtest/gtest.h>

#include <libp2p/log/configurator.hpp>
#include <libp2p/log/logger.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/injector/host_injector.hpp>
#include <libp2p/security/noise.hpp>
#include <libp2p/crypto/ed25519_provider/ed25519_provider_impl.hpp>
#include <libp2p/multi/uvarint.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

using GossipPubSub = sgns::ipfs_pubsub::GossipPubSub;

namespace
{
    /// Distinct fixed port — no other test in this suite uses 40301 (the
    /// pre-existing same-port failure is exactly the hazard avoided here).
    constexpr int kTestNodePort = 40301;

    /// Gossip protocol the node under test speaks (Config default).
    constexpr const char *kProtocolVersion = "/meshsub/1.0.0";

    /// Test topic shared by the node and the raw fixture.
    const std::string kBoundsTopic = "gossip_bounds_topic";

    /// Test heartbeat (D-11: 25-50 ms band, clear of timer floors).
    constexpr auto kHeartbeat = std::chrono::milliseconds( 25 );

    libp2p::protocol::gossip::Config MakeBoundsConfig()
    {
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode            = true; // echo base, as sibling tests
        config.heartbeat_interval_msec      = kHeartbeat;
        config.rw_timeout_msec              = std::chrono::milliseconds( 60000 ); // Pitfall 4
        config.max_pending_bytes_per_stream = 4 * 1024;                            // < any test payload
        config.overflow_close_threshold     = 3;                                   // K = 3
        // max_message_size stays at its 16 MiB default — larger than any payload here
        return config;
    }

    /// Subscription-cap variant (TEST-02(a)/(c), plan 03-02): the GOSS-01
    /// ingress boundary under test — a remote peer may hold at most 3
    /// admitted remote subscriptions. Egress knobs keep their 03-01 values
    /// (irrelevant to these cases; no oversized writes ever happen here).
    libp2p::protocol::gossip::Config MakeSubscriptionCapConfig()
    {
        auto config                          = MakeBoundsConfig();
        config.max_remote_subscriptions_per_peer = 3;
        return config;
    }

    /// Extracts a hosted node's own PeerId from its interface address
    /// ("<interface multiaddr>/ipfs/<peerID>" — gossip_pubsub.hpp contract).
    libp2p::peer::PeerId SelfPeerIdOf( GossipPubSub &pubs )
    {
        const std::string addr      = pubs.GetInterfaceAddress();
        const auto        ipfs_pos  = addr.find( "/ipfs/" );
        const auto        p2p_pos   = addr.find( "/p2p/" );
        assert( ipfs_pos != std::string::npos || p2p_pos != std::string::npos );
        const std::string marker    = ipfs_pos != std::string::npos ? "/ipfs/" : "/p2p/";
        const auto        pos       = ipfs_pos != std::string::npos ? ipfs_pos : p2p_pos;
        auto id_res = libp2p::peer::PeerId::fromBase58( addr.substr( pos + marker.size() ) );
        assert( id_res );
        return std::move( id_res.value() );
    }

    /// True iff `peers` contains `id`.
    bool ContainsPeer( const std::vector<libp2p::peer::PeerId> &peers,
                       const libp2p::peer::PeerId              &id )
    {
        return std::find( peers.begin(), peers.end(), id ) != peers.end();
    }

    /// Generates a fresh Ed25519 key pair so every host has a distinct
    /// identity (pubsub_counts pattern).
    libp2p::crypto::KeyPair GenerateKeyPair()
    {
        libp2p::crypto::ed25519::Ed25519ProviderImpl provider;
        auto keypair = provider.generate().value();
        libp2p::crypto::KeyPair result;
        result.publicKey  = { libp2p::crypto::Key::Type::Ed25519,
                             { keypair.public_key.begin(), keypair.public_key.end() } };
        result.privateKey = { libp2p::crypto::Key::Type::Ed25519,
                              { keypair.private_key.begin(), keypair.private_key.end() } };
        return result;
    }

    /// Hand-encodes RPC{ subscriptions: [ SubOpts{ subscribe: true,
    /// topicid: topic } ] } with the varint length prefix — public
    /// libp2p::multi::UVarint only, no generated protobuf headers (Pitfall 5).
    /// SubOpts: field 1 (subscribe, varint) = true; field 2 (topicid,
    /// length-delimited) = topic. RPC: field 1 (subscriptions,
    /// length-delimited) = the SubOpts message.
    std::vector<uint8_t> EncodeSubscribeRpc( const std::string &topic )
    {
        assert( topic.size() < 128 ); // single-byte length is all we need here

        std::vector<uint8_t> sub_opts;
        sub_opts.push_back( 0x08 ); // field 1, wire type 0: subscribe = true
        sub_opts.push_back( 0x01 );
        sub_opts.push_back( 0x12 ); // field 2, wire type 2: topicid
        sub_opts.push_back( static_cast<uint8_t>( topic.size() ) );
        sub_opts.insert( sub_opts.end(), topic.begin(), topic.end() );

        std::vector<uint8_t> rpc;
        rpc.push_back( 0x0A ); // field 1, wire type 2: subscriptions[0]
        rpc.push_back( static_cast<uint8_t>( sub_opts.size() ) );
        rpc.insert( rpc.end(), sub_opts.begin(), sub_opts.end() );

        std::vector<uint8_t> out;
        auto prefix = libp2p::multi::UVarint( rpc.size() ).toVector();
        out.insert( out.end(), prefix.begin(), prefix.end() );
        out.insert( out.end(), rpc.begin(), rpc.end() );
        return out;
    }

    /// Raw gossip-speaking slow-reader peer: a libp2p host built with public
    /// injector APIs only. It dials the node under test, opens a gossip
    /// stream, sends one hand-encoded subscribe RPC, and NEVER reads again —
    /// neither from its own outbound stream nor from the streams the node
    /// opens toward it (they are accepted and held open, untouched).
    class RawGossipPeer
    {
    public:
        RawGossipPeer() : m_keypair( GenerateKeyPair() )
        {
            auto injector = libp2p::injector::makeHostInjector(
                libp2p::injector::useKeyPair( m_keypair ),
                libp2p::injector::useSecurityAdaptors<libp2p::security::Noise>(),
                // The fixture dials the node under test over 127.0.0.1;
                // this fork's TCP transport rejects loopback dial
                // destinations unless explicitly allowed
                libp2p::injector::useAllowLoopbackDial() );
            m_host    = injector.create<std::shared_ptr<libp2p::Host>>();
            m_context = injector.create<std::shared_ptr<boost::asio::io_context>>();
            m_work    = std::make_unique<WorkGuard>( m_context->get_executor() );

            // Accept gossip streams the node under test opens toward us:
            // hold them open and never read (the slow-reader behavior).
            m_host->setProtocolHandler(
                { kProtocolVersion },
                [this]( libp2p::StreamAndProtocol stream ) {
                    std::lock_guard<std::mutex> lock( m_streams_mutex );
                    m_streams.push_back( std::move( stream.stream ) );
                } );

            // BasicHost::newStream derives the dial's source addresses from
            // the host's listen addresses -- a host that never listens dials
            // with empty sources and the connection setup rejects them.
            // Listen on an ephemeral loopback port and start.
            m_context->post( [this] {
                auto ma_res = libp2p::multi::Multiaddress::create( "/ip4/127.0.0.1/tcp/0" );
                if ( ma_res )
                {
                    std::ignore = m_host->listen( ma_res.value() );
                }
                m_host->start();
            } );

            m_runner = std::thread( [ctx = m_context] { ctx->run(); } );
        }

        ~RawGossipPeer()
        {
            {
                std::lock_guard<std::mutex> lock( m_streams_mutex );
                m_streams.clear();
            }
            // Tear the network stack down FIRST, while its io_context is
            // still alive and its thread still running — member order alone
            // would destroy the context before the host (its sockets) and
            // corrupt state for tests that run later in this process.
            m_host.reset();
            m_work.reset();
            m_context->stop();
            if ( m_runner.joinable() )
            {
                m_runner.join();
            }
        }

        RawGossipPeer( const RawGossipPeer & ) = delete;
        RawGossipPeer &operator=( const RawGossipPeer & ) = delete;

        const libp2p::peer::PeerId id() const
        {
            return m_host->getPeerInfo().id;
        }

        /// Closes every connection to the last-dialed peer
        /// (Host::disconnect). Needed before a re-dial that must exercise
        /// the NEW-connection path — the gater does not terminate live
        /// connections, and a dial over an existing connection is not
        /// intercepted by it.
        void DisconnectDialed()
        {
            std::lock_guard<std::mutex> lock( m_streams_mutex );
            m_streams.clear();
            if ( m_last_dialed )
            {
                m_host->disconnect( *m_last_dialed );
            }
        }

        /// Dials the node at `address` (multiaddress string with /p2p/<id>),
        /// opens a gossip stream, sends the subscribe RPC for `topic`, and
        /// holds the stream open without ever reading. Bounded: fails after
        /// kDialTimeoutMs instead of hanging.
        bool DialAndSubscribe( const std::string &address, const std::string &topic )
        {
            m_last_error = "dial pending";
            auto ma_res = libp2p::multi::Multiaddress::create( address );
            if ( !ma_res )
            {
                m_last_error = "multiaddress create failed: " + address;
                return false;
            }
            auto           ma = std::move( ma_res.value() );
            auto           peer_id_str = ma.getPeerId();
            if ( !peer_id_str )
            {
                m_last_error = "no peer id in address: " + address;
                return false;
            }
            auto peer_id_res = libp2p::peer::PeerId::fromBase58( *peer_id_str );
            if ( !peer_id_res )
            {
                m_last_error = "peer id base58 decode failed";
                return false;
            }
            libp2p::peer::PeerInfo peer_info{ std::move( peer_id_res.value() ), { ma } };

            // The dial multiaddress must be bare /ip4/../tcp/.. — the
            // transport's canDial() rejects addresses carrying a /p2p/ or
            // /ipfs/ peer-id suffix (the peer id rides in the PeerInfo).
            auto bare = address.substr( 0, address.find( "/p2p/" ) );
            bare     = bare.substr( 0, bare.find( "/ipfs/" ) );
            auto bare_ma_res = libp2p::multi::Multiaddress::create( bare );
            if ( !bare_ma_res )
            {
                m_last_error = "bare multiaddress create failed: " + bare;
                return false;
            }
            peer_info.addresses.assign( 1, std::move( bare_ma_res.value() ) );
            m_last_dialed = peer_info.id;

            std::mutex              m;
            std::condition_variable cv;
            bool                    done = false;
            bool                    ok   = false;

            m_host->newStream(
                peer_info,
                { kProtocolVersion },
                [this, &topic, &m, &cv, &done, &ok]( auto &&stream_res ) {
                    if ( !stream_res )
                    {
                        m_last_error = "newStream failed: " + stream_res.error().message();
                        std::lock_guard<std::mutex> lock( m );
                        ok   = false;
                        done = true;
                        cv.notify_one();
                        return;
                    }
                    auto        stream = std::move( stream_res.value().stream );
                    auto        rpc    = EncodeSubscribeRpc( topic );
                    // Hold the stream open; never read from it.
                    {
                        std::lock_guard<std::mutex> lock( m_streams_mutex );
                        m_streams.push_back( stream );
                    }
                    stream->write(
                        gsl::span<const uint8_t>( rpc.data(), rpc.size() ),
                        rpc.size(),
                        [&m, &cv, &done, &ok]( auto && /*res*/ ) {
                            std::lock_guard<std::mutex> lock( m );
                            ok   = true; // RPC dispatched to the wire; response never read
                            done = true;
                            cv.notify_one();
                        } );
                } );

            std::unique_lock<std::mutex> lock( m );
            cv.wait_for( lock, std::chrono::seconds( 10 ), [&] { return done; } );
            if ( !done )
            {
                m_last_error = "dial timed out";
            }
            else if ( ok )
            {
                m_last_error.clear();
            }
            return done && ok;
        }

        const std::string &LastError() const
        {
            return m_last_error;
        }

    private:
        using WorkGuard = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;

        libp2p::crypto::KeyPair                        m_keypair;
        std::shared_ptr<libp2p::Host>                  m_host;
        std::shared_ptr<boost::asio::io_context>       m_context;
        std::unique_ptr<WorkGuard>                     m_work;
        std::thread                                    m_runner;
        std::mutex                                     m_streams_mutex;
        std::vector<std::shared_ptr<libp2p::connection::Stream>> m_streams;
        std::string                                    m_last_error;
        std::optional<libp2p::peer::PeerId>            m_last_dialed;
    };

    /// Process-wide raw fixture. A SECOND freshly-constructed host+
    /// io_context in the same process reliably hangs its first dial (seen
    /// with --gtest_repeat=2 and in the full suite; root cause not chased —
    /// out of this phase's scope), while one instance serves any number of
    /// dials, disconnects, and re-dials across tests. Block state lives on
    /// each test's own node instance, so a stable fixture identity is
    /// exactly the deployment shape under test (one remote peer, many
    /// nodes).
    RawGossipPeer &SharedRawPeer()
    {
        static RawGossipPeer peer;
        return peer;
    }

} // namespace

const std::string logger_config( R"(
# ----------------
sinks:
  - name: console
    type: console
    color: true
groups:
  - name: gossip_pubsub_bounds_test
    sink: console
    level: info
    children:
      - name: libp2p
      - name: Gossip
# ----------------
  )" );

class GossipPubSubBoundsTest : public ::testing::Test
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
        libp2p::log::setLevelOfGroup( "gossip_pubsub_bounds_test", soralog::Level::DEBUG );
    }

    virtual void TearDown() override {}

protected:
    /// Dialable loopback address of the node under test: GetLocalAddress()
    /// is built on the 0.0.0.0 wildcard bind, rewritten to 127.0.0.1.
    std::string LoopbackAddress( GossipPubSub &pubs )
    {
        auto        addr = pubs.GetLocalAddress();
        const auto  pos  = addr.find( "0.0.0.0" );
        if ( pos != std::string::npos )
        {
            addr.replace( pos, strlen( "0.0.0.0" ), "127.0.0.1" );
        }
        return addr;
    }

    /// Bounded idempotent wait (D-11): terminates on the predicate or the
    /// iteration cap — never an unbounded wait.
    bool WaitFor( const std::function<bool()> &pred, int max_iters, std::chrono::milliseconds step )
    {
        for ( int i = 0; i < max_iters; ++i )
        {
            if ( pred() )
            {
                return true;
            }
            std::this_thread::sleep_for( step );
        }
        return pred();
    }

    /// Heartbeat-paced overflow cycle (never burst-then-count, Pitfall 2):
    /// publishes one oversized (~64 KiB > 4 KiB cap) message, waits >= one
    /// heartbeat flush, and repeats until the node's topic peer table drops
    /// the raw peer (drop K closed the stream and cleanup ran) or the
    /// publish cap is hit.
    bool OverflowUntilPeerForgotten( GossipPubSub &pubs )
    {
        const std::vector<uint8_t> payload( 64 * 1024, 0x2A );
        for ( int i = 0; i < 10; ++i )
        {
            auto res = pubs.Publish( kBoundsTopic, payload );
            if ( !res.has_value() )
            {
                return false;
            }
            // >= 2 heartbeats: the flush that trips the entry check happens
            // on the heartbeat after the publish, and K=3 drops need K
            // paced publishes; sleep here is idempotent pacing, not
            // ordering (D-11).
            std::this_thread::sleep_for( kHeartbeat * 3 );
            if ( pubs.getPeerCount( kBoundsTopic ) == 0 )
            {
                return true; // closed + cleanup observed
            }
        }
        return false;
    }
};

/**
 * @given A gossip node with a 4 KiB outbound pending cap (K=3) subscribed to
 *        a topic, and a raw slow-reader peer subscribed to the same topic
 *        that never reads
 * @when Oversized (~64 KiB) publishes drive K consecutive cap-hit drops, the
 *       stream closes (close 1), the peer re-dials and re-offends, and K more
 *       drops close the stream again (close 2)
 * @then Drops 1..K-1 never disconnect the peer; close 1 leaves the peer
 *       unblocked (D-09 single-episode tolerance); close 2 escalates through
 *       the existing BlockPeer() chain — IsPeerBlocked == true (GOSS-03)
 */
TEST_F( GossipPubSubBoundsTest, SustainedOverflowClosesStreamAndBlocksPeer )
{
    GossipPubSub pubs( MakeBoundsConfig() );
    ASSERT_FALSE( pubs.Start( kTestNodePort, {} ).get() );

    // Node subscribes itself so mesh grafting forwards full-size RPCs to the
    // raw peer (Pitfall 3 — non-mesh subscribers only get tiny IHAVEs).
    auto sub = pubs.Subscribe(
        kBoundsTopic, []( boost::optional<const GossipPubSub::Message &> ) {} );
    sub.get();

    RawGossipPeer &raw = SharedRawPeer();
    ASSERT_TRUE( raw.DialAndSubscribe( LoopbackAddress( pubs ), kBoundsTopic ) )
        << "raw fixture could not dial/subscribe: " << raw.LastError()
        << " (address was: '" << LoopbackAddress( pubs ) << "')";

    // Bounded wait: the raw peer's subscription is learned (mesh forms
    // within a couple of heartbeats), then a bounded settle for grafting.
    ASSERT_TRUE( WaitFor( [&] { return pubs.getPeerCount( kBoundsTopic ) > 0; }, 200, kHeartbeat ) )
        << "node never learned the raw peer's subscription";
    std::this_thread::sleep_for( kHeartbeat * 4 ); // mesh settle (bounded, idempotent)

    // Cycle 1: K drops -> close 1. Drops 1..K-1 keep the peer known (a drop
    // is not a disconnect) — asserted inside the cycle by termination on the
    // peer-table observable only.
    ASSERT_TRUE( OverflowUntilPeerForgotten( pubs ) ) << "sustained overflow did not close the stream";

    // Close 1 is tolerated (D-09): one episode must not block the peer.
    EXPECT_FALSE( pubs.IsPeerBlocked( raw.id() ) );

    // Re-dial + re-offend (gater has NOT blocked; the existing connection is
    // alive — banOrForget only closed gossip streams).
    ASSERT_TRUE( raw.DialAndSubscribe( LoopbackAddress( pubs ), kBoundsTopic ) )
        << "raw fixture could not re-dial after the first close: " << raw.LastError();
    ASSERT_TRUE( WaitFor( [&] { return pubs.getPeerCount( kBoundsTopic ) > 0; }, 200, kHeartbeat ) )
        << "reconnect was never observed after the first close";
    std::this_thread::sleep_for( kHeartbeat * 4 ); // mesh settle (bounded, idempotent)

    // Cycle 2: K more drops -> close 2 -> escalation.
    ASSERT_TRUE( OverflowUntilPeerForgotten( pubs ) ) << "second overflow cycle did not close the stream";

    // GOSS-03: the 2nd sustained-overflow close reached the gater via the
    // existing BlockPeer() chain; permanent until UnblockPeer() (D-10).
    EXPECT_TRUE( pubs.IsPeerBlocked( raw.id() ) );

    pubs.Stop();
}

/**
 * @given Two hosted GossipPubSub nodes connected over loopback (ephemeral
 *        ports, pubsub_counts mechanics), the observing node configured
 *        with max_remote_subscriptions_per_peer = 3
 * @when The subscriber node subscribes to four topics T1..T4 via the public
 *       Subscribe() API, then re-subscribes T1 three more times
 * @then T1, T2 and T3 are learned by the observer (the cap-th topic IS
 *       admitted — the boundary one step under), T4 stays unknown across a
 *       bounded settling wait (the cap+1-th topic ignored — the boundary one
 *       step over, GOSS-01), and the duplicate re-subscriptions change
 *       nothing (duplicates never consume headroom — the precision edge:
 *       dedup precedes the cap check, so a re-sent subscribe is an exact
 *       set-membership hit, never both counted and rejected).
 *
 * Placement note: hosted-node cases (this one and the sweep case below) are
 * registered BEFORE the raw-fixture cases — nothing may run after
 * BlockedPeerRedialRejected's shared-raw-peer teardown (lingering fixture
 * callbacks can crash a following test's SetUp — observed as SEH 0xc0000005
 * when this case originally followed it; order-only, no assertion change).
 */
TEST_F( GossipPubSubBoundsTest, SubscribeCapBoundsRemoteSubscriptions )
{
    GossipPubSub nodeA( MakeSubscriptionCapConfig() );
    GossipPubSub nodeB( MakeSubscriptionCapConfig() );
    ASSERT_FALSE( nodeA.Start( 0, {} ).get() ); // ephemeral ports (Pitfall 1)
    ASSERT_FALSE( nodeB.Start( 0, {} ).get() );
    nodeB.AddPeers( { nodeA.GetInterfaceAddress() } ); // pubsub_counts mechanics

    const libp2p::peer::PeerId bId = SelfPeerIdOf( nodeB );

    const std::string t1 = "gossip_sub_cap_t1";
    const std::string t2 = "gossip_sub_cap_t2";
    const std::string t3 = "gossip_sub_cap_t3";
    const std::string t4 = "gossip_sub_cap_t4"; // the cap+1-th topic

    // B subscribes T1..T4 via the public API; all subscriptions stay alive
    // for the whole test (a Subscription destructor cancels).
    std::vector<std::shared_future<std::shared_ptr<GossipPubSub::Subscription>>> subs;
    for ( const auto &topic : { t1, t2, t3, t4 } )
    {
        subs.push_back( nodeB.Subscribe(
            topic, []( boost::optional<const GossipPubSub::Message &> ) {} ) );
        subs.back().get();
    }

    // Bounded idempotent wait: A learned B on T1 (and then T2/T3 — all three
    // admitted; ordering on the wire follows the Subscribe() call order).
    ASSERT_TRUE( WaitFor( [&] { return ContainsPeer( nodeA.getAllPeers( t1 ), bId ); },
                          200, kHeartbeat ) )
        << "observer never learned the cap-th-below subscriptions";
    ASSERT_TRUE( ContainsPeer( nodeA.getAllPeers( t2 ), bId ) );
    ASSERT_TRUE( ContainsPeer( nodeA.getAllPeers( t3 ), bId ) );

    // Bounded settling wait: T4 is never learned (the cap+1-th subscription
    // is ignored at onPeerSubscribed — no table mutation ever happens for
    // it, so the entry stays empty and sweep-eligible, D-04).
    std::this_thread::sleep_for( kHeartbeat * 8 );
    EXPECT_TRUE( nodeA.getAllPeers( t4 ).empty() )
        << "cap+1-th subscription was admitted — GOSS-01 cap not enforced";

    // Precision edge: B re-subscribes T1 three more times; duplicates are
    // exact set-membership hits at the dedup early-return and never reach
    // the cap check — A's view must be unchanged (still exactly B on T1,
    // T2/T3 unaffected, T4 still ignored).
    for ( int i = 0; i < 3; ++i )
    {
        subs.push_back( nodeB.Subscribe(
            t1, []( boost::optional<const GossipPubSub::Message &> ) {} ) );
        subs.back().get();
        std::this_thread::sleep_for( kHeartbeat * 2 ); // bounded pacing
    }
    std::this_thread::sleep_for( kHeartbeat * 4 ); // bounded settle
    EXPECT_EQ( nodeA.getAllPeers( t1 ).size(), 1 )
        << "duplicate subscriptions changed the admitted set";
    EXPECT_TRUE( ContainsPeer( nodeA.getAllPeers( t1 ), bId ) );
    EXPECT_TRUE( ContainsPeer( nodeA.getAllPeers( t2 ), bId ) );
    EXPECT_TRUE( ContainsPeer( nodeA.getAllPeers( t3 ), bId ) );
    EXPECT_TRUE( nodeA.getAllPeers( t4 ).empty() );

    nodeA.Stop();
    nodeB.Stop();
}

/**
 * @given Two hosted GossipPubSub nodes connected over loopback with a tiny
 *        heartbeat interval (25 ms), B subscribed to a single topic T1 that
 *        A has learned
 * @when B unsubscribes from T1 through the public path (Subscription::cancel
 *       — no public Unsubscribe(topic) exists on GossipPubSub; the
 *       subscription object IS the public unsubscribe path), then
 *       re-subscribes to T1
 * @then A's peer table for T1 empties after the unsubscribe (the entry is
 *       dead: no self-subscription, no fanout, no subscribers, no mesh —
       sweep-eligible on the very next heartbeat, D-04) and the
 *       re-subscription is learned again (liveness after collection — the
 *       sweep prunes without ever breaking re-subscription).
 */
TEST_F( GossipPubSubBoundsTest, SweepCollectsTopicAfterUnsubscribeAndResubscribeRelearns )
{
    GossipPubSub nodeA( MakeSubscriptionCapConfig() ); // tiny heartbeat; cap irrelevant (1 topic)
    GossipPubSub nodeB( MakeSubscriptionCapConfig() );
    ASSERT_FALSE( nodeA.Start( 0, {} ).get() ); // ephemeral ports (Pitfall 1)
    ASSERT_FALSE( nodeB.Start( 0, {} ).get() );
    nodeB.AddPeers( { nodeA.GetInterfaceAddress() } );

    const libp2p::peer::PeerId bId = SelfPeerIdOf( nodeB );
    const std::string         t1  = "gossip_sweep_topic";

    // Learn: B subscribes T1 -> A's table holds B on T1.
    auto subT1 = nodeB.Subscribe(
        t1, []( boost::optional<const GossipPubSub::Message &> ) {} );
    subT1.get();
    ASSERT_TRUE( WaitFor( [&] { return ContainsPeer( nodeA.getAllPeers( t1 ), bId ); },
                          200, kHeartbeat ) )
        << "observer never learned the initial subscription";

    // Unsubscribe via the public path: A's entry for T1 loses its last
    // subscriber and empties (heartbeat-paced, bounded idempotent wait).
    subT1.get()->cancel();
    ASSERT_TRUE( WaitFor( [&] { return nodeA.getAllPeers( t1 ).empty(); },
                          200, kHeartbeat ) )
        << "dead topic entry was not collected after unsubscribe";

    // Liveness after collection: B re-subscribes T1 -> A re-learns (the
    // sweep never breaks re-subscription).
    auto subAgain = nodeB.Subscribe(
        t1, []( boost::optional<const GossipPubSub::Message &> ) {} );
    subAgain.get();
    ASSERT_TRUE( WaitFor( [&] { return ContainsPeer( nodeA.getAllPeers( t1 ), bId ); },
                          200, kHeartbeat ) )
        << "re-subscription after sweep was not re-learned";

    nodeA.Stop();
    nodeB.Stop();
}

/**
 * @given A gossip node with a 4 KiB outbound pending cap (K=3) subscribed to
 *        a topic, and a raw slow-reader peer subscribed to the same topic
 * @when A paced series of BELOW-cap (~1 KiB) publishes flows to the peer,
 *       then the payload switches to above-cap (~64 KiB)
 * @then The below-cap series is admitted (the peer stays known on the topic
 *       and unblocked — one step under the boundary never drops and never
 *       closes), and the above-cap series closes the stream via the bounded
 *       cycle observable (one step over the boundary drops at entry, K drops
 *       close). Never asserts an exact drop count after a burst (Pitfall 2).
 */
TEST_F( GossipPubSubBoundsTest, BelowCapAdmittedAboveCapDroppedAtEntry )
{
    GossipPubSub pubs( MakeBoundsConfig() );
    ASSERT_FALSE( pubs.Start( 40302, {} ).get() ); // distinct port (Pitfall 1)

    auto sub = pubs.Subscribe(
        kBoundsTopic, []( boost::optional<const GossipPubSub::Message &> ) {} );
    sub.get();

    RawGossipPeer &raw = SharedRawPeer();
    ASSERT_TRUE( raw.DialAndSubscribe( LoopbackAddress( pubs ), kBoundsTopic ) )
        << "raw fixture could not dial/subscribe: " << raw.LastError();

    ASSERT_TRUE( WaitFor( [&] { return pubs.getPeerCount( kBoundsTopic ) > 0; }, 200, kHeartbeat ) )
        << "node never learned the raw peer's subscription";
    std::this_thread::sleep_for( kHeartbeat * 4 ); // mesh settle (bounded, idempotent)

    // Phase 1 — below the cap: ~1 KiB payloads serialize well under the
    // 4 KiB cap; every write is admitted. Paced by >= one heartbeat per
    // publish (never burst-then-count).
    const std::vector<uint8_t> small_payload( 1024, 0x01 );
    for ( int i = 0; i < 10; ++i )
    {
        auto res = pubs.Publish( kBoundsTopic, small_payload );
        ASSERT_TRUE( res.has_value() );
        std::this_thread::sleep_for( kHeartbeat * 3 );
        // admitted writes never drop and never close: the peer stays known
        // (asserted inside the paced loop, one step under the boundary)
        ASSERT_GT( pubs.getPeerCount( kBoundsTopic ), 0 )
            << "below-cap publish dropped/closed the stream (iteration " << i << ")";
    }
    EXPECT_FALSE( pubs.IsPeerBlocked( raw.id() ) );

    // Phase 2 — above the cap: the same fixture, switched to ~64 KiB
    // payloads; the bounded overflow-cycle observable terminates on close.
    ASSERT_TRUE( OverflowUntilPeerForgotten( pubs ) )
        << "above-cap series did not close the stream";

    pubs.Stop();
}

/**
 * @given A gossip node that has blocked a raw peer after two sustained-
 *        overflow closes (the tracer's two-cycle drive)
 * @when The blocked peer disconnects and attempts ONE more dial + subscribe
 * @then The gater rejects the new connection: the subscription is never
 *       learned (getAllPeers stays empty across a bounded settling wait) and
 *       IsPeerBlocked remains true. Asserts gater enforcement without
 *       asserting the death of any live connection (the block applies to
 *       new connection attempts only).
 */
TEST_F( GossipPubSubBoundsTest, BlockedPeerRedialRejected )
{
    GossipPubSub pubs( MakeBoundsConfig() );
    ASSERT_FALSE( pubs.Start( 40303, {} ).get() ); // distinct port (Pitfall 1)

    auto sub = pubs.Subscribe(
        kBoundsTopic, []( boost::optional<const GossipPubSub::Message &> ) {} );
    sub.get();

    RawGossipPeer &raw = SharedRawPeer();
    ASSERT_TRUE( raw.DialAndSubscribe( LoopbackAddress( pubs ), kBoundsTopic ) )
        << "raw fixture could not dial/subscribe: " << raw.LastError();

    // Two-cycle drive to a block (tracer idiom): close 1 tolerated...
    ASSERT_TRUE( WaitFor( [&] { return pubs.getPeerCount( kBoundsTopic ) > 0; }, 200, kHeartbeat ) );
    std::this_thread::sleep_for( kHeartbeat * 4 );
    ASSERT_TRUE( OverflowUntilPeerForgotten( pubs ) ) << "cycle 1 did not close";
    ASSERT_FALSE( pubs.IsPeerBlocked( raw.id() ) );

    // ...re-dial + re-offend, close 2 blocks.
    ASSERT_TRUE( raw.DialAndSubscribe( LoopbackAddress( pubs ), kBoundsTopic ) )
        << "raw fixture could not re-dial after the first close: " << raw.LastError();
    ASSERT_TRUE( WaitFor( [&] { return pubs.getPeerCount( kBoundsTopic ) > 0; }, 200, kHeartbeat ) );
    std::this_thread::sleep_for( kHeartbeat * 4 );
    ASSERT_TRUE( OverflowUntilPeerForgotten( pubs ) ) << "cycle 2 did not close";
    ASSERT_TRUE( pubs.IsPeerBlocked( raw.id() ) );

    // Force the NEW-connection path (the gater never terminates the still-
    // live connection), then attempt exactly one more dial + subscribe.
    raw.DisconnectDialed();
    std::this_thread::sleep_for( kHeartbeat * 4 ); // bounded settle after teardown

    // The dial result itself is not the assert surface (rejection may
    // surface as an error here or later); the gater's effect is that the
    // subscription is never learned.
    std::ignore = raw.DialAndSubscribe( LoopbackAddress( pubs ), kBoundsTopic );

    // Bounded negative wait: the peer table stays empty (the gater rejected
    // the new connection — no subscription exchange ever happened) and the
    // block persists (D-10).
    std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    EXPECT_EQ( pubs.getPeerCount( kBoundsTopic ), 0 )
        << "blocked peer's subscription was learned — gater did not reject the redial";
    EXPECT_TRUE( pubs.IsPeerBlocked( raw.id() ) );

    pubs.Stop();
}
