This is the project for implementing IPFS pubsub system.

Please clone :

git clone --recurse-submodules ssh://git@gitlab.geniusventures.io:8487/GeniusVentures/ipfs-pubsub.git

# Build on Windows
# Build on Linux
## Dependency 
    - openssl
    - boost (tested on version 1.72.0)
    - protobuf 
## Build
    mkdir build & cd build
    mkdir Linux & cd Linux
    cmake ../.. -DOPENSSL_INCLUDE_DIR=/path_to/openssl
    cmake --build . --config Release
