
#ifndef KAD_FACTORY_HPP
#define KAD_FACTORY_HPP

#include <libp2p/crypto/crypto_provider.hpp>
#include <libp2p/crypto/key_marshaller.hpp>
#include <libp2p/host/host.hpp>
#include <libp2p/protocol/kademlia/routing_table.hpp>

namespace pubsub::discovery {
  std::shared_ptr<boost::asio::io_context> createIOContext();

  struct PerHostObjects {
    std::shared_ptr<libp2p::Host> host;
    std::shared_ptr<libp2p::protocol::kademlia::RoutingTable> routing_table;
    std::shared_ptr<libp2p::crypto::CryptoProvider> key_gen;
    std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller> key_marshaller;
  };

  void createPerHostObjects(PerHostObjects &objects,
                            const KademliaConfig &conf);

  boost::optional<libp2p::peer::PeerInfo> str2peerInfo(const std::string &str);

}  // namespace 

#endif  // FACTORY
