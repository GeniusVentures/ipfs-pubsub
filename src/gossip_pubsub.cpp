#include "gossip_pubsub.hpp"

#include <iostream>
#include <boost/format.hpp>
#include <libp2p/injector/gossip_injector.hpp>
#include <libp2p/injector/network_injector.hpp>

#include <boost/di/extension/scopes/shared.hpp>

OUTCOME_CPP_DEFINE_CATEGORY_2(GossipPubSubError, e) 
{
    using E = GossipPubSubError;
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
std::string FormatPeerId(const std::vector<uint8_t>& bytes) 
{
    auto res = libp2p::peer::PeerId::fromBytes(bytes);
    return res ? res.value().toBase58().substr(46) : "???";
}

std::string ToString(const std::vector<uint8_t>& buf)
{
    // NOLINTNEXTLINE
    return std::string(reinterpret_cast<const char*>(buf.data()), buf.size());
}

std::vector<uint8_t> FromString(const std::string& s)
{
    std::vector<uint8_t> ret{};
    auto sz = s.size();
    if (sz > 0) 
    {
        ret.reserve(sz);
        ret.assign(s.begin(), s.end());
    }
    return ret;
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

boost::optional<libp2p::peer::PeerInfo> PeerInfoFromString(const std::string& str) 
{
    auto server_ma_res = libp2p::multi::Multiaddress::create(str);
    if (!server_ma_res) 
    {
        return boost::none;
    }
    auto server_ma = std::move(server_ma_res.value());

    auto server_peer_id_str = server_ma.getPeerId();
    if (!server_peer_id_str) 
    {
        return boost::none;
    }

    auto server_peer_id_res = libp2p::peer::PeerId::fromBase58(*server_peer_id_str);
    if (!server_peer_id_res) 
    {
        return boost::none;
    }

    return libp2p::peer::PeerInfo{ server_peer_id_res.value(), {server_ma} };
}

template <typename... Ts>
auto makeHostedGossipInjector(Ts &&... args)
{
    using namespace libp2p;

    auto csprng = std::make_shared<crypto::random::BoostRandomGenerator>();
    auto ed25519_provider = std::make_shared<crypto::ed25519::Ed25519ProviderImpl>();
    auto rsa_provider = std::make_shared<crypto::rsa::RsaProviderImpl>();
    auto ecdsa_provider = std::make_shared<crypto::ecdsa::EcdsaProviderImpl>();
    auto secp256k1_provider = std::make_shared<crypto::secp256k1::Secp256k1ProviderImpl>();
    auto hmac_provider = std::make_shared<crypto::hmac::HmacProviderImpl>();
    std::shared_ptr<crypto::CryptoProvider> crypto_provider =
        std::make_shared<crypto::CryptoProviderImpl>(
            csprng, ed25519_provider, rsa_provider, ecdsa_provider,
            secp256k1_provider, hmac_provider);
    auto validator = std::make_shared<crypto::validator::KeyValidatorImpl>(crypto_provider);

    // @todo Check if there is no error in the keypair generation
    auto keypair = crypto_provider->generateKeys(crypto::Key::Type::Ed25519).value();

    return libp2p::injector::makeGossipInjector<
        boost::di::extension::shared_config>(
            boost::di::bind<crypto::CryptoProvider>().TEMPLATE_TO(
                crypto_provider)[boost::di::override],
            boost::di::bind<crypto::KeyPair>().TEMPLATE_TO(
                std::move(keypair))[boost::di::override],
            boost::di::bind<crypto::random::CSPRNG>().TEMPLATE_TO(
                std::move(csprng))[boost::di::override],
            boost::di::bind<crypto::marshaller::KeyMarshaller>()
            .TEMPLATE_TO<crypto::marshaller::KeyMarshallerImpl>()[boost::di::override],
            boost::di::bind<crypto::validator::KeyValidator>().TEMPLATE_TO(
                std::move(validator))[boost::di::override],

            std::forward<decltype(args)>(args)...);
}
}

GossipPubSub::GossipPubSub(libp2p::common::Logger logger)
    : m_logger(logger)
{
    // Overriding default config to see local messages as well (echo mode)
    libp2p::protocol::gossip::Config config;
    config.echo_forward_mode = true;

    // Objects creating
    // Injector creates and ties dependent objects
    auto injector = makeHostedGossipInjector(
        libp2p::injector::useGossipConfig(config)
    );

    // Create asio context
    m_context = injector.create<std::shared_ptr<boost::asio::io_context>>();
    m_strand = std::make_unique<boost::asio::io_context::strand>(*m_context);

    // host is our local libp2p node
    m_host = injector.create<std::shared_ptr<libp2p::Host>>();

    // Create gossip node
    m_gossip = injector.create<std::shared_ptr<libp2p::protocol::gossip::Gossip>>();
}

std::future<std::error_code> GossipPubSub::Start(
    int listeningPort, 
    const std::vector<std::string>& booststrapPeers)
{
    auto result = std::make_shared<std::promise<std::error_code>>();
    if (IsStarted())
    {
        m_logger->info((boost::format("%s PubSub service was previously started") % m_localAddress).str());
        result->set_value(std::error_code());
        return result->get_future();
    }

    // Make peer uri of local node
    m_localAddress = (boost::format("/ip4/%s/tcp/%d/p2p/%s") % GetLocalIP(*m_context) % listeningPort % m_host->getId().toBase58()).str();
    m_logger->info((boost::format("%s: Starting PubSub service") % m_localAddress).str());

    // Tell gossip to connect to remote peers, only if specified
    for (const auto& remotePeerAddress : booststrapPeers)
    {
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
                m_host->start();
                m_gossip->start();
                m_logger->info((boost::format("%s : PubSub service started") % m_localAddress).str());
                result->set_value(std::error_code());
            }
        });

        _serviceThread.reset(new std::thread([this]() { m_context->run(); }));

        if (m_context->stopped())
        {
            auto errorMessage = (boost::format("%s: PubSub service failed to start") % m_localAddress).str();
            m_logger->error(errorMessage);
            if (!result->get_future().valid())
            {
                result->set_value(GossipPubSubError::FAILED_SERVICE_START);
            }
            _serviceThread.reset();
        }
    }
    return result->get_future();
}

GossipPubSub::~GossipPubSub()
{
    if (_serviceThread)
    {
        Stop();
    }
}

void GossipPubSub::Stop()
{
    m_strand->post([this]()
    {
        if (!m_context->stopped())
        {
            m_gossip->stop();
            m_host->stop();
            m_context->stop();
        }
    });
    _serviceThread->join();
    _serviceThread.reset();
}


std::future<GossipPubSub::Subscription> GossipPubSub::Subscribe(const std::string& topic, MessageCallback onMessageCallback)
{
    auto subscription = std::make_shared<std::promise<GossipPubSub::Subscription>>();
    m_strand->post([subscription, this, topic, onMessageCallback]()
    {
        using Message = libp2p::protocol::gossip::Gossip::Message;
        // Forwarding is required to force assigment operator, otherwise subscription is cancelled.
        subscription->set_value(std::forward<Subscription>(m_gossip->subscribe({ topic }, onMessageCallback)));
        if (m_logger->should_log(spdlog::level::info))
        {
            m_logger->info((boost::format("%s: PubSub subscribed to topic '%s'") % m_localAddress % topic).str());
        }
    });
    return subscription->get_future();
}

void GossipPubSub::Publish(const std::string& topic, const std::string& message)
{
    m_strand->post([topic, message, this]()
    {
        m_gossip->publish({ topic }, FromString(message));
        if (m_logger->should_log(spdlog::level::debug))
        {
            m_logger->debug(
                (boost::format("%s: Message '%s' published to topic '%s'")
                    % m_localAddress % message % topic).str());
        }
    });
}

