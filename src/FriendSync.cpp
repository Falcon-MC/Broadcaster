#include "Broadcaster/FriendSync.h"

#include "Core/Debug/BedrockLog.h"
#include "Network/Session/XboxSocialService.h"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {

    const int64_t MILLIS_PER_DAY = 24LL * 60 * 60 * 1000;
    const int WAIT_STEP_MS = 200;

    int64_t nowMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

}

FriendSync::FriendSync(const BroadcasterConfig &config, MinecraftAuthentication &authentication,
                       PlayerHistory &history, const std::atomic<bool> &stopping)
        : mConfig(config), mAuthentication(authentication), mHistory(history), mStopping(stopping) {
}

void FriendSync::run() {
    while (!mStopping.load()) {
        if (mConfig.mAcceptFriendRequests)
            _acceptRequests();
        if (mConfig.mRemoveInactiveFriends)
            _removeInactiveFriends();

        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(mConfig.mFriendSyncIntervalSeconds);
        while (!mStopping.load() && std::chrono::steady_clock::now() < until)
            std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
    }
}

void FriendSync::_acceptRequests() {
    XboxSocialService social(mAuthentication);
    social.setCancelFlag(&mStopping);

    std::vector<XboxPerson> requests;
    std::string error;
    if (!social.requestPeople(XboxPeopleList::ReceivedRequests, requests, error)) {
        LOG_WARN(LogAreaID::Network, "Could not read the friend requests: %s", error.c_str());
        return;
    }

    for (const XboxPerson &person: requests) {
        if (mStopping.load())
            return;

        if (!social.addFriend(person.mXuid, error)) {
            LOG_WARN(LogAreaID::Network, "Could not accept the friend request of %s: %s", person.mGamertag.c_str(),
                     error.c_str());
            continue;
        }

        mHistory.recordIfUnknown(person.mXuid);
        LOG_INFO(LogAreaID::Network, "Accepted the friend request of %s", person.mGamertag.c_str());
    }
}

void FriendSync::_removeInactiveFriends() {
    XboxSocialService social(mAuthentication);
    social.setCancelFlag(&mStopping);

    std::vector<XboxPerson> friends;
    std::string error;
    if (!social.requestPeople(XboxPeopleList::Friends, friends, error)) {
        LOG_WARN(LogAreaID::Network, "Could not read the friends list: %s", error.c_str());
        return;
    }

    const int64_t limit = (int64_t) mConfig.mInactiveDays * MILLIS_PER_DAY;
    const int64_t now = nowMillis();
    for (const XboxPerson &person: friends) {
        if (mStopping.load())
            return;

        mHistory.recordIfUnknown(person.mXuid);
        if (now - mHistory.lastSeen(person.mXuid) < limit)
            continue;

        if (!social.removeFriend(person.mXuid, error)) {
            LOG_WARN(LogAreaID::Network, "Could not remove the inactive friend %s: %s", person.mGamertag.c_str(),
                     error.c_str());
            continue;
        }

        mHistory.forget(person.mXuid);
        LOG_INFO(LogAreaID::Network, "Removed %s, who has not joined for %d days", person.mGamertag.c_str(),
                 mConfig.mInactiveDays);
    }
}
