#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

/**
 * When each friend last joined through the broadcaster, so friends who stopped playing can be removed to keep
 * room in the friends list.
 */
class PlayerHistory {
public:
    explicit PlayerHistory(std::string path);

    void load();

    void recordSeen(const std::string &xuid);

    /**
     * The last time the player joined, in milliseconds since the epoch, or 0 when they never did.
     */
    int64_t lastSeen(const std::string &xuid) const;

    /**
     * Starts tracking a friend that was never seen, so a new friend gets the full inactivity delay.
     */
    void recordIfUnknown(const std::string &xuid);

    void forget(const std::string &xuid);

private:
    void _save() const;

    std::string mPath;
    mutable std::mutex mMutex;
    std::map<std::string, int64_t> mLastSeen;
};
