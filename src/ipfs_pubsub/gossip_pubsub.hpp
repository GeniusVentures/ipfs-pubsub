#pragma once

#include <ipfs_pubsub/logger.hpp>

#include <libp2p/basic/scheduler.hpp>
#include <string>
#include <chrono>
#include <atomic>
#include <future>
#include <unordered_map>
#include <set>

// boost/asio/io_context.hpp must be included before libp2p.
// Otherwise an error of the second including of winsock.h is thrown on Windows
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/signals2.hpp>

#include <libp2p/peer/peer_info.hpp>
#include <libp2p/protocol/gossip/gossip.hpp>
#include <libp2p/outcome/outcome.hpp>
#include <optional>
#include <mutex>
#include "ipfs_pubsub/deny_list_connection_gater.hpp"
#include "ipfs_lite/dht/kademlia_dht.hpp"
#include <libp2p/protocol/identify/identify.hpp>
#include <libp2p/protocol/autonat/autonat.hpp>
#include <libp2p/protocol/holepunch/holepunch_server.hpp>
#include <libp2p/protocol/holepunch/holepunch_client.hpp>
#include <libp2p/transport/upgrader.hpp>

namespace sgns::ipfs_pubsub
{
    // Private template defined in gossip_pubsub.cpp -- forward declared so
    // the GossipPubSub class can reference it in its private section.
    // (Definition lives in the .cpp; instantiated only there.)

    /**
     * Codes for errors that originate in gossip pubsub
     */
    enum class GossipPubSubError
    {
        INVALID_LOCAL_ADDRESS = 1,
        FAILED_LOCAL_ADDRESS_LISTENING,
        FAILED_SERVICE_START,
        SERVICE_NOT_RUNNING,
        PUBLISH_FAILED,
    };

    /**
     * Gossip pubsub service.
     * Allows to subscribe to a named channel (topic) using linp2p Gossip pub-sub protocol
     * and publish short messages to the named channels
     */
    class GossipPubSub final
    {
    public:
        typedef libp2p::protocol::gossip::Gossip::Message              Message;
        typedef libp2p::protocol::gossip::Gossip::SubscriptionCallback MessageCallback;

        class Subscription
        {
        public:
            Subscription() = default;
            Subscription( const Subscription & ) = delete;
            Subscription &operator=( const Subscription & ) = delete;
            Subscription( Subscription && ) = default;
            Subscription &operator=( Subscription && ) = default;
            ~Subscription() { cancel(); }
            void cancel();

            /// Move the inner libp2p subscription out without cancelling.
            /// Used during shutdown to defer cleanup until after the
            /// Source objects are gone.
            std::shared_ptr<libp2p::protocol::Subscription> move_inner()
            {
                return std::move( inner_ );
            }

            Subscription( std::shared_ptr<libp2p::protocol::Subscription> inner,
                          std::weak_ptr<boost::asio::io_context::strand>  strand )
                : inner_( std::move( inner ) ), strand_( std::move( strand ) )
            {
            }

        private:
            std::shared_ptr<libp2p::protocol::Subscription> inner_;
            std::weak_ptr<boost::asio::io_context::strand>  strand_;
        };

        /** Creates a gossip subscription service.
         */
        GossipPubSub();

        GossipPubSub( libp2p::protocol::gossip::Config config );

        /** Creates a gossip subscription service.
         * keyPair public / private key pair.
         * The public key is used as a gossip channel indentifier in a peer multiaddress
         */
        GossipPubSub( libp2p::crypto::KeyPair keyPair );

        GossipPubSub( libp2p::crypto::KeyPair keyPair, libp2p::protocol::gossip::Config config );

        /** Creates a gossip subscription service within a private network.
         * @param keyPair - public / private key pair.
         * The public key is used as a gossip channel indentifier in a peer multiaddress
         * @param config - gossip protocol configuration
         * @param networkKey - private network (pnet) key. Swarm-key text, base16 or
         * base64 encoded 32-byte PSK. Nodes created with different keys cannot
         * communicate; must match across all nodes of the private network.
         */
        GossipPubSub( libp2p::crypto::KeyPair                    keyPair,
                      libp2p::protocol::gossip::Config           config,
                      const std::string                          &networkKey );

        /** Adds a peer to the connection gater deny list.
 * Blocked peers are rejected at every stage of the connection upgrade
 * pipeline (dial, secured, upgraded). Existing connections are not
 * terminated; the block applies to new connection attempts.
 * @param peerId - the peer to block
 */
        void BlockPeer( const libp2p::peer::PeerId &peerId );

        /** Adds several peers to the connection gater deny list.
 * @param peerIds - the peers to block
 */
        void BlockPeers( const std::vector<libp2p::peer::PeerId> &peerIds );

        /** Removes a peer from the connection gater deny list.
 * @param peerId - the peer to unblock
 */
        void UnblockPeer( const libp2p::peer::PeerId &peerId );

        /** Checks whether a peer is in the connection gater deny list.
 * @param peerId - the peer to check
 * @return true if the peer is blocked
 */
        bool IsPeerBlocked( const libp2p::peer::PeerId &peerId ) const;

        /** Returns all peers currently in the connection gater deny list.
 * @return a snapshot of the deny list
 */
        std::vector<libp2p::peer::PeerId> GetBlockedPeers() const;

        ~GossipPubSub();

        /** Subscribes the service to a specified topic.
         * @param topic - a topic to subscribe to
         * @param onMessageCallback - callback which is invoked when the topic messages are received
         * @return subscription handler.
         */
        std::shared_future<std::shared_ptr<GossipPubSub::Subscription>> Subscribe( const std::string &topic,
                                                                                   MessageCallback onMessageCallback );

        /** Publish a string message to specific topic.
         * @param topic - a topic to publish a message to.
         * @param message - published message
         */
        /**
         * Publishes once and reports the completed transport attempt. A stopped
         * service is rejected synchronously; callers must not mistake queueing
         * work for a successful network notification.
         */
        libp2p::outcome::result<void> Publish( const std::string &topic, const std::vector<uint8_t> &message );

        /** Publish a message with automatic batching (5ms aggregation window).
         * Messages are collected and sent together to reduce overhead when publishing
         * to many topics rapidly. Use this instead of Publish() for high-throughput scenarios.
         * @param topic - a topic to publish a message to.
         * @param message - published message
         */
        void PublishBuffered( const std::string &topic, const std::vector<uint8_t> &message );

        /** Publish multiple messages at once (batch operation).
         * More efficient than calling Publish() multiple times as it uses a single strand post.
         * @param messages - vector of (topic, message) pairs to publish
         */
        void PublishBatch( const std::vector<std::pair<std::string, std::vector<uint8_t>>> &messages );

        /** Starts the gossip pubsub service.
         * @param listeningPort - is a port for incoming connections.
         * @param booststrapPeers - a list of remote peers that are connected to the current mesh
         * @return future object that indicates if the service is started.
         */
        std::future<std::error_code> Start( int                             listeningPort,
                                            const std::vector<std::string> &booststrapPeers,
                                            const std::string              &bindAddresses = "",
                                            const std::vector<std::string> &addAddresses  = {} );

        /** Stops the gossip pubsub service.
         */
        void Stop();

        /** Checks if the service is started.
         * @return flag tha indicates if the service is started
         */
        bool IsStarted() const;

        /** Wait until the service is stopped.
         * The Stop() method should be called from another thread.
         */
        void Wait();

        /** Returns the current service local address.
         * @return a string representation of the current peer local multi-address
         */
        const std::string &GetLocalAddress();

        /** Returns the actual interface address with peer ID for bootstrapping.
         * @return a string representation of the interface address + /ipfs/<peerID>
         */
        std::string GetInterfaceAddress();

        /** Returns an asio context that is used for gossip protocol handling.
         * @return the asio context
         */
        std::shared_ptr<boost::asio::io_context> GetAsioContext() const;

        /** Transforms a peer id to formatted string.
         * @param encoded peerId
         * @return string representation of the passed peerId
         */
        static std::string FormatPeerId( const std::vector<uint8_t> &peerId );

        /**
         * Find peers with the CID we are looking for using Kademlia DHT.
         * @param cid - IPFS Main CID to get from bitswap
          */
        libp2p::outcome::result<void> StartFindingPeers( const libp2p::multi::ContentIdentifier &cid );
        /**
         * Find peers with the CID we are looking for using Kademlia DHT.
         * @param key - IPFS Main CID to get from bitswap
          */
        libp2p::outcome::result<void> StartFindingPeers( const libp2p::protocol::kademlia::ContentId &key );

        /**
         * Service to provide CID as addresses update
         * @param key - IPFS Main CID to provide
          */
        void ProvideCID( const libp2p::protocol::kademlia::ContentId &key );

        /**
         * Start sending out CID provides once our addresses are updated.
          */
        void StartProvidingCID();

        /**
         * Schedule another find peers.
         * @param cid - IPFS Main CID to get from bitswap
         * @param interval - Time until next find occurs
          */
        void ScheduleNextFind( const libp2p::multi::ContentIdentifier &cid, std::chrono::seconds interval );
        void ScheduleNextFind( const libp2p::protocol::kademlia::ContentId &cid, std::chrono::seconds interval );
        /**
         * @brief       Add peers to be bootstrapped.
         * @param[in]   booststrapPeers: Vector of peers
          */
        void AddPeers( const std::vector<std::string> &booststrapPeers );

        const std::shared_ptr<libp2p::Host>                        GetHost() const;
        const std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> GetDHT() const;

        /**
         * Get Peers in a topic
         * @param topic - String of topic name
          */
        size_t getPeerCount( const std::string &topic ) const;

        /**
         * Get PeerIds in a topic
         * @param topic - String of topic name
          */
        std::vector<libp2p::peer::PeerId> getAllPeers( const std::string &topic ) const;

    private:
        void Init( std::optional<libp2p::crypto::KeyPair> keyPair,
                   std::optional<std::string>            networkKey = std::nullopt );

        // Extracts common objects (context, host, gossip, DHT, protocols)
        // from the assembled injector. Template, defined in gossip_pubsub.cpp.
        template <typename Injector>
        void InitHostFromInjector( Injector &&injector );
        
        void StopImpl();

        static libp2p::protocol::gossip::Config GetDefaultConfig()
        {
            libp2p::protocol::gossip::Config config;
            config.echo_forward_mode       = false; // Production default
            config.sign_messages           = true;
            config.seen_cache_limit        = 10;
            config.heartbeat_interval_msec = std::chrono::milliseconds{ 100 };
            return config;
        }

        libp2p::protocol::gossip::Config                               config_;
        std::once_flag                                                 m_stop_once;
        std::atomic<bool>                                              m_started{ false };
        // Serializes acceptance of a publish operation with shutdown.  Once Stop()
        // has made the service unavailable, Publish() must not enqueue work that
        // the stopped io_context can no longer execute.
        std::mutex                                                     m_lifecycle_mutex;
        std::shared_ptr<boost::asio::io_context>                       m_context;
        std::shared_ptr<boost::asio::io_context::strand>               m_strand;
        std::thread                                                    m_thread;
        std::shared_ptr<libp2p::Host>                                  m_host;
        std::shared_ptr<libp2p::protocol::gossip::Gossip>              m_gossip;
        std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT>           dht_;
        std::shared_ptr<DenyListConnectionGater>                       m_connection_gater;
        std::shared_ptr<libp2p::protocol::Identify>                    m_identify;
        std::shared_ptr<boost::asio::steady_timer>                     m_timer;
        std::string                                                    m_localAddress;
        std::vector<libp2p::multi::Multiaddress>                       m_localAddressAdditional;
        std::vector<libp2p::protocol::kademlia::ContentId>             m_provideCids;
        std::vector<std::shared_future<std::shared_ptr<Subscription>>> m_subscriptions;
        std::mutex                                                      m_subscriptions_mutex;

        // Address monitoring and peer management
        std::shared_ptr<boost::asio::steady_timer> m_address_monitor_timer;

        // Message batching for high-throughput scenarios
        struct PendingMessage
        {
            std::string          topic;
            std::vector<uint8_t> message;
        };

        std::vector<PendingMessage>                m_pending_messages;
        std::shared_ptr<boost::asio::steady_timer> m_batch_timer;
        std::chrono::milliseconds                  m_batch_window{ 5 }; // 5ms aggregation window
        bool                                       m_batch_timer_active{ false };

        // Message batching methods
        void flushPendingMessages();
        void scheduleBatchFlush();

        // Address management methods
        void                     startAddressMonitoring();
        void                     refreshLocalAddresses();
        void                     scheduleNextAddressRefresh();
        void                     onNetworkChange();
        std::vector<std::string> getCurrentNetworkInterfaces();
        void                     logCurrentNetworkState();
        //Default Bootstrap Servers
        std::vector<std::string> bootstrapAddresses_ = {
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmNnooDu7bfjPFoTZYxMNLWUQJyrVwtbZg5gBMjTezGAJN",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmQCU2EcMqAqQPR2i9bChDtGNJchTbq5TbXJJ16u19uLTa",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmbLHAnMoJPWSCR5Zhtx6BHJX9KiKNN6tpvbUcqanj75Nb",
            //"/dnsaddr/bootstrap.libp2p.io/ipfs/QmcZf59bWwK5XFi76CZX8cbJ4BhTzzA3gU1ZjYZcYW3dwt",
            //"/ip4/64.225.105.42/tcp/4001/p2p/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29",
            //"/ip4/3.92.45.153/tcp/4001/ipfs/12D3KooWP6R6XVCBK7t76o8VDwZdxpzAqVeDtHYQNmntP2y8NHvK",
            // "/ip4/104.131.131.82/tcp/4001/ipfs/QmaCpDMGvV2BGHeYERUEnRQAwe3N8SzbUtfsmvsqQLuvuJ",  // mars.i.ipfs.io
            // "/ip4/104.236.179.241/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM", // pluto.i.ipfs.io
            // "/ip4/128.199.219.111/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu", // saturn.i.ipfs.io
            // "/ip4/104.236.76.40/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64",   // venus.i.ipfs.io
            // "/ip4/178.62.158.247/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd",  // earth.i.ipfs.io
            // "/ip6/2604:a880:1:20::203:d001/tcp/4001/ipfs/QmSoLPppuBtQSGwKDZT2M73ULpjvfd3aZ6ha4oFGL1KrGM", // pluto.i.ipfs.io
            // "/ip6/2400:6180:0:d0::151:6001/tcp/4001/ipfs/QmSoLSafTMBsPKadTEgaXctDQVcqN88CNLHXMkTNwMKPnu", // saturn.i.ipfs.io
            // "/ip6/2604:a880:800:10::4a:5001/tcp/4001/ipfs/QmSoLV4Bbm51jM9C4gDYZQ9Cy3U6aXMJDAbzgu2fzaDs64", // venus.i.ipfs.io
            // "/ip6/2a03:b0c0:0:1010::23:1001/tcp/4001/ipfs/QmSoLer265NRgSp2LA3dPaeykiS1J6DifTC88f5uVQKNAd", // earth.i.ipfs.io
            //"/dnsaddr/fra1-1.hostnodes.pinata.cloud/ipfs/QmWaik1eJcGHq1ybTWe7sezRfqKNcDRNkeBaLnGwQJz1Cj",
            //"/dnsaddr/fra1-2.hostnodes.pinata.cloud/ipfs/QmNfpLrQQZr5Ns9FAJKpyzgnDL2GgC6xBug1yUZozKFgu4",
            //"/dnsaddr/fra1-3.hostnodes.pinata.cloud/ipfs/QmPo1ygpngghu5it8u4Mr3ym6SEU2Wp2wA66Z91Y1S1g29",
            //"/dnsaddr/nyc1-1.hostnodes.pinata.cloud/ipfs/QmRjLSisUCHVpFa5ELVvX3qVPfdxajxWJEHs9kN3EcxAW6",
            //"/dnsaddr/nyc1-2.hostnodes.pinata.cloud/ipfs/QmPySsdmbczdZYBpbi2oq2WMJ8ErbfxtkG8Mo192UHkfGP",
            //"/dnsaddr/nyc1-3.hostnodes.pinata.cloud/ipfs/QmSarArpxemsPESa6FNkmuu9iSE1QWqPX2R3Aw6f5jq4D5",
        };
        Logger m_logger = createLogger( "GossipPubSub" );
    };

    inline bool GossipPubSub::IsStarted() const
    {
        return m_started.load();
    }

    inline const std::string &GossipPubSub::GetLocalAddress()
    {
        return m_localAddress;
    }

    inline const std::shared_ptr<libp2p::Host> GossipPubSub::GetHost() const
    {
        return m_host;
    }

    inline const std::shared_ptr<sgns::ipfs_lite::ipfs::dht::IpfsDHT> GossipPubSub::GetDHT() const
    {
        return dht_;
    }

}

OUTCOME_HPP_DECLARE_ERROR_2( sgns::ipfs_pubsub, GossipPubSubError );
