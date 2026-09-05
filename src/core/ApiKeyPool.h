#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <chrono>
#include "../../include/json.hpp"

namespace VideoDubber {

struct KeyInfo {
    std::string key;
    std::string label;
    bool enabled = true;
    int success_count = 0;
    int error_count = 0;
    std::string status = "Hoạt động";
    std::chrono::steady_clock::time_point cooldown_until;
};

class ApiKeyPool {
public:
    static ApiKeyPool& instance();

    void loadKeysFromConfig();
    std::string getNextKey(const std::string& provider);
    void reportSuccess(const std::string& provider, const std::string& key);
    void reportError(const std::string& provider, const std::string& key, int http_status);

    bool addKey(const std::string& provider, const std::string& key, const std::string& label = "");
    bool deleteKey(const std::string& provider, const std::string& key);
    bool toggleKey(const std::string& provider, const std::string& key, bool enabled);
    
    nlohmann::json getKeysJson();
    bool testKey(const std::string& provider, const std::string& key, std::string& out_msg);

    size_t getKeyCount(const std::string& provider);

private:
    ApiKeyPool() = default;
    std::recursive_mutex mutex_;
    std::vector<KeyInfo> gemini_keys_;
    std::vector<KeyInfo> deepseek_keys_;
    size_t gemini_idx_ = 0;
    size_t deepseek_idx_ = 0;

    void syncToConfig();
};

} // namespace VideoDubber
