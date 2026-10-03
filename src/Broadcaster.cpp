#include "Broadcaster/Broadcaster.h"

#include "Broadcaster/FriendSync.h"
#include "Core/Debug/BedrockLog.h"
#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/Auth/XboxLiveConfig.h"
#include "Network/Crypto/KeyPair.h"
#include "Network/Server/BedrockListener.h"
#include "Network/ServerQuery.h"
#include "Network/Session/XboxPresenceService.h"
#include "Protocol/Packets/ItemRegistryPacket.h"
#include "Protocol/Packets/JigsawStructureDataPacket.h"
#include "Protocol/Packets/StartGamePacket.h"
#include "Protocol/Packets/TransferPacket.h"
#include "Protocol/Packets/VoxelShapesPacket.h"
#include "Protocol/ProtocolInfo.h"

#include <chrono>
#include <iostream>
#include <utility>

namespace {

    const int ACCEPT_TIMEOUT_MS = 500;
    const int WAIT_STEP_MS = 200;
    const int RELISTEN_DELAY_SECONDS = 5;
    const int TRANSFER_DISCONNECT_TIMEOUT_MS = 10000;
    const int64_t REDIRECT_ENTITY_ID = 1;
    const int32_t REDIRECT_DIMENSION = 2;
    const float REDIRECT_SPAWN_HEIGHT = 66.0f;

}

Broadcaster::Broadcaster(BroadcasterConfig config)
        : mConfig(std::move(config)), mHistory(mConfig.mPlayerHistoryPath) {
}

Broadcaster::~Broadcaster() {
    stop();
    for (std::thread &worker: mWorkers) {
        if (worker.joinable())
            worker.join();
    }
    while (mActiveTransfers.load() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
}

void Broadcaster::stop() {
    mStopping.store(true);
    std::lock_guard<std::mutex> guard(mListenerMutex);
    if (mListener != nullptr)
        mListener->close();
}

bool Broadcaster::run(std::string &outError) {
    mHistory.load();
    if (!_signIn(outError))
        return false;

    mWorkers.emplace_back(&Broadcaster::_advertise, this);
    mWorkers.emplace_back(&Broadcaster::_keepPresence, this);
    if (mConfig.mFriendSync) {
        mWorkers.emplace_back([this] {
            FriendSync(mConfig, *mAuthentication, mHistory, mStopping).run();
        });
    }

    while (!mStopping.load()) {
        std::string error;
        if (!_listen(error)) {
            LOG_WARN(LogAreaID::Network, "Could not start the NetherNet listener: %s", error.c_str());
            _wait(RELISTEN_DELAY_SECONDS);
            continue;
        }

        _acceptPlayers();
        if (!mStopping.load()) {
            LOG_WARN(LogAreaID::Network, "The NetherNet listener stopped, starting it again");
            _wait(RELISTEN_DELAY_SECONDS);
        }
    }
    return true;
}

bool Broadcaster::_signIn(std::string &outError) {
    mAuthentication = std::make_unique<MinecraftAuthentication>(XboxLiveConfig::android(), mConfig.mTokenCachePath,
                                                                ProtocolInfo::MINECRAFT_VERSION_NETWORK);
    mAuthentication->getLiveAuthentication().setDeviceCodeCallback(
            [](const std::string &verificationUri, const std::string &userCode) {
                LOG_INFO(LogAreaID::Network, "Sign in to the broadcasting account at %s with the code %s",
                         verificationUri.c_str(), userCode.c_str());
            });

    const std::shared_ptr<KeyPair> key = KeyPair::generate();
    if (key == nullptr) {
        outError = "could not generate a key pair";
        return false;
    }

    MinecraftAuthenticationResult result;
    if (!mAuthentication->authenticate(*key, false, result, outError))
        return false;

    LOG_INFO(LogAreaID::Network, "Signed in as %s", result.mDisplayName.c_str());
    return true;
}

bool Broadcaster::_listen(std::string &outError) {
    ListenerSettings settings;
    settings.mServerName = _worldName();
    settings.mSubName = mConfig.mHostName.empty() ? mConfig.mServerHost : mConfig.mHostName;
    settings.mMaxPlayers = mConfig.mMaxPlayers;
    settings.mProtocolVersion = ProtocolInfo::CURRENT_PROTOCOL;
    settings.mGameVersion = ProtocolInfo::MINECRAFT_VERSION_NETWORK;
    settings.mRakNet = false;
    settings.mNetherNet = true;
    settings.mAuthentication = mAuthentication.get();
    settings.mOnlineSignaling =
            mConfig.mWebSocketSignaling ? NetherNetSignalingType::WebSocket : NetherNetSignalingType::JsonRpc;
    settings.mPublishSession = true;

    std::unique_ptr<BedrockListener> listener = std::make_unique<BedrockListener>();
    if (!listener->listen(settings, outError))
        return false;

    std::lock_guard<std::mutex> guard(mListenerMutex);
    mListener = std::move(listener);
    LOG_INFO(LogAreaID::Network, "Broadcasting %s, players are sent to %s:%u", settings.mServerName.c_str(),
             mConfig.mServerHost.c_str(), (unsigned int) mConfig.mServerPort);
    return true;
}

void Broadcaster::_acceptPlayers() {
    BedrockListener *listener;
    {
        std::lock_guard<std::mutex> guard(mListenerMutex);
        listener = mListener.get();
    }

    while (!mStopping.load() && listener->isListening()) {
        IncomingConnection incoming;
        if (!listener->accept(incoming, ACCEPT_TIMEOUT_MS))
            continue;

        mActiveTransfers.fetch_add(1);
        std::thread([this](IncomingConnection connection) {
            _transfer(std::move(connection));
            mActiveTransfers.fetch_sub(1);
        }, std::move(incoming)).detach();
    }
}

void Broadcaster::_transfer(IncomingConnection incoming) {
    BedrockConnection &connection = *incoming.mConnection;
    const std::string &name = incoming.mIdentity.mDisplayName;

    connection.send(JigsawStructureDataPacket());
    connection.send(VoxelShapesPacket());

    StartGamePacket start;
    start.mUniqueActorId = REDIRECT_ENTITY_ID;
    start.mRuntimeActorId = REDIRECT_ENTITY_ID;
    start.mPlayerGameType = GameType::Creative;
    start.mLevelGameType = GameType::Creative;
    start.mPlayerPosition = Vector3f(0.0f, REDIRECT_SPAWN_HEIGHT, 0.0f);
    start.mDimensionId = REDIRECT_DIMENSION;
    start.mVanillaVersion = "*";
    start.mLevelName = _worldName();
    start.mServerEngine = "Falcon Broadcaster";
    connection.send(start);
    connection.send(ItemRegistryPacket());

    TransferPacket transfer;
    transfer.mAddress = mConfig.mServerHost;
    transfer.mPort = mConfig.mServerPort;
    connection.send(transfer);
    connection.flush();

    if (!incoming.mIdentity.mXuid.empty())
        mHistory.recordSeen(incoming.mIdentity.mXuid);
    LOG_INFO(LogAreaID::Network, "Sent %s to %s:%u", name.c_str(), mConfig.mServerHost.c_str(),
             (unsigned int) mConfig.mServerPort);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TRANSFER_DISCONNECT_TIMEOUT_MS);
    while (!connection.isClosed() && !mStopping.load() && std::chrono::steady_clock::now() < deadline)
        connection.readPacket(WAIT_STEP_MS, &mStopping);

    if (!connection.isClosed())
        connection.close("transferred");
}

void Broadcaster::_advertise() {
    while (!mStopping.load()) {
        std::string serverName = _worldName();
        std::string subName = mConfig.mHostName.empty() ? mConfig.mServerHost : mConfig.mHostName;
        int playerCount = -1;
        int maxPlayers = mConfig.mMaxPlayers;

        if (mConfig.mQueryServer) {
            ServerQueryResult result;
            std::string error;
            if (ServerQuery::query(mConfig.mServerHost, mConfig.mServerPort, result, error)) {
                if (mConfig.mHostName.empty() && !result.mMotd.empty())
                    subName = result.mMotd;
                if (mConfig.mWorldName.empty() && !result.mSubMotd.empty())
                    serverName = result.mSubMotd;
                playerCount = result.mPlayerCount;
                if (result.mMaxPlayers > 0)
                    maxPlayers = result.mMaxPlayers;
            } else {
                LOG_WARN(LogAreaID::Network, "Could not query %s:%u: %s", mConfig.mServerHost.c_str(),
                         (unsigned int) mConfig.mServerPort, error.c_str());
            }
        }

        {
            std::lock_guard<std::mutex> guard(mListenerMutex);
            if (mListener != nullptr)
                mListener->advertise(serverName, subName, playerCount, maxPlayers);
        }
        _wait(mConfig.mUpdateIntervalSeconds);
    }
}

void Broadcaster::_keepPresence() {
    XboxPresenceService presence(*mAuthentication);
    while (!mStopping.load()) {
        int heartbeat = XboxPresenceService::DEFAULT_HEARTBEAT_SECONDS;
        std::string error;
        if (!presence.update(heartbeat, error))
            LOG_WARN(LogAreaID::Network, "Could not update the presence: %s", error.c_str());
        _wait(heartbeat);
    }
}

void Broadcaster::_wait(int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!mStopping.load() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
}

std::string Broadcaster::_worldName() const {
    if (!mConfig.mWorldName.empty())
        return mConfig.mWorldName;
    if (!mConfig.mHostName.empty())
        return mConfig.mHostName;
    return mConfig.mServerHost;
}
