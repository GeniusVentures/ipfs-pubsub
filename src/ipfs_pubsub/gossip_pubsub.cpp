#include "ipfs_pubsub/gossip_pubsub.hpp"

#include <iostream>
#include <boost/format.hpp>
#include <libp2p/injector/host_injector.hpp>

#include <boost/di/extension/scopes/shared.hpp>
#if defined(_WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

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
#if defined(_WIN32)
    // Windows implementation using GetAdaptersAddresses
    ULONG bufferSize = 15000;
    IP_ADAPTER_ADDRESSES* adapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);

    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapterAddresses, &bufferSize) == ERROR_BUFFER_OVERFLOW) {
        free(adapterAddresses);
        adapterAddresses = (IP_ADAPTER_ADDRESSES*)malloc(bufferSize);
    }

    std::string addr = "127.0.0.1"; // Default to localhost

    if (GetAdaptersAddresses(AF_INET, 0, nullptr, adapterAddresses, &bufferSize) == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES* adapter = adapterAddresses; adapter; adapter = adapter->Next) {
            if (adapter->OperStatus == IfOperStatusUp && adapter->IfType != IF_TYPE_SOFTWARE_LOOPBACK) {
                for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next) {
                    SOCKADDR* addrStruct = unicast->Address.lpSockaddr;
                    if (addrStruct->sa_family == AF_INET) { // For IPv4
                        char buffer[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(((struct sockaddr_in*)addrStruct)->sin_addr), buffer, INET_ADDRSTRLEN);
                        addr = buffer;
                        break;
                    }
                }
            }
            if (addr != "127.0.0.1") break; // Stop if we found a non-loopback IP
        }
    }

    free(adapterAddresses);
    return addr;

#else
    // Unix-like implementation using getifaddrs
    struct ifaddrs *ifaddr, *ifa;
    int family;
    std::string addr = "127.0.0.1"; // Default to localhost

    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return addr;
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        family = ifa->ifa_addr->sa_family;

        // We only want IPv4 addresses
        if (family == AF_INET && !(ifa->ifa_flags & IFF_LOOPBACK)) {
            char host[NI_MAXHOST];
            int s = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
            if (s == 0) {
                addr = host;
                break;
            }
        }
    }

    freeifaddrs(ifaddr);
    return addr;
#endif
}

boost::optional<libp2p::peer::PeerInfo> PeerInfoFromString(const std::string& addresses) {
    if (addresses.empty()) {
        return boost::none;
    }

    boost::optional<libp2p::peer::PeerId> peer_id;
    std::vector<libp2p::multi::Multiaddress> multiaddresses;

    //for (const auto& addr : addresses) {
        auto ma_res = libp2p::multi::Multiaddress::create(addresses);
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
    //}

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
    kademlia_config.randomWalk.interval = std::chrono::seconds(30);
    kademlia_config.requestConcurency = 3;
    kademlia_config.maxProvidersPerKey = 300;

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
        //Init Provid CIDs
        m_provideCids = std::vector<libp2p::protocol::kademlia::ContentId>();
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
        dht_ = std::make_shared<sgns::ipfs_lite::ipfs::dht::IpfsDHT>(kademlia, bootstrapAddresses_,m_context);

        //Make Holepunch Client
        m_holepunchmsgproc = std::make_shared<libp2p::protocol::HolepunchClientMsgProc>(*m_host, m_host->getNetwork().getConnectionManager());
        m_holepunch = std::make_shared<libp2p::protocol::HolepunchClient>(*m_host, m_holepunchmsgproc, m_host->getBus());
        m_holepunch->start();
        //Make Identify
        m_identifymsgproc = std::make_shared<libp2p::protocol::IdentifyMessageProcessor>(
            *m_host, m_host->getNetwork().getConnectionManager(), *injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>(), injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>());
        m_identify = std::make_shared<libp2p::protocol::Identify>(*m_host, m_identifymsgproc, m_host->getBus(), injector.create<std::shared_ptr<libp2p::transport::Upgrader>>(), [this]() { this->StartProvidingCID(); });       
        m_identify->start();
		// m_autonatmsgproc = std::make_shared<libp2p::protocol::AutonatMessageProcessor>(
        //      *m_host, m_host->getNetwork().getConnectionManager(), *injector.create<std::shared_ptr<libp2p::peer::IdentityManager>>(), injector.create<std::shared_ptr<libp2p::crypto::marshaller::KeyMarshaller>>());
		// m_autonat = std::make_shared<libp2p::protocol::Autonat>(*m_host, m_autonatmsgproc, m_host->getBus());  
		// m_autonat->start();
        // m_holepunchmsgproc = std::make_shared<libp2p::protocol::HolepunchMessageProcessor>(
        //       *m_host, m_host->getNetwork().getConnectionManager());
		// m_holepunch = std::make_shared<libp2p::protocol::Holepunch>(*m_host, m_holepunchmsgproc, m_host->getBus());  
		//m_autonat->start();
    }

std::future<std::error_code> GossipPubSub::Start(
    int listeningPort, 
    const std::vector<std::string>& booststrapPeers,
    const std::string& bindAddresses,
    const std::vector<std::string>& addAddresses)
{
        auto result = std::make_shared<std::promise<std::error_code>>();
        if (IsStarted())
        {
            m_logger->info((boost::format("%s PubSub service was previously started") % m_localAddress[0]).str());
            result->set_value(std::error_code());
            return result->get_future();
        }



        if (bindAddresses.empty()) {
            std::cout << "Using default bind addresses" << std::endl;
            //m_localAddress.push_back((boost::format("/ip4/%s/tcp/%d/p2p/%s") % GetLocalIP(*m_context) % listeningPort % m_host->getId().toBase58()).str());
            m_localAddress = (boost::format("/ip4/%s/tcp/%d/p2p/%s") % GetLocalIP(*m_context) % listeningPort % m_host->getId().toBase58()).str();
        } else {
            // Use provided bind addresses
            std::cout << "Using provided bind addresses" << std::endl;
            // for (const auto& address : bindAddresses)
            // {
            //     m_localAddress.push_back((boost::format("/ip4/%s/tcp/%d/p2p/%s") % address % listeningPort % m_host->getId().toBase58()).str());
                
            // }
            m_localAddress = bindAddresses;
        }

        if(!addAddresses.empty())
        {
            for (const auto& address : addAddresses)
            {
                auto ma = libp2p::multi::Multiaddress::create((boost::format("/ip4/%s/tcp/%d/p2p/%s") % address % listeningPort % m_host->getId().toBase58()).str());
                if(ma)
                {
                    m_localAddressAdditional.push_back(ma.value());
                }
                
            }           
        }
        m_logger->info((boost::format("%s: Starting PubSub service") % m_localAddress).str());

        // Tell gossip to connect to remote peers, only if specified
        for (const auto& remotePeerAddress : booststrapPeers)
        {
            //std::vector<std::string> remoteAddr = {remotePeerAddress};
            boost::optional<libp2p::peer::PeerInfo> remotePeerInfo = PeerInfoFromString(remotePeerAddress);
            if (remotePeerInfo)
            {
                m_gossip->addBootstrapPeer(remotePeerInfo->id, remotePeerInfo->addresses[0]);
            }
        }


        // Local address -> peer info
        boost::optional<libp2p::peer::PeerInfo> peerInfo = PeerInfoFromString(m_localAddress);
        if (!peerInfo)
        {
            auto errorMessage = (boost::format("%s: Cannot resolve local peer from the address") % m_localAddress).str();
            m_logger->error(errorMessage);
            result->set_value(GossipPubSubError::INVALID_LOCAL_ADDRESS);
            return result->get_future();
        }
        else
        {
            // Start the node as soon as async engine starts
            m_strand->post([result, peerInfo, this]
            {
                auto listen_res = m_host->listen(peerInfo->addresses[0]);
                if (!listen_res)
                {
                    m_context->stop();
                    m_logger->error("Cannot listen to multiaddress: {}, detais {}", 
                        peerInfo->addresses[0].getStringAddress(), 
                        listen_res.error().message());

                    result->set_value(GossipPubSubError::FAILED_LOCAL_ADDRESS_LISTENING);
                }
                else
                {

                    // Adding LAN and WAN addresses to the local peer
                    //m_host->getPeerRepository().getAddressRepository().upsertAddresses(peerInfo->id, peerInfo->addresses, libp2p::peer::ttl::kPermanent);
                    if(m_localAddressAdditional.size() > 0)
                    {
                        m_host->getPeerRepository().getAddressRepository().upsertAddresses(peerInfo->id, m_localAddressAdditional, libp2p::peer::ttl::kPermanent);
                    }
                    
                    m_host->start();
                    m_gossip->start();
                    m_logger->info((boost::format("%s : PubSub service started") % m_localAddress).str());
                    result->set_value(std::error_code());
                }
            });

            m_thread = std::thread([this]() { m_context->run(); });

            if (m_context->stopped())
            {
                auto errorMessage = (boost::format("%s: PubSub service failed to start") % m_localAddress).str();
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
                    if(provider.id != m_host->getId())
                    {
                        m_gossip->addBootstrapPeer(provider.id, provider.addresses[0]);   
                    }         
                }
                std::chrono::seconds interval(120);
                ScheduleNextFind(cid, interval);
                return true;
            }
            else
            {
                std::cout << "Empty providers list received" << std::endl;
                //StartFindingPeersWithRetry(ioc, cid, filename, addressoffset, parse, save, handle_read, status);
                std::chrono::seconds interval(120);
                ScheduleNextFind(cid, interval);
                return false;
            }
            });
    }

    bool GossipPubSub::StartFindingPeers(
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
                    std::cout << "New Peer: " << provider.id.toBase58() << std::endl;
                    for(auto& provaddr : provider.addresses)           
                    {
                        std::cout << provaddr.getStringAddress() << std::endl;

                    }
                    if(provider.id != m_host->getId())
                    {
                        m_gossip->addBootstrapPeer(provider.id, provider.addresses[0]);
                    }
                }
                std::chrono::seconds interval(120);
                ScheduleNextFind(key, interval);
                return true;
            }
            else
            {
                std::cout << "Empty providers list received" << std::endl;
                //StartFindingPeersWithRetry(ioc, cid, filename, addressoffset, parse, save, handle_read, status);
                std::chrono::seconds interval(120);
                ScheduleNextFind(key, interval);
                return false;
            }
            });     
    }

    void GossipPubSub::ScheduleNextFind(const libp2p::multi::ContentIdentifier& cid, std::chrono::seconds interval) {
        if (!m_timer) {
            m_timer = std::make_shared<boost::asio::steady_timer>(*m_context);
        }
        m_timer->expires_after(interval);
        m_timer->async_wait([=](const boost::system::error_code& ec) {
            if (!ec) {
                StartFindingPeers(cid);
            } else {
                std::cerr << "Timer error: " << ec.message() << std::endl;
            }
        });
    }

    void GossipPubSub::ScheduleNextFind(const libp2p::protocol::kademlia::ContentId& cid, std::chrono::seconds interval) {
        if (!m_timer) {
            m_timer = std::make_shared<boost::asio::steady_timer>(*m_context);
        }
        std::cout << "Schedule Next Find" << std::endl;
        m_timer->expires_after(interval);
        m_timer->async_wait([=](const boost::system::error_code& ec) {
            if (!ec) {
                std::cout << "Start Next Find" << std::endl;
                StartFindingPeers(cid);
            } else {
                std::cerr << "Timer error: " << ec.message() << std::endl;
            }
        });
    }

    void GossipPubSub::ProvideCID(const libp2p::protocol::kademlia::ContentId& key)
    {
        m_provideCids.push_back(key);
    }

    void GossipPubSub::StartProvidingCID()
    {
        for(auto& cid : m_provideCids)
        {
            dht_->ProvideCID(cid, true);
        }
        m_provideCids.clear();
    }

    void GossipPubSub::AddPeers(const std::vector<std::string>& booststrapPeers)
    {
        for (const auto& remotePeerAddress : booststrapPeers)
        {
            //std::vector<std::string> remoteAddr = {remotePeerAddress};
            boost::optional<libp2p::peer::PeerInfo> remotePeerInfo = PeerInfoFromString(remotePeerAddress);
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
