#include "Broadcaster/Broadcaster.h"
#include "Broadcaster/BroadcasterConfig.h"

#include "Core/Debug/BedrockLog.h"
#include "Core/Debug/ContentLogEndPoint.h"
#include "Core/Debug/FileLogEndPoint.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
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

    Broadcaster broadcaster(config);
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
