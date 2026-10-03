#pragma once

#include <string>

/**
 * The settings read from broadcaster.json.
 */
struct BroadcasterConfig {
    std::string mServerHost;
    unsigned short mServerPort = 19132;

    std::string mHostName;
    std::string mWorldName;
    int mMaxPlayers = 20;
    bool mQueryServer = true;
    int mUpdateIntervalSeconds = 30;
    bool mWebSocketSignaling = true;

    bool mFriendSync = true;
    int mFriendSyncIntervalSeconds = 60;
    bool mAcceptFriendRequests = true;
    bool mRemoveInactiveFriends = true;
    int mInactiveDays = 8;

    std::string mTokenCachePath = "cache/token.json";
    std::string mPlayerHistoryPath = "cache/player_history.json";

    /**
     * Loads the file, or writes the default settings to it when it does not exist yet.
     * outCreated reports that the defaults were just written and must be edited first.
     */
    static bool load(const std::string &path, BroadcasterConfig &outConfig, bool &outCreated, std::string &outError);

    bool validate(std::string &outError) const;

    std::string toJson() const;
};
