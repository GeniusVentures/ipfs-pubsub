#ifndef IPFS_PUBSUB_LOGGER_HPP
#define IPFS_PUBSUB_LOGGER_HPP

#include <spdlog/spdlog.h>

namespace sgns::ipfs_pubsub
{
    using Logger = std::shared_ptr<spdlog::logger>;
    /**
    * Provide logger object
    * @param tag - tagging name for identifying logger
    * @return logger object
    */
    Logger createLogger(const std::string& tag);
}

#endif  // IPFS_PUBSUB_LOGGER_HPP
