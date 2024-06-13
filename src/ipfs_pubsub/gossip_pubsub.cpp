#include "ipfs_pubsub/gossip_pubsub.hpp"

#include <iostream>
#include <boost/format.hpp>
#include <libp2p/injector/host_injector.hpp>

#include <boost/di/extension/scopes/shared.hpp>

OUTCOME_CPP_DEFINE_CATEGORY_3(sgns::ipfs_pubsub, GossipPubSubError, e)
{
    using E = sgns::ipfs_pubsub::GossipPubSubError;
    switch (e) 
    {
    case E::INVALID_LOCAL_ADDRESS:
        return "Cannot resolve local peer from address";
    case E::FAILED_LOCAL_ADDRESS_LISTENING:
        return "Cannot listen to multiaddress";
    case E::FAILED_SERVICE_START:
        return "Failed to start pubsub service";
    }
    return "Unknown error";
}

namespace
{
std::string ToString(const std::vector<uint8_t>& buf)
{
    // NOLINTNEXTLINE
    return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
}

std::string GetLocalIP(boost::asio::io_context& io) 
{
    boost::asio::ip::tcp::resolver resolver(io);
    boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
    boost::asio::ip::tcp::resolver::iterator it = resolver.resolve(query);
    boost::asio::ip::tcp::resolver::iterator end;
    std::string addr("127.0.0.1");
    while (it != end) 
    {
        auto ep = it->endpoint();
        if (ep.address().is_v4()) 
        {
            addr = ep.address().to_string();
            break;
        }
        ++it;
    }
    return addr;
}

boost::optional<libp2p::peer::PeerInfo> PeerInfoFromString(const std::vector<std::string>& addresses) {
    if (addresses.empty()) {
        return boost::none;
    }

    boost::optional<libp2p::peer::PeerId> peer_id;
    std::vector<libp2p::multi::Multiaddress> multiaddresses;

    for (const auto& addr : addresses) {
        auto ma_res = libp2p::multi::Multiaddress::create(addr);
        if (!ma_res) {
            return boost::none;
        }

        auto ma = std::move(ma_res.value());
        multiaddresses.push_back(ma);

        if (!peer_id) {
            auto peer_id_str = ma.getPeerId();
            if (peer_id_str) {
                auto peer_id_res = libp2p::peer::PeerId::fromBase58(*peer_id_str);
                if (peer_id_res) {
                    peer_id = peer_id_res.value();
                }
            }
        }
    }

    if (!peer_id) {
        return boost::none;
    }

    return libp2p::peer::PeerInfo{*peer_id, multiaddresses};
}

template <typename... Ts>
auto makeCustomHostInjector(std::optional<libp2p::crypto::KeyPair> keyPair, Ts &&... args)
{
    using namespace libp2p;
    namespace di = boost::di;

    libp2p::protocol::kademlia::Config kademlia_config;
    kademlia_config.randomWalk.enabled = true;
    kademlia_config.randomWalk.interval = std::chrono::seconds(300);
    kademlia_config.requestConcurency = 20;

    auto csprng = std::make_shared<crypto::random::BoostRandomGenerator>();
    auto ed25519_provider = std::make_shared<crypto::ed25519::Ed25519ProviderImpl>();
    auto rsa_provider = std::make_shared<crypto::rsa::RsaProviderImpl>();
    auto ecdsa_provider = std::make_shared<crypto::ecdsa::EcdsaProviderImpl>();
    auto secp256k1_provider = std::make_shared<crypto::secp256k1::Secp256k1ProviderImpl>();
    auto hmac_provider = std::make_shared<crypto::hmac::HmacProviderImpl>();
    std::shared_ptr<crypto::CryptoProvider> crypto_provider =
        std::make_shared<crypto::CryptoProviderImpl>(
            csprng, ed25519_provider, rsa_provider, ecdsa_provider, secp256k1_provider, hmac_provider);
    auto validator = std::make_shared<crypto::validator::KeyValidatorImpl>(crypto_provider);

    if (!keyPair)
    {
        // @todo Check if there is no error in the keypair generation
        keyPair = crypto_provider->generateKeys(crypto::Key::Type::Ed25519).value();
    }

    auto injector = injector::makeHostInjector<di::extension::shared_config>(
        di::bind<crypto::CryptoProvider>().TEMPLATE_TO(crypto_provider)[di::override],
        di::bind<crypto::KeyPair>().TEMPLATE_TO(std::move(*keyPair))[di::override],
        di::bind<crypto::random::CSPRNG>().TEMPLATE_TO(std::move(csprng))[di::override],
        di::bind<crypto::marshaller::KeyMarshaller>().TEMPLATE_TO<crypto::marshaller::KeyMarshallerImpl>()[di::override],
        di::bind<crypto::validator::KeyValidator>().TEMPLATE_TO(std::move(validator))[di::override],
        //Kademlia
        libp2p::injector::makeKademliaInjector(
            libp2p::injector::useKademliaConfig(kademlia_config)),
        std::forward<decltype(args)>(args)...);

    return injector;
}
}

namespace sgns::ipfs_pubsub
{
    std::string GossipPubSub::FormatPeerId(const std::vector<uint8_t>& bytes)
    {
        auto res = libp2p::peer::PeerId::fromBytes(bytes);
        return res ? res.value().toBase58().substr(46) : "???";
    }

    GossipPubSub::GossipPubSub()
    {
        Init(std::optional<libp2p::crypto::KeyPair>());
    }

    GossipPubSub::GossipPubSub(libp2p::crypto::KeyPair keyPair)
    {
        Init(std::move(keyPair));
    }

    void GossipPubSub::Init(std::optional<libp2p::crypto::KeyPair> keyPair)
    {
        // Overriding default config to see local messages as well (echo mode)
        libp2p::protocol::gossip::Config config;
        config.echo_forward_mode = true;
        config.sign_messages = true;
        // Objects creating

        // Injector creates and ties dependent objects
        //auto injector = libp2p::injector::makeHostInjector();// std::move(keyPair));
        auto injector = makeCustomHostInjector(std::move(keyPair));

        // Create asio context
        m_context = injector.create<std::shared_ptr<boost::asio::io_context>>();
        m_strand = std::make_unique<boost::asio::io_context::strand>(*m_context);

        // host is our local libp2p node
        m_host = injector.create<std::shared_ptr<libp2p::Host>>();

        // Create gossip node
        m_gossip = libp2p::protocol::gossip::create(
            injector.create<std::shared_ptr<libp2p::basic::Scheduler>>(), m_host,
            injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>(),
            injector.create<std::shared_ptr<libp2p::crypto::CryptoProvider>>(),
            injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>(),
            std::move(config));

        //Make a DHT
        auto kademlia =
            injector
            .create<std::shared_ptr<libp2p::protocol::kademlia::Kademlia>>();
                //Initialize DHT
        dht_ = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>(kademlia, bootstrapAddresses_);

        //Make Identify
        m_identifymsgproc = std::make_shared<libp2p::protocol::IdentifyMessageProcessor>(
            *m_host, m_host->getNetwork().getConnectionManager(), *injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>(), injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>());
        m_identify = std::make_shared<libp2p::protocol::Identify>(*m_host, m_identifymsgproc, m_host->getBus());       
        m_identify->start();
    }

std::future<std::error_code> GossipPubSub::Start(
    int listeningPort, 
    const std::vector<std::string>& booststrapPeers,
    const std::vector<std::string>& bindAddresses)
{
        auto result = std::make_shared<std::promise<std::error_code>>();
        if (IsStarted())
        {
            m_logger->info((boost::format("%s PubSub service was previously started") % m_localAddress[0]).str());
            result->set_value(std::error_code());
            return result->get_future();
        }

        // Make peer uri of local node
        if (bindAddresses.empty()) {
            std::cout << "Using default bind addresses" << std::endl;
            m_localAddress.push_back((boost::format("/ip4/%s/tcp/%d/p2p/%s") % GetLocalIP(*m_context) % listeningPort % m_host->getId().toBase58()).str());
        } else {
            // Use provided bind addresses
            std::cout << "Using provided bind addresses" << std::endl;
            for (const auto& address : bindAddresses)
            {
                m_localAddress.push_back((boost::format("/ip4/%s/tcp/%d/p2p/%s") % address % listeningPort % m_host->getId().toBase58()).str());
            }
        }
        m_logger->info((boost::format("%s: Starting PubSub service") % m_localAddress[0]).str());

        // Tell gossip to connect to remote peers, only if specified
        for (const auto& remotePeerAddress : booststrapPeers)
        {
            std::vector<std::string> remoteAddr = {remotePeerAddress};
            boost::optional<libp2p::peer::PeerInfo> remotePeerInfo = PeerInfoFromString(remoteAddr);
            if (remotePeerInfo)
            {
                m_gossip->addBootstrapPeer(remotePeerInfo->id, remotePeerInfo->addresses[0]);
            }
        }


        // Local address -> peer info
        boost::optional<libp2p::peer::PeerInfo> peerInfo = PeerInfoFromString(m_localAddress);
        if (!peerInfo)
        {
            auto errorMessage = (boost::format("%s: Cannot resolve local peer from the address") % m_localAddress[0]).str();
            m_logger->error(errorMessage);
            result->set_value(GossipPubSubError::INVALID_LOCAL_ADDRESS);
            return result->get_future();
        }
        else
        {
            // Start the node as soon as async engine starts
            m_strand->post([result, peerInfo, this] {
                bool all_successful = true;
                for (const auto& address : peerInfo->addresses) {
                    auto listen_res = m_host->listen(address);
                    if (!listen_res) {
                        m_context->stop();
                        m_logger->error("Cannot listen to multiaddress: {}, details {}", 
                            address.getStringAddress(), 
                            listen_res.error().message());
                        result->set_value(GossipPubSubError::FAILED_LOCAL_ADDRESS_LISTENING);
                        all_successful = false;
                        break;
                    }
                }

                if (all_successful) {
                    m_host->start();
                    m_gossip->start();
                    m_logger->info((boost::format("%s : PubSub service started") % m_localAddress[0]).str());
                    result->set_value(std::error_code());
                }
            });

            m_thread = std::thread([this]() { m_context->run(); });

            if (m_context->stopped())
            {
                auto errorMessage = (boost::format("%s: PubSub service failed to start") % m_localAddress[0]).str();
                m_logger->error(errorMessage);
                if (!result->get_future().valid())
                {
                    result->set_value(GossipPubSubError::FAILED_SERVICE_START);
                }
            }
        }
        return result->get_future();
    }


    bool GossipPubSub::StartFindingPeers(
        std::shared_ptr<boost::asio::io_context> ioc,
        const libp2p::multi::ContentIdentifier& cid
    )
    {
        // auto peer_id =
        //     libp2p::peer::PeerId::fromHash(cid.content_address).value();
         return dht_->FindProviders(cid, [=](libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> res) {
            if (!res) {
                std::cerr << "Cannot find providers: " << res.error().message() << std::endl;
                return false;
            }
            auto& providers = res.value();
            if (!providers.empty())
            {
                for (auto& provider : providers) {
                    m_gossip->addBootstrapPeer(provider.id, provider.addresses[0]);               
                }
                return true;
            }
            else
            {
                std::cout << "Empty providers list received" << std::endl;
                //StartFindingPeersWithRetry(ioc, cid, filename, addressoffset, parse, save, handle_read, status);
                return false;
            }
            });
    }
    bool GossipPubSub::StartFindingPeers(
            std::shared_ptr<boost::asio::io_context> ioc,
            const libp2p::protocol::kademlia::ContentId& key
        )
    {
        // auto peer_id =
        //     libp2p::peer::PeerId::fromHash(cid.content_address).value();
        return dht_->FindProviders(key, [=](libp2p::outcome::result<std::vector<libp2p::peer::PeerInfo>> res) {
            if (!res) {
                std::cerr << "Cannot find providers: " << res.error().message() << std::endl;
                return false;
            }
            auto& providers = res.value();
            if (!providers.empty())
            {
                for (auto& provider : providers) {
                    m_gossip->addBootstrapPeer(provider.id, provider.addresses[0]);    
                    std::cout << "New Peer: " << provider.id.toBase58() << std::endl;
                    for(auto& provaddr : provider.addresses)           
                    {
                        std::cout << provaddr.getStringAddress() << std::endl;

                    }
                }
                return true;
            }
            else
            {
                std::cout << "Empty providers list received" << std::endl;
                //StartFindingPeersWithRetry(ioc, cid, filename, addressoffset, parse, save, handle_read, status);
                return false;
            }
            });     
    }
    void GossipPubSub::AddPeers(const std::vector<std::string>& booststrapPeers)
    {
        for (const auto& remotePeerAddress : booststrapPeers)
        {
            std::vector<std::string> remoteAddr = {remotePeerAddress};
            boost::optional<libp2p::peer::PeerInfo> remotePeerInfo = PeerInfoFromString(remoteAddr);
            if (remotePeerInfo)
            {
                m_gossip->addBootstrapPeer(remotePeerInfo->id, remotePeerInfo->addresses[0]);
            }
        }
    }

    GossipPubSub::~GossipPubSub()
    {
        Stop();
    }

    void GossipPubSub::Stop()
    {
        auto stopF = [this]()
        {
            if (!m_context->stopped())
            {
                m_gossip->stop();
                m_host->stop();
                m_context->stop();
            }
        };

        if (m_thread.get_id() == std::this_thread::get_id())
        {
            stopF();
        }
        else
        {
            m_strand->post(stopF);
            if (m_thread.joinable())
            {
                m_thread.join();
            }
        }
    }

    void GossipPubSub::Wait()
    {
        if (!(m_thread.get_id() == std::this_thread::get_id()) && m_thread.joinable())
        {
            m_thread.join();
        }
    }

    std::future<GossipPubSub::Subscription> GossipPubSub::Subscribe(const std::string& topic, MessageCallback onMessageCallback)
    {
        auto subscription = std::make_shared<std::promise<GossipPubSub::Subscription>>();
        auto subsF = [subscription, this, topic, onMessageCallback]()
        {
            using Message = libp2p::protocol::gossip::Gossip::Message;
            // Forwarding is required to force assigment operator, otherwise subscription is cancelled.
            subscription->set_value(std::forward<Subscription>(m_gossip->subscribe({ topic }, onMessageCallback)));
            if (m_logger->should_log(spdlog::level::info))
            {
                m_logger->info((boost::format("%s: PubSub subscribed to topic '%s'") % m_localAddress[0] % topic).str());
            }
        };

        if (m_thread.get_id() == std::this_thread::get_id())
        {
            // Subscribe synchronously when the method is called from a pubsub callback
            // For instance the method can be called from a topic message processing callback
            // Otherwise a waiting for the subscription can lead to a dead lock
            subsF();
        }
        else
        {
            m_strand->post(subsF);
        }
        return subscription->get_future();
    }

    void GossipPubSub::Publish(const std::string& topic, const std::vector<uint8_t>& message)
    {
        m_strand->post([topic, message, this]()
        {
            m_gossip->publish({ topic }, message);
            if (m_logger->should_log(spdlog::level::debug))
            {
                m_logger->debug(
                    (boost::format("%s: Message published to topic '%s'")
                        % m_localAddress[0] % topic).str());
            }
        });
    }

    std::shared_ptr<boost::asio::io_context> GossipPubSub::GetAsioContext() const
    {
        return m_context;
    }
}
