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
 * Publishes the configured server as a world in the Xbox Live friends list of the signed in account, and sends
 * every player who joins it on to the server.
 */
class Broadcaster {
public:
    explicit Broadcaster(BroadcasterConfig config);
    ~Broadcaster();

    Broadcaster(const Broadcaster &) = delete;
    Broadcaster &operator=(const Broadcaster &) = delete;

    /**
     * Signs in, publishes the session and serves players until stop() is called. Returns false when the account
     * cannot sign in.
     */
    bool run(std::string &outError);

    void stop();

private:
    bool _signIn(std::string &outError);
    bool _listen(std::string &outError);
    void _acceptPlayers();
    void _transfer(IncomingConnection incoming);
    void _advertise();
    void _keepPresence();
    void _wait(int seconds);
    std::string _worldName() const;

    BroadcasterConfig mConfig;
    PlayerHistory mHistory;
    std::unique_ptr<MinecraftAuthentication> mAuthentication;
    std::unique_ptr<BedrockListener> mListener;
    std::mutex mListenerMutex;
    std::atomic<bool> mStopping{false};
    std::atomic<int> mActiveTransfers{0};
    std::vector<std::thread> mWorkers;
};
