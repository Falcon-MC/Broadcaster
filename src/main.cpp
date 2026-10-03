#include "Broadcaster/Broadcaster.h"
#include "Broadcaster/BroadcasterConfig.h"

#include "Core/Debug/BedrockLog.h"
#include "Core/Debug/ContentLogEndPoint.h"
#include "Core/Debug/FileLogEndPoint.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {

    const char *const DEFAULT_CONFIG_PATH = "broadcaster.json";
    const char *const LOG_FILE = "logs/broadcaster.log";
    const int SIGNAL_POLL_MS = 200;

    std::atomic<bool> interrupted{false};

    void onSignal(int) {
        interrupted.store(true);
    }

    void setUpLogging() {
        BedrockLog::setLogLevel(LogLevel::Info);
        BedrockLog::addEndPoint(std::make_shared<ContentLogEndPoint>());

        std::error_code ignored;
        std::filesystem::create_directories(std::filesystem::path(LOG_FILE).parent_path(), ignored);
        std::shared_ptr<FileLogEndPoint> file = std::make_shared<FileLogEndPoint>(LOG_FILE);
        if (file->isOpen())
            BedrockLog::addEndPoint(file);
    }

    void printStatus(const Broadcaster &broadcaster) {
        for (const AccountStatus &status: broadcaster.getStatus()) {
            if (status.mOnline) {
                LOG_INFO(LogAreaID::Network, "[%s] %s: online, %lld player(s) sent", status.mName.c_str(),
                         status.mGamertag.c_str(), status.mTransferred);
            } else {
                LOG_INFO(LogAreaID::Network, "[%s] %s: offline (%s), %lld player(s) sent", status.mName.c_str(),
                         status.mGamertag.c_str(), status.mReason.c_str(), status.mTransferred);
            }
        }
    }

    void printFriends(const Broadcaster &broadcaster, int maxFriends) {
        int total = 0;
        for (const AccountStatus &status: broadcaster.getStatus()) {
            if (status.mFriendCount < 0) {
                LOG_INFO(LogAreaID::Network, "[%s] friends not read yet", status.mName.c_str());
                continue;
            }
            total += status.mFriendCount;
            LOG_INFO(LogAreaID::Network, "[%s] %d / %d friends", status.mName.c_str(), status.mFriendCount,
                     maxFriends);
        }
        LOG_INFO(LogAreaID::Network, "%d friends in total", total);
    }

    void runConsole(Broadcaster &broadcaster) {
        std::string line;
        while (!interrupted.load() && std::getline(std::cin, line)) {
            const size_t start = line.find_first_not_of(" \t\r");
            const size_t end = line.find_last_not_of(" \t\r");
            const std::string command = start == std::string::npos ? "" : line.substr(start, end - start + 1);

            if (command.empty())
                continue;
            if (command == "help") {
                LOG_INFO(LogAreaID::Network, "Commands: status, friends, reload, stop");
            } else if (command == "status") {
                printStatus(broadcaster);
            } else if (command == "friends") {
                printFriends(broadcaster, broadcaster.getMaxFriends());
            } else if (command == "reload") {
                std::string error;
                if (broadcaster.reload(error))
                    LOG_INFO(LogAreaID::Network, "Reloaded the settings");
                else
                    LOG_ERROR(LogAreaID::Network, "Could not reload the settings: %s", error.c_str());
            } else if (command == "stop") {
                interrupted.store(true);
                return;
            } else {
                LOG_INFO(LogAreaID::Network, "Unknown command %s, type help", command.c_str());
            }
        }
    }

    std::string configPath(int argc, char **argv) {
        for (int index = 1; index + 1 < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "-config" || argument == "--config")
                return argv[index + 1];
        }
        return DEFAULT_CONFIG_PATH;
    }

}

int main(int argc, char **argv) {
    setUpLogging();

    const std::string path = configPath(argc, argv);
    BroadcasterConfig config;
    bool created = false;
    std::string error;
    if (!BroadcasterConfig::load(path, config, created, error)) {
        LOG_ERROR(LogAreaID::Network, "Could not load %s: %s", path.c_str(), error.c_str());
        BedrockLog::shutdown();
        return 1;
    }
    if (created) {
        LOG_INFO(LogAreaID::Network, "Wrote the default settings to %s, set server.host and start again",
                 path.c_str());
        BedrockLog::shutdown();
        return 0;
    }
    if (!config.validate(error)) {
        LOG_ERROR(LogAreaID::Network, "Invalid %s: %s", path.c_str(), error.c_str());
        BedrockLog::shutdown();
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    Broadcaster broadcaster(path, config);
    std::thread(runConsole, std::ref(broadcaster)).detach();
    std::atomic<bool> finished{false};
    std::thread watcher([&broadcaster, &finished] {
        while (!finished.load()) {
            if (interrupted.load()) {
                LOG_INFO(LogAreaID::Network, "Stopping");
                broadcaster.stop();
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(SIGNAL_POLL_MS));
        }
    });

    const bool ran = broadcaster.run(error);
    finished.store(true);
    watcher.join();
    if (!ran)
        LOG_ERROR(LogAreaID::Network, "Could not sign in: %s", error.c_str());

    BedrockLog::shutdown();
    return ran ? 0 : 1;
}
