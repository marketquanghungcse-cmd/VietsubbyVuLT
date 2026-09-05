#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../../include/json.hpp"

namespace VideoDubber {

struct SrtItem {
    int id = 0;
    std::string start_time;
    std::string end_time;
    int64_t start_ms = 0;
    int64_t end_ms = 0;
    std::string text;
    std::string translated_text;
    std::string speaker = "female";

    int64_t getDurationMs() const {
        return end_ms > start_ms ? (end_ms - start_ms) : 0;
    }
};

class SrtParser {
public:
    static std::vector<SrtItem> parseFile(const std::string& filepath);
    static std::vector<SrtItem> parseString(const std::string& content);
    static bool saveToFile(const std::string& filepath, const std::vector<SrtItem>& items, bool use_translated = false);
    static std::string serialize(const std::vector<SrtItem>& items, bool use_translated = false);
    static int64_t timeToMs(const std::string& timeStr);
    static std::string msToTime(int64_t ms);
    static nlohmann::json toJson(const std::vector<SrtItem>& items);
    static std::vector<SrtItem> fromJson(const nlohmann::json& j);
};

} // namespace VideoDubber