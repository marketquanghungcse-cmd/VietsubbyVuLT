#include "SrtParser.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <algorithm>
#include <filesystem>

namespace VideoDubber {

int64_t SrtParser::timeToMs(const std::string& timeStr) {
    std::string s = timeStr;
    std::replace(s.begin(), s.end(), ',', '.');
    int h = 0, m = 0, sec = 0, ms = 0;
    if (sscanf_s(s.c_str(), "%d:%d:%d.%d", &h, &m, &sec, &ms) >= 3) {
        return (int64_t)h * 3600000 + (int64_t)m * 60000 + (int64_t)sec * 1000 + ms;
    }
    return 0;
}

std::string SrtParser::msToTime(int64_t total_ms) {
    if (total_ms < 0) total_ms = 0;
    int h = (int)(total_ms / 3600000);
    total_ms %= 3600000;
    int m = (int)(total_ms / 60000);
    total_ms %= 60000;
    int s = (int)(total_ms / 1000);
    int ms = (int)(total_ms % 1000);

    char buf[64];
    sprintf_s(buf, "%02d:%02d:%02d,%03d", h, m, s, ms);
    return std::string(buf);
}

std::vector<SrtItem> SrtParser::parseString(const std::string& content) {
    std::vector<SrtItem> result;
    std::istringstream stream(content);
    std::string line;

    enum State { ID, TIME, TEXT };
    State state = ID;
    SrtItem current;
    std::string text_accum;

    std::regex time_regex(R"((\d{1,2}:\d{2}:\d{2}[,\.]\d{1,3})\s*-->\s*(\d{1,2}:\d{2}:\d{2}[,\.]\d{1,3}))");

    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (line.empty()) {
            if (!text_accum.empty()) {
                current.text = text_accum;
                result.push_back(current);
                current = SrtItem();
                text_accum.clear();
            }
            state = ID;
            continue;
        }

        std::smatch match;
        if (std::regex_search(line, match, time_regex)) {
            current.start_time = match[1].str();
            current.end_time = match[2].str();
            current.start_ms = timeToMs(current.start_time);
            current.end_ms = timeToMs(current.end_time);
            state = TEXT;
            text_accum.clear();
        } else if (state == ID && std::all_of(line.begin(), line.end(), ::isdigit)) {
            current.id = std::stoi(line);
            state = TIME;
        } else if (state == TEXT) {
            if (!text_accum.empty()) text_accum += "\n";
            text_accum += line;
        }
    }

    if (!text_accum.empty()) {
        current.text = text_accum;
        result.push_back(current);
    }

    for (size_t i = 0; i < result.size(); ++i) {
        result[i].id = (int)(i + 1);
    }
    return result;
}

std::vector<SrtItem> SrtParser::parseFile(const std::string& filepath) {
    std::ifstream file(std::filesystem::u8path(filepath));
    if (!file.is_open()) return {};
    std::stringstream buf;
    buf << file.rdbuf();
    return parseString(buf.str());
}

std::string SrtParser::serialize(const std::vector<SrtItem>& items, bool use_translated) {
    std::ostringstream ss;
    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        ss << (i + 1) << "\n";
        ss << msToTime(item.start_ms) << " --> " << msToTime(item.end_ms) << "\n";
        std::string txt = (use_translated && !item.translated_text.empty()) ? item.translated_text : item.text;
        ss << txt << "\n\n";
    }
    return ss.str();
}

bool SrtParser::saveToFile(const std::string& filepath, const std::vector<SrtItem>& items, bool use_translated) {
    std::ofstream file(std::filesystem::u8path(filepath));
    if (!file.is_open()) return false;
    file << serialize(items, use_translated);
    return true;
}

nlohmann::json SrtParser::toJson(const std::vector<SrtItem>& items) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& item : items) {
        arr.push_back({
            {"id", item.id},
            {"start_time", msToTime(item.start_ms)},
            {"end_time", msToTime(item.end_ms)},
            {"start_ms", item.start_ms},
            {"end_ms", item.end_ms},
            {"text", item.text},
            {"translated_text", item.translated_text},
            {"speaker", item.speaker}
        });
    }
    return arr;
}

std::vector<SrtItem> SrtParser::fromJson(const nlohmann::json& j) {
    std::vector<SrtItem> res;
    if (!j.is_array()) return res;
    for (const auto& el : j) {
        SrtItem item;
        if (el.contains("id")) item.id = el["id"].get<int>();
        if (el.contains("start_time")) item.start_time = el["start_time"].get<std::string>();
        if (el.contains("end_time")) item.end_time = el["end_time"].get<std::string>();
        if (el.contains("start_ms")) item.start_ms = el["start_ms"].get<int64_t>();
        if (el.contains("end_ms")) item.end_ms = el["end_ms"].get<int64_t>();
        if (el.contains("text")) item.text = el["text"].get<std::string>();
        if (el.contains("translated_text")) item.translated_text = el["translated_text"].get<std::string>();
        if (el.contains("speaker")) item.speaker = el["speaker"].get<std::string>();
        if (item.start_ms == 0 && !item.start_time.empty()) item.start_ms = timeToMs(item.start_time);
        if (item.end_ms == 0 && !item.end_time.empty()) item.end_ms = timeToMs(item.end_time);
        res.push_back(item);
    }
    return res;
}

} // namespace VideoDubber