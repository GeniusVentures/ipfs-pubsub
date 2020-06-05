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
# Build for Android
## Dependency 
    - openssl
    - boost (tested on version 1.72.0)
    - protobuf 
    - Android NDK
## Build
    ○ cd ../lib2p
    ○ git checkout cross-compile
    ○ cd ..
    ○ mkdir build & cd build
    ○ mkdir Android & cd Android
    ○ export ANDROID_NDK=/path_to/android-ndk-root
    ○ export NDK_ROOT=$ANDROID_NDK
	○ export CROSS_COMPILER=$PATH:$ANDROID_NDK/prebuilt/linux-x86_64/bin
    ○ cmake ../.. -DOPENSSL_INCLUDE_DIR=/openssl_path_to/openssl  \
    -DCMAKE_SYSTEM_NAME="Android" -DCMAKETOOLCHAIN_FILE=$ANDROID_NDK/ \
    build/cmake/android.toolchain.cmake -DANDROID_ABI="armeabi-v7a, arm64-v8a,x86,x86_64" \
    -DANDROID_NATIVE_API_LEVEL=level_number_to  -DBOOST_ROOT=ndk_path_to/  \
    -DBOOST_INCLUDE_DIR=boost_root_path_to/include  -DBOOST_LIBRARY_DIR=boost_root_path_to/libs
    ○ make -j
