#include "Broadcaster/Broadcaster.h"

#include "Core/Debug/BedrockLog.h"
#include "Network/ServerQuery.h"

#include <chrono>
#include <thread>
#include <utility>

namespace {

    const int WAIT_STEP_MS = 200;

    bool sameSession(const BroadcasterConfig &left, const BroadcasterConfig &right) {
        return left.mMaxPlayers == right.mMaxPlayers && left.mWebSocketSignaling == right.mWebSocketSignaling;
    }

}

Broadcaster::Broadcaster(std::string configPath, BroadcasterConfig config)
        : mConfigPath(std::move(configPath)), mConfigs(config), mHistory(config.mPlayerHistoryPath) {
}

Broadcaster::~Broadcaster() {
    stop();
    std::lock_guard<std::mutex> guard(mAccountsMutex);
    mAccounts.clear();
}

void Broadcaster::stop() {
    mStopping.store(true);
    std::lock_guard<std::mutex> guard(mAccountsMutex);
    for (std::unique_ptr<BroadcastAccount> &account: mAccounts)
        account->stop();
}

bool Broadcaster::run(std::string &outError) {
    mHistory.load();

    const std::shared_ptr<const BroadcasterConfig> config = mConfigs.get();
    for (const BroadcastAccountConfig &accountConfig: config->mAccounts) {
        if (mStopping.load())
            return true;

        std::unique_ptr<BroadcastAccount> account(new BroadcastAccount(accountConfig, mConfigs, mHistory, mStopping));
        std::string error;
        if (!account->signIn(error)) {
            LOG_WARN(LogAreaID::Network, "[%s] Could not sign in, skipping this account: %s",
                     accountConfig.mName.c_str(), error.c_str());
            continue;
        }
        std::lock_guard<std::mutex> guard(mAccountsMutex);
        mAccounts.push_back(std::move(account));
    }

    std::vector<BroadcastAccount *> accounts;
    {
        std::lock_guard<std::mutex> guard(mAccountsMutex);
        for (std::unique_ptr<BroadcastAccount> &account: mAccounts)
            accounts.push_back(account.get());
    }
    if (accounts.empty()) {
        outError = "no account could sign in";
        return false;
    }

    std::vector<std::thread> threads;
    for (BroadcastAccount *account: accounts)
        threads.emplace_back(&BroadcastAccount::run, account);

    _advertise();

    for (std::thread &thread: threads)
        thread.join();
    return true;
}

bool Broadcaster::reload(std::string &outError) {
    BroadcasterConfig next;
    bool created = false;
    if (!BroadcasterConfig::load(mConfigPath, next, created, outError))
        return false;
    if (created) {
        outError = mConfigPath + " was missing, the defaults were written to it";
        return false;
    }
    if (!next.validate(outError))
        return false;

    const std::shared_ptr<const BroadcasterConfig> previous = mConfigs.get();
    if (next.mAccounts != previous->mAccounts)
        LOG_WARN(LogAreaID::Network, "Changes to accounts apply after a restart");
    if (next.mPlayerHistoryPath != previous->mPlayerHistoryPath)
        LOG_WARN(LogAreaID::Network, "Changes to cache.playerHistory apply after a restart");
    next.mAccounts = previous->mAccounts;
    next.mPlayerHistoryPath = previous->mPlayerHistoryPath;

    const bool republish = !sameSession(*previous, next);
    mConfigs.set(std::move(next));
    mAdvertiseNow.store(true);

    if (republish) {
        std::lock_guard<std::mutex> guard(mAccountsMutex);
        for (std::unique_ptr<BroadcastAccount> &account: mAccounts)
            account->reconnect();
    }
    return true;
}

std::vector<AccountStatus> Broadcaster::getStatus() const {
    std::vector<AccountStatus> statuses;
    std::lock_guard<std::mutex> guard(mAccountsMutex);
    for (const std::unique_ptr<BroadcastAccount> &account: mAccounts)
        statuses.push_back(account->getStatus());
    return statuses;
}

void Broadcaster::_advertise() {
    while (!mStopping.load()) {
        const std::shared_ptr<const BroadcasterConfig> config = mConfigs.get();
        const Advertisement advertisement = _queryAdvertisement(*config);
        {
            std::lock_guard<std::mutex> guard(mAccountsMutex);
            for (std::unique_ptr<BroadcastAccount> &account: mAccounts)
                account->advertise(advertisement);
        }
        _wait(config->mUpdateIntervalSeconds);
    }
}

Advertisement Broadcaster::_queryAdvertisement(const BroadcasterConfig &config) const {
    Advertisement advertisement;
    advertisement.mServerName = config.mWorldName.empty()
                                ? (config.mHostName.empty() ? config.mServerHost : config.mHostName)
                                : config.mWorldName;
    advertisement.mSubName = config.mHostName.empty() ? config.mServerHost : config.mHostName;
    advertisement.mMaxPlayers = config.mMaxPlayers;

    if (!config.mQueryServer)
        return advertisement;

    ServerQueryResult result;
    std::string error;
    if (!ServerQuery::query(config.mServerHost, config.mServerPort, result, error)) {
        LOG_WARN(LogAreaID::Network, "Could not query %s:%u: %s", config.mServerHost.c_str(),
                 (unsigned int) config.mServerPort, error.c_str());
        return advertisement;
    }

    if (config.mHostName.empty() && !result.mMotd.empty())
        advertisement.mSubName = result.mMotd;
    if (config.mWorldName.empty() && !result.mSubMotd.empty())
        advertisement.mServerName = result.mSubMotd;
    advertisement.mPlayerCount = result.mPlayerCount;
    if (result.mMaxPlayers > 0)
        advertisement.mMaxPlayers = result.mMaxPlayers;
    return advertisement;
}

void Broadcaster::_wait(int seconds) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!mStopping.load() && !mAdvertiseNow.exchange(false) && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_STEP_MS));
}
