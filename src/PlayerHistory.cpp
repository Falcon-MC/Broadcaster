#include "Broadcaster/PlayerHistory.h"

#include "Core/Debug/BedrockLog.h"
#include "Core/Json/Json.h"
#include "Network/JsonText.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace {

    int64_t nowMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
    }

}

PlayerHistory::PlayerHistory(std::string path) : mPath(std::move(path)) {
}

void PlayerHistory::load() {
    std::lock_guard<std::mutex> guard(mMutex);
    mLastSeen.clear();

    std::ifstream input(mPath, std::ios::binary);
    if (!input)
        return;

    std::stringstream text;
    text << input.rdbuf();
    const std::unique_ptr<json::Value> root = json::parse(text.str());
    if (root == nullptr || !root->isObject()) {
        LOG_WARN(LogAreaID::Network, "Ignoring the unreadable player history in %s", mPath.c_str());
        return;
    }

    for (const std::string &xuid: root->mKeys) {
        const json::Value *value = root->get(xuid);
        if (value != nullptr && value->isNumber())
            mLastSeen[xuid] = (int64_t) value->number();
    }
}

void PlayerHistory::recordSeen(const std::string &xuid) {
    std::lock_guard<std::mutex> guard(mMutex);
    mLastSeen[xuid] = nowMillis();
    _save();
}

int64_t PlayerHistory::lastSeen(const std::string &xuid) const {
    std::lock_guard<std::mutex> guard(mMutex);
    const auto found = mLastSeen.find(xuid);
    return found == mLastSeen.end() ? 0 : found->second;
}

void PlayerHistory::recordIfUnknown(const std::string &xuid) {
    std::lock_guard<std::mutex> guard(mMutex);
    if (mLastSeen.emplace(xuid, nowMillis()).second)
        _save();
}

void PlayerHistory::forget(const std::string &xuid) {
    std::lock_guard<std::mutex> guard(mMutex);
    if (mLastSeen.erase(xuid) != 0)
        _save();
}

void PlayerHistory::_save() const {
    std::error_code ignored;
    const std::filesystem::path parent = std::filesystem::path(mPath).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ignored);

    std::ostringstream json;
    json << "{";
    bool first = true;
    for (const auto &entry: mLastSeen) {
        json << (first ? "\n  " : ",\n  ") << JsonText::quote(entry.first) << ": " << entry.second;
        first = false;
    }
    json << "\n}\n";

    const std::string temporary = mPath + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            LOG_WARN(LogAreaID::Network, "Could not write the player history to %s", mPath.c_str());
            return;
        }
        output << json.str();
    }
    std::filesystem::rename(temporary, mPath, ignored);
}
