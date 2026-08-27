#pragma once

#include <mutex>
#include <set>
#include <vector>

#include <libp2p/network/connection_gater.hpp>
#include <libp2p/network/connection_gater_error.hpp>
#include <libp2p/peer/peer_id.hpp>

namespace sgns::ipfs_pubsub
{
    /**
     * @brief ConnectionGater with a dynamic peer-id deny list.
     *
     * Rejects connections to/from peers that were added via BlockPeer() at
     * every stage of the upgrade pipeline where the remote peer is known:
     * interceptPeerDial, interceptAddrDial, interceptSecured and
     * interceptUpgraded. Connections intercepted at interceptAccept are
     * unknown at the raw stage, so they are allowed to proceed and get
     * rejected once the peer identity is established (interceptSecured).
     *
     * The deny list is protected by a mutex and may be modified at runtime
     * from any thread.
     */
    class DenyListConnectionGater final : public libp2p::network::ConnectionGater
    {
    public:
        /** Adds a peer to the deny list. Blocked peers cannot dial this
         * node and cannot be dialed by this node.
         * @param peer - the peer to block
         */
        void BlockPeer( const libp2p::peer::PeerId &peer )
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_denied_peers.insert( peer );
        }

        /** Adds several peers to the deny list at once.
         * @param peers - the peers to block
         */
        void BlockPeers( const std::vector<libp2p::peer::PeerId> &peers )
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_denied_peers.insert( peers.begin(), peers.end() );
        }

        /** Removes a peer from the deny list.
         * @param peer - the peer to unblock
         */
        void UnblockPeer( const libp2p::peer::PeerId &peer )
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_denied_peers.erase( peer );
        }

        /** Checks whether the peer is in the deny list.
         * @param peer - the peer to check
         * @return true if the peer is blocked
         */
        bool IsPeerBlocked( const libp2p::peer::PeerId &peer ) const
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            return m_denied_peers.find( peer ) != m_denied_peers.end();
        }

        /** Returns all blocked peers.
         * @return a snapshot of the deny list
         */
        std::vector<libp2p::peer::PeerId> GetBlockedPeers() const
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            return std::vector<libp2p::peer::PeerId>( m_denied_peers.begin(), m_denied_peers.end() );
        }        /** Removes all entries from the deny list. */
        void Clear()
        {
            std::lock_guard<std::mutex> lock( m_mutex );
            m_denied_peers.clear();
        }

        libp2p::outcome::result<void> interceptPeerDial( const libp2p::peer::PeerId &p ) override
        {
            if ( IsPeerBlocked( p ) )
            {
                return libp2p::network::ConnectionGaterError::GATER_REJECTED_PEER_DIAL;
            }
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> interceptAddrDial( const libp2p::peer::PeerId &        p,
                                                         const libp2p::multi::Multiaddress &addr ) override
        {
            if ( IsPeerBlocked( p ) )
            {
                return libp2p::network::ConnectionGaterError::GATER_REJECTED_ADDR_DIAL;
            }
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> interceptAccept( const libp2p::multi::Multiaddress &local,
                                                       const libp2p::multi::Multiaddress &remote ) override
        {
            // Remote peer identity is not established yet at this stage.
            // The connection gets checked in interceptSecured once the
            // security handshake reveals the remote peer id.
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> interceptSecured( bool                       is_initiator,
                                                        const libp2p::peer::PeerId &remote_peer,
                                                        const libp2p::multi::Multiaddress &remote_addr ) override
        {
            if ( IsPeerBlocked( remote_peer ) )
            {
                return libp2p::network::ConnectionGaterError::GATER_REJECTED_SECURED;
            }
            return libp2p::outcome::success();
        }

        libp2p::outcome::result<void> interceptUpgraded(
            const std::shared_ptr<libp2p::connection::CapableConnection> &conn ) override
        {
            if ( conn != nullptr )
            {
                auto remote_peer_res = conn->remotePeer();
                if ( remote_peer_res && IsPeerBlocked( remote_peer_res.value() ) )
                {
                    return libp2p::network::ConnectionGaterError::GATER_REJECTED_UPGRADED;
                }
            }
            return libp2p::outcome::success();
        }

    private:
        std::set<libp2p::peer::PeerId> m_denied_peers;
        mutable std::mutex             m_mutex;
    };
} // namespace sgns::ipfs_pubsub
