This is the project for implementing IPFS pubsub system.

Please clone :

git clone --recurse-submodules ssh://github.com/GeniusVentures/ipfs-pubsub.git

# Build on Windows
## Dependency 
    - openssl
    - boost (tested on version 1.78.0)
    - protobuf 
## Build
    cmake ../.. -G "Visual Studio 15 2017 Win64"    -DBUILD_TESTING=OFF -DCMAKE_USE_OPENSSL=ON     -DBOOST_ROOT="C:/local/boost_1_72_0"  \
        -DBOOST_INCLUDE_DIR="C:/local/boost_1_72_0"     -DBOOST_LIBRARY_DIR="C:/local/boost_1_72_0/lib64-msvc-14.1"     -DOPENSSL_ROOT_DIR="C:/Program Files/OpenSSL-Win64"
    cmake --build . --config Release
# Build on Linux
    
## Dependency 
    - openssl
    - boost (tested on version 1.78.0)
    - protobuf 
## Build
    mkdir build & cd build
    mkdir Linux & cd Linux
    cmake ../.. -DOPENSSL_INCLUDE_DIR=/path_openssl_to/openssl  -DBOOST_INCLUDE_DIR=/path_boost_to/boost
    make
# Build for Android
## Dependency 
    - openssl
    - boost (tested on version 1.78.0)
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
