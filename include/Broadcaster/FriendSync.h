#pragma once

#include "Broadcaster/BroadcasterConfig.h"
#include "Broadcaster/PlayerHistory.h"

#include <atomic>
#include <string>

class MinecraftAuthentication;

/**
 * Keeps a broadcasting account's friends list usable: removes friends who have not joined for a while, then accepts
 * the requests players send it while the list has room, since Xbox Live caps its size.
 */
class FriendSync {
public:
    FriendSync(std::string accountName, const BroadcasterConfigStore &configs, MinecraftAuthentication &authentication,
               PlayerHistory &history, std::atomic<int> &friendCount, const std::atomic<bool> &stopping);

    void run();

private:
    /**
     * Returns how many friends the account has after the removals, or -1 when the list could not be read.
     */
    int _removeInactiveFriends(const BroadcasterConfig &config);

    void _acceptRequests(const BroadcasterConfig &config, int friendCount);

    std::string mAccountName;
    const BroadcasterConfigStore &mConfigs;
    MinecraftAuthentication &mAuthentication;
    PlayerHistory &mHistory;
    std::atomic<int> &mFriendCount;
    const std::atomic<bool> &mStopping;
    bool mFull = false;
};
