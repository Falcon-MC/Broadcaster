#include "Broadcaster/BroadcastAccount.h"

#include "Broadcaster/FriendSync.h"
#include "Core/Debug/BedrockLog.h"
#include "Network/Auth/MinecraftAuthentication.h"
#include "Network/Auth/XboxLiveConfig.h"
#include "Network/Crypto/KeyPair.h"
#include "Network/Server/BedrockListener.h"
#include "Network/Session/XboxPresenceService.h"
#include "Protocol/Packets/ItemRegistryPacket.h"
#include "Protocol/Packets/JigsawStructureDataPacket.h"
#include "Protocol/Packets/StartGamePacket.h"
#include "Protocol/Packets/TransferPacket.h"
#include "Protocol/Packets/VoxelShapesPacket.h"
#include "Protocol/ProtocolInfo.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

    const int ACCEPT_TIMEOUT_MS = 500;
    const int WAIT_STEP_MS = 200;
    const int HEALTH_CHECK_SECONDS = 15;
    const int MIN_RECONNECT_SECONDS = 5;
    const int MAX_RECONNECT_SECONDS = 300;
    const int STABLE_SECONDS = 120;
    const int SIGN_IN_AFTER_FAILURES = 3;
    const int TRANSFER_DISCONNECT_TIMEOUT_MS = 10000;
    const int64_t REDIRECT_ENTITY_ID = 1;
    const int32_t REDIRECT_DIMENSION = 2;
    const float REDIRECT_SPAWN_HEIGHT = 66.0f;

}

BroadcastAccount::BroadcastAccount(BroadcastAccountConfig account, const BroadcasterConfigStore &configs,
                                   PlayerHistory &history, const std::atomic<bool> &stopping)
        : mAccount(std::move(account)), mConfigs(configs), mHistory(history), mStopping(stopping) {
}

BroadcastAccount::~BroadcastAccount() {
    stop();
    for (std::thread &worker: mWorkers) {
        if (worker.joinable())
            worker.join();
    }
    while (mActiveTransfers.load() > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
}

void BroadcastAccount::stop() {
    std::lock_guard<std::mutex> guard(mListenerMutex);
    if (mListener != nullptr)
        mListener->close();
}

bool BroadcastAccount::signIn(std::string &outError) {
    if (mAuthentication == nullptr) {
        mAuthentication = std::make_unique<MinecraftAuthentication>(XboxLiveConfig::android(),
                                                                    mAccount.mTokenCachePath,
                                                                    ProtocolInfo::MINECRAFT_VERSION_NETWORK);
        const std::string name = mAccount.mName;
        mAuthentication->getLiveAuthentication().setDeviceCodeCallback(
                [name](const std::string &verificationUri, const std::string &userCode) {
                    LOG_INFO(LogAreaID::Network, "[%s] Sign in to the broadcasting account at %s with the code %s",
                             name.c_str(), verificationUri.c_str(), userCode.c_str());
                });
    }

    const std::shared_ptr<KeyPair> key = KeyPair::generate();
    if (key == nullptr) {
        outError = "could not generate a key pair";
        return false;
    }

    MinecraftAuthenticationResult result;
    if (!mAuthentication->authenticate(*key, false, result, outError))
        return false;

    {
        std::lock_guard<std::mutex> guard(mListenerMutex);
        mGamertag = result.mDisplayName;
    }
    LOG_INFO(LogAreaID::Network, "[%s] Signed in as %s", mAccount.mName.c_str(), result.mDisplayName.c_str());
    return true;
}

void BroadcastAccount::run() {
    mWorkers.emplace_back(&BroadcastAccount::_keepPresence, this);
    mWorkers.emplace_back([this] {
        FriendSync(mAccount.mName, mConfigs, *mAuthentication, mHistory, mFriendCount, mStopping).run();
    });

    int delay = MIN_RECONNECT_SECONDS;
    int failures = 0;
    while (!mStopping.load()) {
        const auto started = std::chrono::steady_clock::now();
        std::string reason;
        if (_listen(reason))
            reason = _serve();
        _closeListener();
        _setState(false, reason);
        if (mStopping.load())
            break;
        if (mReconnectRequested.exchange(false)) {
            LOG_INFO(LogAreaID::Network, "[%s] Publishing the session again with the new settings",
                     mAccount.mName.c_str());
            continue;
        }

        const auto uptime = std::chrono::steady_clock::now() - started;
        if (uptime >= std::chrono::seconds(STABLE_SECONDS)) {
            delay = MIN_RECONNECT_SECONDS;
            failures = 0;
        }

        failures++;
        LOG_WARN(LogAreaID::Network, "[%s] Friends cannot reach the world (%s), reconnecting in %d seconds",
                 mAccount.mName.c_str(), reason.c_str(), delay);
        _wait(delay);
        delay = std::min(delay * 2, MAX_RECONNECT_SECONDS);

        if (failures >= SIGN_IN_AFTER_FAILURES && !mStopping.load()) {
            failures = 0;
            std::string error;
            if (!signIn(error))
                LOG_WARN(LogAreaID::Network, "[%s] Could not sign in again: %s", mAccount.mName.c_str(),
                         error.c_str());
        }
    }
}

bool BroadcastAccount::_listen(std::string &outError) {
    const std::shared_ptr<const BroadcasterConfig> config = mConfigs.get();
    ListenerSettings settings;
    settings.mServerName = config->mWorldName;
    settings.mSubName = config->mHostName;
    {
        std::lock_guard<std::mutex> guard(mListenerMutex);
        if (!mAdvertisement.mServerName.empty())
            settings.mServerName = mAdvertisement.mServerName;
        if (!mAdvertisement.mSubName.empty())
            settings.mSubName = mAdvertisement.mSubName;
    }
    if (settings.mServerName.empty())
        settings.mServerName = config->mServerHost;
    if (settings.mSubName.empty())
        settings.mSubName = config->mServerHost;
    settings.mMaxPlayers = config->mMaxPlayers;
    settings.mProtocolVersion = ProtocolInfo::CURRENT_PROTOCOL;
    settings.mGameVersion = ProtocolInfo::MINECRAFT_VERSION_NETWORK;
    settings.mRakNet = false;
    settings.mNetherNet = true;
    settings.mAuthentication = mAuthentication.get();
    settings.mOnlineSignaling =
            config->mWebSocketSignaling ? NetherNetSignalingType::WebSocket : NetherNetSignalingType::JsonRpc;
    settings.mPublishSession = true;

    std::unique_ptr<BedrockListener> listener = std::make_unique<BedrockListener>();
    if (!listener->listen(settings, outError))
        return false;

    std::lock_guard<std::mutex> guard(mListenerMutex);
    if (mStopping.load()) {
        listener->close();
        outError = "stopping";
        return false;
    }
    if (!mAdvertisement.mServerName.empty()) {
        listener->advertise(mAdvertisement.mServerName, mAdvertisement.mSubName, mAdvertisement.mPlayerCount,
                            mAdvertisement.mMaxPlayers);
    }
    mListener = std::move(listener);
    LOG_INFO(LogAreaID::Network, "[%s] Broadcasting %s, players are sent to %s:%u", mAccount.mName.c_str(),
             settings.mServerName.c_str(), config->mServerHost.c_str(), (unsigned int) config->mServerPort);
    return true;
}

std::string BroadcastAccount::_serve() {
    BedrockListener *listener;
    {
        std::lock_guard<std::mutex> guard(mListenerMutex);
        listener = mListener.get();
    }

    auto nextCheck = std::chrono::steady_clock::now();
    while (!mStopping.load()) {
        if (mReconnectRequested.load())
            return "the settings were reloaded";
        if (std::chrono::steady_clock::now() >= nextCheck) {
            std::string reason;
            bool online;
            {
                std::lock_guard<std::mutex> guard(mListenerMutex);
                online = listener->isOnline(reason);
            }
            if (!online)
                return reason;
            _setState(true, "");
            nextCheck = std::chrono::steady_clock::now() + std::chrono::seconds(HEALTH_CHECK_SECONDS);
        }
        _acceptPlayer(*listener);
    }
    return "stopping";
}

void BroadcastAccount::_closeListener() {
    std::unique_ptr<BedrockListener> listener;
    {
        std::lock_guard<std::mutex> guard(mListenerMutex);
        listener = std::move(mListener);
    }
    if (listener != nullptr)
        listener->close();
}

void BroadcastAccount::_acceptPlayer(BedrockListener &listener) {
    IncomingConnection incoming;
    if (!listener.accept(incoming, ACCEPT_TIMEOUT_MS))
        return;

    mActiveTransfers.fetch_add(1);
    std::thread([this](IncomingConnection connection) {
        _transfer(std::move(connection));
        mActiveTransfers.fetch_sub(1);
    }, std::move(incoming)).detach();
}

void BroadcastAccount::_transfer(IncomingConnection incoming) {
    const std::shared_ptr<const BroadcasterConfig> config = mConfigs.get();
    BedrockConnection &connection = *incoming.mConnection;
    const std::string &name = incoming.mIdentity.mDisplayName;

    RoutedPlayer player;
    player.mName = name;
    player.mXuid = incoming.mIdentity.mXuid;
    player.mLanguageCode = incoming.mLanguageCode;
    player.mDeviceOS = incoming.mDeviceOS;
    const BroadcastRoute *route = config->route(player);
    const std::string host = route != nullptr ? route->mHost : config->mServerHost;
    const unsigned short port = route != nullptr ? route->mPort : config->mServerPort;

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
    start.mLevelName = config->mWorldName.empty() ? host : config->mWorldName;
    start.mServerEngine = "Falcon Broadcaster";
    connection.send(start);
    connection.send(ItemRegistryPacket());

    TransferPacket transfer;
    transfer.mAddress = host;
    transfer.mPort = port;
    connection.send(transfer);
    connection.flush();

    if (!incoming.mIdentity.mXuid.empty())
        mHistory.recordSeen(incoming.mIdentity.mXuid);
    mTransferred.fetch_add(1);
    LOG_INFO(LogAreaID::Network, "[%s] Sent %s to %s:%u", mAccount.mName.c_str(), name.c_str(), host.c_str(),
             (unsigned int) port);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TRANSFER_DISCONNECT_TIMEOUT_MS);
    while (!connection.isClosed() && !mStopping.load() && std::chrono::steady_clock::now() < deadline)
        connection.readPacket(WAIT_STEP_MS, &mStopping);

    if (!connection.isClosed())
        connection.close("transferred");
}

void BroadcastAccount::advertise(const Advertisement &advertisement) {
    std::lock_guard<std::mutex> guard(mListenerMutex);
    mAdvertisement = advertisement;
    if (mListener != nullptr) {
        mListener->advertise(advertisement.mServerName, advertisement.mSubName, advertisement.mPlayerCount,
                             advertisement.mMaxPlayers);
    }
}

void BroadcastAccount::reconnect() {
    mReconnectRequested.store(true);
}

AccountStatus BroadcastAccount::getStatus() const {
    AccountStatus status;
    status.mName = mAccount.mName;
    status.mFriendCount = mFriendCount.load();
    status.mTransferred = mTransferred.load();
    std::lock_guard<std::mutex> guard(mListenerMutex);
    status.mGamertag = mGamertag;
    status.mOnline = mOnline;
    status.mReason = mReason;
    return status;
}

void BroadcastAccount::_setState(bool online, const std::string &reason) {
    std::lock_guard<std::mutex> guard(mListenerMutex);
    mOnline = online;
    mReason = reason;
}

void BroadcastAccount::_keepPresence() {
    XboxPresenceService presence(*mAuthentication);
    while (!mStopping.load()) {
        int heartbeat = XboxPresenceService::DEFAULT_HEARTBEAT_SECONDS;
        std::string error;
        if (!presence.update(heartbeat, error))
            LOG_WARN(LogAreaID::Network, "[%s] Could not update the presence: %s", mAccount.mName.c_str(),
                     error.c_str());
        _wait(heartbeat);
    }
}

void BroadcastAccount::_wait(int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!mStopping.load() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
}
