#include "Broadcaster/FriendSync.h"

#include "Core/Debug/BedrockLog.h"
#include "Network/Session/XboxSocialService.h"

#include <chrono>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {

    const int64_t MILLIS_PER_DAY = 24LL * 60 * 60 * 1000;
    const int WAIT_STEP_MS = 200;

    int64_t nowMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

}

FriendSync::FriendSync(std::string accountName, const BroadcasterConfigStore &configs,
                       MinecraftAuthentication &authentication, PlayerHistory &history, std::atomic<int> &friendCount,
                       const std::atomic<bool> &stopping)
        : mAccountName(std::move(accountName)), mConfigs(configs), mAuthentication(authentication),
          mHistory(history), mFriendCount(friendCount), mStopping(stopping) {
}

void FriendSync::run() {
    while (!mStopping.load()) {
        const std::shared_ptr<const BroadcasterConfig> config = mConfigs.get();
        if (config->mFriendSync) {
            const int friendCount = _removeInactiveFriends(*config);
            if (friendCount >= 0)
                mFriendCount.store(friendCount);
            if (config->mAcceptFriendRequests && friendCount >= 0)
                _acceptRequests(*config, friendCount);
        }

        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(config->mFriendSyncIntervalSeconds);
        while (!mStopping.load() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
    }
}

int FriendSync::_removeInactiveFriends(const BroadcasterConfig &config) {
    XboxSocialService social(mAuthentication);
    social.setCancelFlag(&mStopping);

    std::vector<XboxPerson> friends;
    std::string error;
    if (!social.requestPeople(XboxPeopleList::Friends, friends, error)) {
        LOG_WARN(LogAreaID::Network, "[%s] Could not read the friends list: %s", mAccountName.c_str(), error.c_str());
        return -1;
    }

    int remaining = (int) friends.size();
    if (!config.mRemoveInactiveFriends)
        return remaining;

    const int64_t limit = (int64_t) config.mInactiveDays * MILLIS_PER_DAY;
    const int64_t now = nowMillis();
    for (const XboxPerson &person: friends) {
        if (mStopping.load())
            return remaining;

        mHistory.recordIfUnknown(person.mXuid);
        if (now - mHistory.lastSeen(person.mXuid) < limit)
            continue;

        if (!social.removeFriend(person.mXuid, error)) {
            LOG_WARN(LogAreaID::Network, "[%s] Could not remove the inactive friend %s: %s", mAccountName.c_str(),
                     person.mGamertag.c_str(), error.c_str());
            continue;
        }

        mHistory.forget(person.mXuid);
        remaining--;
        LOG_INFO(LogAreaID::Network, "[%s] Removed %s, who has not joined for %d days", mAccountName.c_str(),
                 person.mGamertag.c_str(), config.mInactiveDays);
    }
    return remaining;
}

void FriendSync::_acceptRequests(const BroadcasterConfig &config, int friendCount) {
    if (friendCount >= config.mMaxFriends) {
        if (!mFull)
            LOG_WARN(LogAreaID::Network, "[%s] The friends list is full (%d), new requests wait for room",
                     mAccountName.c_str(), friendCount);
        mFull = true;
        return;
    }
    mFull = false;

    XboxSocialService social(mAuthentication);
    social.setCancelFlag(&mStopping);

    std::vector<XboxPerson> requests;
    std::string error;
    if (!social.requestPeople(XboxPeopleList::ReceivedRequests, requests, error)) {
        LOG_WARN(LogAreaID::Network, "[%s] Could not read the friend requests: %s", mAccountName.c_str(),
                 error.c_str());
        return;
    }

    for (const XboxPerson &person: requests) {
        if (mStopping.load())
            return;
        if (friendCount >= config.mMaxFriends) {
            LOG_WARN(LogAreaID::Network, "[%s] The friends list is full (%d), new requests wait for room",
                     mAccountName.c_str(), friendCount);
            mFull = true;
            return;
        }

        if (!social.addFriend(person.mXuid, error)) {
            LOG_WARN(LogAreaID::Network, "[%s] Could not accept the friend request of %s: %s", mAccountName.c_str(),
                     person.mGamertag.c_str(), error.c_str());
            continue;
        }

        friendCount++;
        mFriendCount.store(friendCount);
        mHistory.recordIfUnknown(person.mXuid);
        LOG_INFO(LogAreaID::Network, "[%s] Accepted the friend request of %s", mAccountName.c_str(),
                 person.mGamertag.c_str());
    }
}
