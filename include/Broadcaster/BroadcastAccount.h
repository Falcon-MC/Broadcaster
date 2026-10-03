#pragma once

#include "Broadcaster/BroadcasterConfig.h"
#include "Broadcaster/PlayerHistory.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class BedrockListener;
class MinecraftAuthentication;
struct IncomingConnection;

/**
 * What the published world shows to friends, refreshed from the server status.
 */
struct Advertisement {
    std::string mServerName;
    std::string mSubName;
    int mPlayerCount = -1;
    int mMaxPlayers = 0;
};

/**
 * A snapshot of one account for the console.
 */
struct AccountStatus {
    std::string mName;
    std::string mGamertag;
    bool mOnline = false;
    std::string mReason;
    int mFriendCount = -1;
    long long mTransferred = 0;
};

/**
 * One Xbox Live account publishing the server in its friends list. It watches its own session and signaling, and
 * reconnects with a growing delay whenever friends can no longer reach it.
 */
class BroadcastAccount {
public:
    BroadcastAccount(BroadcastAccountConfig account, const BroadcasterConfigStore &configs, PlayerHistory &history,
                     const std::atomic<bool> &stopping);
    ~BroadcastAccount();

    BroadcastAccount(const BroadcastAccount &) = delete;
    BroadcastAccount &operator=(const BroadcastAccount &) = delete;

    bool signIn(std::string &outError);

    /**
     * Publishes the session and serves players until the broadcaster stops, reconnecting after every failure.
     */
    void run();

    void stop();

    void advertise(const Advertisement &advertisement);

    /**
     * Drops the current session and publishes it again, so changed session settings apply.
     */
    void reconnect();

    AccountStatus getStatus() const;

    const BroadcastAccountConfig &getConfig() const {
        return mAccount;
    }

private:
    bool _listen(std::string &outError);
    std::string _serve();
    void _closeListener();
    void _acceptPlayer(BedrockListener &listener);
    void _transfer(IncomingConnection incoming);
    void _keepPresence();
    void _setState(bool online, const std::string &reason);
    void _wait(int seconds);

    BroadcastAccountConfig mAccount;
    const BroadcasterConfigStore &mConfigs;
    PlayerHistory &mHistory;
    const std::atomic<bool> &mStopping;
    std::unique_ptr<MinecraftAuthentication> mAuthentication;
    std::unique_ptr<BedrockListener> mListener;
    mutable std::mutex mListenerMutex;
    Advertisement mAdvertisement;
    std::string mGamertag;
    bool mOnline = false;
    std::string mReason = "starting";
    std::atomic<bool> mReconnectRequested{false};
    std::atomic<int> mFriendCount{-1};
    std::atomic<long long> mTransferred{0};
    std::atomic<int> mActiveTransfers{0};
    std::vector<std::thread> mWorkers;
};
