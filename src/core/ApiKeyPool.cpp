#include "ApiKeyPool.h"
#include "Config.h"
#include "HttpClient.h"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace VideoDubber {

ApiKeyPool& ApiKeyPool::instance() {
    static ApiKeyPool inst;
    return inst;
}

void ApiKeyPool::loadKeysFromConfig() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto cfg = ConfigManager::instance().getConfig();

    gemini_keys_.clear();
    int g_idx = 1;
    for (const auto& k : cfg.gemini_api_keys) {
        if (!k.empty()) {
            gemini_keys_.push_back({k, "Gemini Key #" + std::to_string(g_idx++), true, 0, 0, "Sẵn sàng", std::chrono::steady_clock::now()});
        }
    }

    deepseek_keys_.clear();
    int d_idx = 1;
    for (const auto& k : cfg.deepseek_api_keys) {
        if (!k.empty()) {
            deepseek_keys_.push_back({k, "DeepSeek Key #" + std::to_string(d_idx++), true, 0, 0, "Sẵn sàng", std::chrono::steady_clock::now()});
        }
    }
    gemini_idx_ = 0;
    deepseek_idx_ = 0;
}

void ApiKeyPool::syncToConfig() {
    auto cfg = ConfigManager::instance().getConfig();
    cfg.gemini_api_keys.clear();
    for (const auto& k : gemini_keys_) {
        if (k.enabled && !k.key.empty()) cfg.gemini_api_keys.push_back(k.key);
    }
    cfg.deepseek_api_keys.clear();
    for (const auto& k : deepseek_keys_) {
        if (k.enabled && !k.key.empty()) cfg.deepseek_api_keys.push_back(k.key);
    }
    ConfigManager::instance().setConfig(cfg);
}

std::string ApiKeyPool::getNextKey(const std::string& provider) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& pool = (provider == "gemini") ? gemini_keys_ : deepseek_keys_;
    auto& idx = (provider == "gemini") ? gemini_idx_ : deepseek_idx_;

    if (pool.empty()) return "";

    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < pool.size(); ++i) {
        size_t cur = (idx + i) % pool.size();
        if (pool[cur].enabled && (pool[cur].status == "Hoạt động" || now >= pool[cur].cooldown_until)) {
            pool[cur].status = "Hoạt động";
            idx = (cur + 1) % pool.size();
            return pool[cur].key;
        }
    }
    idx = (idx + 1) % pool.size();
    return pool[idx].key;
}

void ApiKeyPool::reportSuccess(const std::string& provider, const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& pool = (provider == "gemini") ? gemini_keys_ : deepseek_keys_;
    for (auto& item : pool) {
        if (item.key == key) {
            item.success_count++;
            item.status = "Hoạt động";
            break;
        }
    }
}

void ApiKeyPool::reportError(const std::string& provider, const std::string& key, int http_status) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& pool = (provider == "gemini") ? gemini_keys_ : deepseek_keys_;
    for (auto& item : pool) {
        if (item.key == key) {
            item.error_count++;
            if (http_status == 429) {
                item.status = "Đang chờ hồi Quota (429)";
                item.cooldown_until = std::chrono::steady_clock::now() + std::chrono::seconds(35);
            } else {
                item.status = "Lỗi HTTP " + std::to_string(http_status);
            }
            break;
        }
    }
}

bool ApiKeyPool::addKey(const std::string& provider, const std::string& key, const std::string& label) {
    if (key.empty()) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& pool = (provider == "gemini") ? gemini_keys_ : deepseek_keys_;
    for (const auto& it : pool) {
        if (it.key == key) return false;
    }
    std::string l = label.empty() ? ((provider == "gemini" ? "Gemini Key #" : "DeepSeek Key #") + std::to_string(pool.size() + 1)) : label;
    pool.push_back({key, l, true, 0, 0, "Sẵn sàng", std::chrono::steady_clock::now()});
    syncToConfig();
    return true;
}

bool ApiKeyPool::deleteKey(const std::string& provider, const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& pool = (provider == "gemini") ? gemini_keys_ : deepseek_keys_;
    auto it = std::remove_if(pool.begin(), pool.end(), [&](const KeyInfo& ki) { return ki.key == key; });
    if (it != pool.end()) {
        pool.erase(it, pool.end());
        syncToConfig();
        return true;
    }
    return false;
}

bool ApiKeyPool::toggleKey(const std::string& provider, const std::string& key, bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& pool = (provider == "gemini") ? gemini_keys_ : deepseek_keys_;
    for (auto& it : pool) {
        if (it.key == key) {
            it.enabled = enabled;
            it.status = enabled ? "Sẵn sàng" : "Đã tắt";
            syncToConfig();
            return true;
        }
    }
    return false;
}

nlohmann::json ApiKeyPool::getKeysJson() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto formatPool = [](const std::vector<KeyInfo>& pool) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& k : pool) {
            std::string masked = k.key;
            if (masked.length() > 10) {
                masked = masked.substr(0, 6) + "..." + masked.substr(masked.length() - 4);
            }
            arr.push_back({
                {"raw_key", k.key},
                {"masked_key", masked},
                {"label", k.label},
                {"enabled", k.enabled},
                {"success_count", k.success_count},
                {"error_count", k.error_count},
                {"status", k.status}
            });
        }
        return arr;
    };

    return {
        {"gemini_keys", formatPool(gemini_keys_)},
        {"deepseek_keys", formatPool(deepseek_keys_)}
    };
}

bool ApiKeyPool::testKey(const std::string& provider, const std::string& key, std::string& out_msg) {
    auto t_start = std::chrono::steady_clock::now();

    if (provider == "gemini") {
        std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=" + key;
        std::string body = "{\"contents\":[{\"parts\":[{\"text\":\"Xin chao\"}]}]}";
        auto resp = HttpClient::post(url, body, {{"Content-Type", "application/json"}}, 15);
        auto t_end = std::chrono::steady_clock::now();
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

        if (resp.status_code == 200) {
            out_msg = "Kết nối Google Gemini 2.0 Flash thành công (HTTP 200 OK - " + std::to_string(ms) + "ms)! Key hoạt động tốt.";
            reportSuccess("gemini", key);
            return true;
        } else if (resp.status_code == 400 || resp.status_code == 401 || resp.status_code == 403) {
            out_msg = "API Key không hợp lệ hoặc sai quyền truy cập (HTTP " + std::to_string(resp.status_code) + "): " + resp.body;
            reportError("gemini", key, resp.status_code);
            return false;
        } else if (resp.status_code == 429) {
            out_msg = "Key đã chạm giới hạn tần suất (Rate Limit 429 Quota Exceeded).";
            reportError("gemini", key, 429);
            return false;
        } else {
            out_msg = "Lỗi kết nối Gemini (HTTP " + std::to_string(resp.status_code) + "): " + resp.error;
            reportError("gemini", key, resp.status_code);
            return false;
        }
    } else {
        std::string url = "https://api.deepseek.com/chat/completions";
        std::string body = "{\"model\":\"deepseek-chat\",\"messages\":[{\"role\":\"user\",\"content\":\"Hi\"}],\"max_tokens\":5}";
        std::map<std::string, std::string> headers = {
            {"Content-Type", "application/json"},
            {"Authorization", "Bearer " + key}
        };
        auto resp = HttpClient::post(url, body, headers, 15);
        auto t_end = std::chrono::steady_clock::now();
        int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

        if (resp.status_code == 200) {
            out_msg = "Kết nối DeepSeek Chat thành công (HTTP 200 OK - " + std::to_string(ms) + "ms)! Key hoạt động tốt.";
            reportSuccess("deepseek", key);
            return true;
        } else if (resp.status_code == 401 || resp.status_code == 403) {
            out_msg = "API Key DeepSeek không hợp lệ hoặc hết số dư (HTTP " + std::to_string(resp.status_code) + "): " + resp.body;
            reportError("deepseek", key, resp.status_code);
            return false;
        } else if (resp.status_code == 429) {
            out_msg = "Key DeepSeek chạm giới hạn tần suất (Rate Limit 429 Quota Exceeded).";
            reportError("deepseek", key, 429);
            return false;
        } else {
            out_msg = "Lỗi kết nối DeepSeek (HTTP " + std::to_string(resp.status_code) + "): " + resp.error;
            reportError("deepseek", key, resp.status_code);
            return false;
        }
    }
}

size_t ApiKeyPool::getKeyCount(const std::string& provider) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return (provider == "gemini") ? gemini_keys_.size() : deepseek_keys_.size();
}

} // namespace VideoDubber
