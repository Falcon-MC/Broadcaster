#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * One Xbox Live account that broadcasts the server. Each account has its own friends list, so several accounts
 * share the players once one list is full.
 */
struct BroadcastAccountConfig {
    std::string mName;
    std::string mTokenCachePath;

    bool operator==(const BroadcastAccountConfig &other) const {
        return mName == other.mName && mTokenCachePath == other.mTokenCachePath;
    }
};

/**
 * Sends the players it matches to another server than the default one. An empty list matches everyone, so a route
 * with no condition at all catches every player.
 */
struct BroadcastRoute {
    std::string mHost;
    unsigned short mPort = 19132;
    std::vector<std::string> mLanguages;
    std::vector<std::string> mPlatforms;
    std::vector<std::string> mPlayers;
};

/**
 * The player data a route is matched against.
 */
struct RoutedPlayer {
    std::string mName;
    std::string mXuid;
    std::string mLanguageCode;
    int mDeviceOS = -1;
};

/**
 * The settings read from broadcaster.json.
 */
struct BroadcasterConfig {
    std::string mServerHost;
    unsigned short mServerPort = 19132;
    std::vector<BroadcastRoute> mRoutes;

    std::string mHostName;
    std::string mWorldName;
    int mMaxPlayers = 20;
    bool mQueryServer = true;
    int mUpdateIntervalSeconds = 30;
    bool mWebSocketSignaling = true;

    std::vector<BroadcastAccountConfig> mAccounts;

    bool mFriendSync = true;
    int mFriendSyncIntervalSeconds = 60;
    bool mAcceptFriendRequests = true;
    bool mRemoveInactiveFriends = true;
    int mInactiveDays = 8;
    int mMaxFriends = 1000;

    std::string mPlayerHistoryPath = "cache/player_history.json";

    /**
     * Loads the file, or writes the default settings to it when it does not exist yet.
     * outCreated reports that the defaults were just written and must be edited first.
     */
    static bool load(const std::string &path, BroadcasterConfig &outConfig, bool &outCreated, std::string &outError);

    /**
     * The name of a DeviceOS value as written in routes, or an empty string when it has none.
     */
    static std::string platformName(int deviceOS);

    bool validate(std::string &outError) const;

    /**
     * The server a player is sent to: the first route matching them, or the default server.
     */
    const BroadcastRoute *route(const RoutedPlayer &player) const;

    std::string toJson() const;
};

/**
 * Holds the settings in use, so a reload swaps them while accounts keep running.
 */
class BroadcasterConfigStore {
public:
    explicit BroadcasterConfigStore(BroadcasterConfig config)
            : mConfig(std::make_shared<const BroadcasterConfig>(std::move(config))) {
    }

    std::shared_ptr<const BroadcasterConfig> get() const {
        std::lock_guard<std::mutex> guard(mMutex);
        return mConfig;
    }

    void set(BroadcasterConfig config) {
        std::shared_ptr<const BroadcasterConfig> next = std::make_shared<const BroadcasterConfig>(std::move(config));
        std::lock_guard<std::mutex> guard(mMutex);
        mConfig = std::move(next);
    }

private:
    mutable std::mutex mMutex;
    std::shared_ptr<const BroadcasterConfig> mConfig;
};
