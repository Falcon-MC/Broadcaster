#pragma once

#include "Broadcaster/BroadcastAccount.h"
#include "Broadcaster/BroadcasterConfig.h"
#include "Broadcaster/PlayerHistory.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * Publishes the configured server as a world in the Xbox Live friends list of every configured account, and sends
 * every player who joins it on to the server.
 */
class Broadcaster {
public:
    Broadcaster(std::string configPath, BroadcasterConfig config);
    ~Broadcaster();

    Broadcaster(const Broadcaster &) = delete;
    Broadcaster &operator=(const Broadcaster &) = delete;

    /**
     * Signs in every account, then broadcasts until stop() is called. Returns false when no account can sign in.
     */
    bool run(std::string &outError);

    void stop();

    /**
     * Reads the settings file again and applies it without restarting. Accounts are only read at startup.
     */
    bool reload(std::string &outError);

    std::vector<AccountStatus> getStatus() const;

    int getMaxFriends() const {
        return mConfigs.get()->mMaxFriends;
    }

private:
    void _advertise();
    Advertisement _queryAdvertisement(const BroadcasterConfig &config) const;
    void _wait(int seconds);

    std::string mConfigPath;
    BroadcasterConfigStore mConfigs;
    PlayerHistory mHistory;
    std::atomic<bool> mStopping{false};
    std::atomic<bool> mAdvertiseNow{false};
    mutable std::mutex mAccountsMutex;
    std::vector<std::unique_ptr<BroadcastAccount>> mAccounts;
};
