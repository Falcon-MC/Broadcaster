#include "Broadcaster/BroadcasterConfig.h"

#include "Core/Json/Json.h"
#include "Network/JsonText.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>

namespace {

    const char *const EXAMPLE_HOST = "play.example.net";
    const char *const DEFAULT_ACCOUNT_NAME = "main";
    const char *const DEFAULT_TOKEN_PATH = "cache/token.json";
    const int XBOX_FRIENDS_LIMIT = 1000;

    const json::Value *section(const json::Value &root, const char *name) {
        const json::Value *value = root.get(name);
        return value != nullptr && value->isObject() ? value : nullptr;
    }

    void readString(const json::Value *object, const char *key, std::string &outValue) {
        const json::Value *value = object != nullptr ? object->get(key) : nullptr;
        if (value != nullptr && value->isString())
            outValue = value->string();
    }

    void readInt(const json::Value *object, const char *key, int &outValue) {
        const json::Value *value = object != nullptr ? object->get(key) : nullptr;
        if (value != nullptr && value->isNumber())
            outValue = (int) value->number();
    }

    void readBool(const json::Value *object, const char *key, bool &outValue) {
        const json::Value *value = object != nullptr ? object->get(key) : nullptr;
        if (value != nullptr && value->mType == json::Value::Type::Boolean)
            outValue = value->boolean();
    }

    void readStrings(const json::Value *object, const char *key, std::vector<std::string> &outValues) {
        const json::Value *value = object != nullptr ? object->get(key) : nullptr;
        if (value == nullptr || !value->isArray())
            return;
        for (const std::unique_ptr<json::Value> &entry: value->mArray) {
            if (entry != nullptr && entry->isString())
                outValues.push_back(entry->string());
        }
    }

    std::string boolean(bool value) {
        return value ? "true" : "false";
    }

    std::string lower(std::string text) {
        for (char &character: text)
            character = (char) std::tolower((unsigned char) character);
        return text;
    }

    bool matchesLanguage(const std::string &pattern, const std::string &languageCode) {
        const std::string wanted = lower(pattern);
        const std::string code = lower(languageCode);
        if (!wanted.empty() && wanted.back() == '*')
            return code.compare(0, wanted.size() - 1, wanted, 0, wanted.size() - 1) == 0;
        return code == wanted;
    }

    bool matchesAny(const std::vector<std::string> &patterns, const std::string &value, bool languages) {
        if (patterns.empty())
            return true;
        for (const std::string &pattern: patterns) {
            if (languages ? matchesLanguage(pattern, value) : lower(pattern) == lower(value))
                return true;
        }
        return false;
    }

    std::string quotedList(const std::vector<std::string> &values) {
        std::string text = "[";
        for (size_t index = 0; index < values.size(); ++index)
            text += (index > 0 ? ", " : "") + JsonText::quote(values[index]);
        return text + "]";
    }

    const char *const PLATFORM_NAMES[] = {
            "", "android", "ios", "macos", "fireos", "gearvr", "hololens", "windows", "windows", "dedicated",
            "tvos", "playstation", "switch", "xbox", "windowsphone", "linux"
    };

}

std::string BroadcasterConfig::platformName(int deviceOS) {
    const int count = (int) (sizeof(PLATFORM_NAMES) / sizeof(PLATFORM_NAMES[0]));
    return deviceOS > 0 && deviceOS < count ? PLATFORM_NAMES[deviceOS] : "";
}

const BroadcastRoute *BroadcasterConfig::route(const RoutedPlayer &player) const {
    const std::string platform = platformName(player.mDeviceOS);
    for (const BroadcastRoute &candidate: mRoutes) {
        if (!matchesAny(candidate.mLanguages, player.mLanguageCode, true))
            continue;
        if (!matchesAny(candidate.mPlatforms, platform, false))
            continue;
        if (!candidate.mPlayers.empty() && !matchesAny(candidate.mPlayers, player.mName, false)
            && !matchesAny(candidate.mPlayers, player.mXuid, false))
            continue;
        return &candidate;
    }
    return nullptr;
}

bool BroadcasterConfig::load(const std::string &path, BroadcasterConfig &outConfig, bool &outCreated,
                             std::string &outError) {
    outCreated = false;
    outConfig = BroadcasterConfig();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        outConfig.mServerHost = EXAMPLE_HOST;
        outConfig.mAccounts.push_back({DEFAULT_ACCOUNT_NAME, DEFAULT_TOKEN_PATH});
        std::error_code ignored;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ignored);

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) {
            outError = "could not write " + path;
            return false;
        }
        output << outConfig.toJson();
        outCreated = true;
        return true;
    }

    std::stringstream text;
    text << input.rdbuf();
    const std::unique_ptr<json::Value> root = json::parse(text.str());
    if (root == nullptr || !root->isObject()) {
        outError = path + " is not a valid JSON object";
        return false;
    }

    const json::Value *server = section(*root, "server");
    readString(server, "host", outConfig.mServerHost);
    int port = outConfig.mServerPort;
    readInt(server, "port", port);
    if (port <= 0 || port > 65535) {
        outError = "server.port must be between 1 and 65535";
        return false;
    }
    outConfig.mServerPort = (unsigned short) port;

    const json::Value *routes = root->get("routes");
    if (routes != nullptr && routes->isArray()) {
        for (const std::unique_ptr<json::Value> &entry: routes->mArray) {
            if (entry == nullptr || !entry->isObject())
                continue;
            BroadcastRoute route;
            const json::Value *target = section(*entry, "server");
            readString(target, "host", route.mHost);
            int routePort = route.mPort;
            readInt(target, "port", routePort);
            if (routePort <= 0 || routePort > 65535) {
                outError = "the port of a route must be between 1 and 65535";
                return false;
            }
            route.mPort = (unsigned short) routePort;
            readStrings(entry.get(), "languages", route.mLanguages);
            readStrings(entry.get(), "platforms", route.mPlatforms);
            readStrings(entry.get(), "players", route.mPlayers);
            outConfig.mRoutes.push_back(route);
        }
    }

    const json::Value *session = section(*root, "session");
    readString(session, "hostName", outConfig.mHostName);
    readString(session, "worldName", outConfig.mWorldName);
    readInt(session, "maxPlayers", outConfig.mMaxPlayers);
    readBool(session, "queryServer", outConfig.mQueryServer);
    readInt(session, "updateInterval", outConfig.mUpdateIntervalSeconds);
    std::string signaling = outConfig.mWebSocketSignaling ? "websocket" : "jsonrpc";
    readString(session, "signaling", signaling);
    outConfig.mWebSocketSignaling = signaling != "jsonrpc";

    const json::Value *accounts = root->get("accounts");
    if (accounts != nullptr && accounts->isArray()) {
        outConfig.mAccounts.clear();
        for (const std::unique_ptr<json::Value> &entry: accounts->mArray) {
            if (entry == nullptr || !entry->isObject())
                continue;
            BroadcastAccountConfig account;
            readString(entry.get(), "name", account.mName);
            readString(entry.get(), "token", account.mTokenCachePath);
            outConfig.mAccounts.push_back(account);
        }
    }

    const json::Value *friends = section(*root, "friendSync");
    readBool(friends, "enabled", outConfig.mFriendSync);
    readInt(friends, "updateInterval", outConfig.mFriendSyncIntervalSeconds);
    readBool(friends, "acceptRequests", outConfig.mAcceptFriendRequests);
    readBool(friends, "removeInactive", outConfig.mRemoveInactiveFriends);
    readInt(friends, "inactiveDays", outConfig.mInactiveDays);
    readInt(friends, "maxFriends", outConfig.mMaxFriends);

    const json::Value *cache = section(*root, "cache");
    readString(cache, "playerHistory", outConfig.mPlayerHistoryPath);

    if (signaling != "websocket" && signaling != "jsonrpc") {
        outError = "session.signaling must be \"websocket\" or \"jsonrpc\"";
        return false;
    }
    return true;
}

bool BroadcasterConfig::validate(std::string &outError) const {
    if (mServerHost.empty() || mServerHost == EXAMPLE_HOST) {
        outError = "set server.host to the address players are transferred to";
        return false;
    }
    for (const BroadcastRoute &route: mRoutes) {
        if (route.mHost.empty()) {
            outError = "every route needs a server.host";
            return false;
        }
        for (const std::string &platform: route.mPlatforms) {
            bool known = false;
            for (const char *name: PLATFORM_NAMES)
                known = known || (*name != '\0' && lower(platform) == name);
            if (!known) {
                outError = "unknown platform " + platform + " in a route";
                return false;
            }
        }
    }
    if (mMaxPlayers <= 0) {
        outError = "session.maxPlayers must be positive";
        return false;
    }
    if (mUpdateIntervalSeconds < 5) {
        outError = "session.updateInterval must be at least 5 seconds";
        return false;
    }
    if (mFriendSync && mFriendSyncIntervalSeconds < 20) {
        outError = "friendSync.updateInterval must be at least 20 seconds";
        return false;
    }
    if (mRemoveInactiveFriends && mInactiveDays <= 0) {
        outError = "friendSync.inactiveDays must be positive";
        return false;
    }
    if (mMaxFriends <= 0 || mMaxFriends > XBOX_FRIENDS_LIMIT) {
        outError = "friendSync.maxFriends must be between 1 and " + std::to_string(XBOX_FRIENDS_LIMIT);
        return false;
    }
    if (mAccounts.empty()) {
        outError = "add at least one entry to accounts";
        return false;
    }
    std::set<std::string> names;
    std::set<std::string> tokens;
    for (const BroadcastAccountConfig &account: mAccounts) {
        if (account.mName.empty() || account.mTokenCachePath.empty()) {
            outError = "every entry of accounts needs a name and a token path";
            return false;
        }
        if (!names.insert(account.mName).second) {
            outError = "the account name " + account.mName + " is used twice";
            return false;
        }
        if (!tokens.insert(account.mTokenCachePath).second) {
            outError = "the token path " + account.mTokenCachePath + " is used by two accounts";
            return false;
        }
    }
    return true;
}

std::string BroadcasterConfig::toJson() const {
    std::ostringstream json;
    json << "{\n"
         << "  \"server\": {\n"
         << "    \"host\": " << JsonText::quote(mServerHost) << ",\n"
         << "    \"port\": " << mServerPort << "\n"
         << "  },\n"
         << "  \"routes\": [";
    for (size_t index = 0; index < mRoutes.size(); ++index) {
        const BroadcastRoute &route = mRoutes[index];
        json << (index > 0 ? ",\n" : "\n")
             << "    {\n"
             << "      \"server\": { \"host\": " << JsonText::quote(route.mHost) << ", \"port\": " << route.mPort
             << " },\n"
             << "      \"languages\": " << quotedList(route.mLanguages) << ",\n"
             << "      \"platforms\": " << quotedList(route.mPlatforms) << ",\n"
             << "      \"players\": " << quotedList(route.mPlayers) << "\n"
             << "    }";
    }
    json << (mRoutes.empty() ? "],\n" : "\n  ],\n")
         << "  \"session\": {\n"
         << "    \"hostName\": " << JsonText::quote(mHostName) << ",\n"
         << "    \"worldName\": " << JsonText::quote(mWorldName) << ",\n"
         << "    \"maxPlayers\": " << mMaxPlayers << ",\n"
         << "    \"queryServer\": " << boolean(mQueryServer) << ",\n"
         << "    \"updateInterval\": " << mUpdateIntervalSeconds << ",\n"
         << "    \"signaling\": " << (mWebSocketSignaling ? "\"websocket\"" : "\"jsonrpc\"") << "\n"
         << "  },\n"
         << "  \"accounts\": [\n";
    for (size_t index = 0; index < mAccounts.size(); ++index) {
        json << "    { \"name\": " << JsonText::quote(mAccounts[index].mName) << ", \"token\": "
             << JsonText::quote(mAccounts[index].mTokenCachePath) << " }"
             << (index + 1 < mAccounts.size() ? ",\n" : "\n");
    }
    json << "  ],\n"
         << "  \"friendSync\": {\n"
         << "    \"enabled\": " << boolean(mFriendSync) << ",\n"
         << "    \"updateInterval\": " << mFriendSyncIntervalSeconds << ",\n"
         << "    \"acceptRequests\": " << boolean(mAcceptFriendRequests) << ",\n"
         << "    \"removeInactive\": " << boolean(mRemoveInactiveFriends) << ",\n"
         << "    \"inactiveDays\": " << mInactiveDays << ",\n"
         << "    \"maxFriends\": " << mMaxFriends << "\n"
         << "  },\n"
         << "  \"cache\": {\n"
         << "    \"playerHistory\": " << JsonText::quote(mPlayerHistoryPath) << "\n"
         << "  }\n"
         << "}\n";
    return json.str();
}
