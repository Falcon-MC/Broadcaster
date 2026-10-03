#include "Broadcaster/BroadcasterConfig.h"

#include "Core/Json/Json.h"
#include "Network/JsonText.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace {

    const char *const EXAMPLE_HOST = "play.example.net";

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

    std::string boolean(bool value) {
        return value ? "true" : "false";
    }

}

bool BroadcasterConfig::load(const std::string &path, BroadcasterConfig &outConfig, bool &outCreated,
                             std::string &outError) {
    outCreated = false;
    outConfig = BroadcasterConfig();

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        outConfig.mServerHost = EXAMPLE_HOST;
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
    outConfig.mServerPort = (unsigned short) port;

    const json::Value *session = section(*root, "session");
    readString(session, "hostName", outConfig.mHostName);
    readString(session, "worldName", outConfig.mWorldName);
    readInt(session, "maxPlayers", outConfig.mMaxPlayers);
    readBool(session, "queryServer", outConfig.mQueryServer);
    readInt(session, "updateInterval", outConfig.mUpdateIntervalSeconds);
    std::string signaling = outConfig.mWebSocketSignaling ? "websocket" : "jsonrpc";
    readString(session, "signaling", signaling);
    outConfig.mWebSocketSignaling = signaling != "jsonrpc";

    const json::Value *friends = section(*root, "friendSync");
    readBool(friends, "enabled", outConfig.mFriendSync);
    readInt(friends, "updateInterval", outConfig.mFriendSyncIntervalSeconds);
    readBool(friends, "acceptRequests", outConfig.mAcceptFriendRequests);
    readBool(friends, "removeInactive", outConfig.mRemoveInactiveFriends);
    readInt(friends, "inactiveDays", outConfig.mInactiveDays);

    const json::Value *cache = section(*root, "cache");
    readString(cache, "token", outConfig.mTokenCachePath);
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
    if (mServerPort == 0) {
        outError = "server.port must be between 1 and 65535";
        return false;
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
    return true;
}

std::string BroadcasterConfig::toJson() const {
    std::ostringstream json;
    json << "{\n"
         << "  \"server\": {\n"
         << "    \"host\": " << JsonText::quote(mServerHost) << ",\n"
         << "    \"port\": " << mServerPort << "\n"
         << "  },\n"
         << "  \"session\": {\n"
         << "    \"hostName\": " << JsonText::quote(mHostName) << ",\n"
         << "    \"worldName\": " << JsonText::quote(mWorldName) << ",\n"
         << "    \"maxPlayers\": " << mMaxPlayers << ",\n"
         << "    \"queryServer\": " << boolean(mQueryServer) << ",\n"
         << "    \"updateInterval\": " << mUpdateIntervalSeconds << ",\n"
         << "    \"signaling\": " << (mWebSocketSignaling ? "\"websocket\"" : "\"jsonrpc\"") << "\n"
         << "  },\n"
         << "  \"friendSync\": {\n"
         << "    \"enabled\": " << boolean(mFriendSync) << ",\n"
         << "    \"updateInterval\": " << mFriendSyncIntervalSeconds << ",\n"
         << "    \"acceptRequests\": " << boolean(mAcceptFriendRequests) << ",\n"
         << "    \"removeInactive\": " << boolean(mRemoveInactiveFriends) << ",\n"
         << "    \"inactiveDays\": " << mInactiveDays << "\n"
         << "  },\n"
         << "  \"cache\": {\n"
         << "    \"token\": " << JsonText::quote(mTokenCachePath) << ",\n"
         << "    \"playerHistory\": " << JsonText::quote(mPlayerHistoryPath) << "\n"
         << "  }\n"
         << "}\n";
    return json.str();
}
