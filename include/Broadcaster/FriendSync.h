#pragma once

#include "Broadcaster/BroadcasterConfig.h"
#include "Broadcaster/PlayerHistory.h"

#include <atomic>

class MinecraftAuthentication;

/**
 * Keeps the broadcasting account's friends list usable: accepts the requests players send it, and removes friends
 * who have not joined for a while, since Xbox Live caps the size of the list.
 */
class FriendSync {
public:
    FriendSync(const BroadcasterConfig &config, MinecraftAuthentication &authentication, PlayerHistory &history,
               const std::atomic<bool> &stopping);

    void run();

private:
    void _acceptRequests();
    void _removeInactiveFriends();

    const BroadcasterConfig &mConfig;
    MinecraftAuthentication &mAuthentication;
    PlayerHistory &mHistory;
    const std::atomic<bool> &mStopping;
};
