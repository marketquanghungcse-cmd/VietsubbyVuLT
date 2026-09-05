#include "Config.h"
#include <fstream>
#include <iostream>

namespace VideoDubber {

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() {
    if (!load()) {
        config_.gemini_api_keys.push_back("");
        config_.deepseek_api_keys.push_back("");
        save();
    }
}

bool ConfigManager::load(const std::string& filepath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ifstream file(filepath);
    if (!file.is_open()) return false;
    try {
        nlohmann::json j;
        file >> j;
        if (j.contains("default_model")) config_.default_model = j["default_model"].get<std::string>();
        if (j.contains("gemini_api_keys")) config_.gemini_api_keys = j["gemini_api_keys"].get<std::vector<std::string>>();
        if (j.contains("deepseek_api_keys")) config_.deepseek_api_keys = j["deepseek_api_keys"].get<std::vector<std::string>>();
        if (j.contains("gemini_model_name")) config_.gemini_model_name = j["gemini_model_name"].get<std::string>();
        if (j.contains("deepseek_model_name")) config_.deepseek_model_name = j["deepseek_model_name"].get<std::string>();
        if (j.contains("source_language")) config_.source_language = j["source_language"].get<std::string>();
        if (j.contains("target_language")) config_.target_language = j["target_language"].get<std::string>();
        if (j.contains("whisper_model_path")) config_.whisper_model_path = j["whisper_model_path"].get<std::string>();
        if (j.contains("whisper_threads")) config_.whisper_threads = j["whisper_threads"].get<int>();
        if (j.contains("tts_engine")) config_.tts_engine = j["tts_engine"].get<std::string>();
        if (j.contains("tts_voice")) config_.tts_voice = j["tts_voice"].get<std::string>();
        if (j.contains("tts_speed_min")) config_.tts_speed_min = j["tts_speed_min"].get<double>();
        if (j.contains("tts_speed_max")) config_.tts_speed_max = j["tts_speed_max"].get<double>();
        if (j.contains("vibi_api_key")) config_.vibi_api_key = j["vibi_api_key"].get<std::string>();
        if (j.contains("vibi_default_voice_id")) config_.vibi_default_voice_id = j["vibi_default_voice_id"].get<std::string>();
        if (j.contains("vibi_provider")) config_.vibi_provider = j["vibi_provider"].get<std::string>();
        if (j.contains("vibi_model_id")) config_.vibi_model_id = j["vibi_model_id"].get<std::string>();
        if (j.contains("bgm_mode")) config_.bgm_mode = j["bgm_mode"].get<std::string>();
        if (j.contains("bgm_volume")) config_.bgm_volume = j["bgm_volume"].get<double>();
        if (j.contains("voice_volume")) config_.voice_volume = j["voice_volume"].get<double>();
        if (j.contains("blur_mode")) config_.blur_mode = j["blur_mode"].get<std::string>();
        if (j.contains("blur_sub_mode")) config_.blur_sub_mode = j["blur_sub_mode"].get<std::string>();
        if (j.contains("blur_kernel_size")) config_.blur_kernel_size = j["blur_kernel_size"].get<int>();
        if (j.contains("blur_bottom_ratio")) config_.blur_bottom_ratio = j["blur_bottom_ratio"].get<double>();
        if (j.contains("pause_after_download")) config_.pause_after_download = j["pause_after_download"].get<bool>();
        if (j.contains("pause_after_stt")) config_.pause_after_stt = j["pause_after_stt"].get<bool>();
        if (j.contains("pause_after_translate")) config_.pause_after_translate = j["pause_after_translate"].get<bool>();
        if (j.contains("output_dir")) config_.output_dir = j["output_dir"].get<std::string>();
        if (j.contains("output_naming_pattern")) config_.output_naming_pattern = j["output_naming_pattern"].get<std::string>();
        if (j.contains("custom_output_prefix")) config_.custom_output_prefix = j["custom_output_prefix"].get<std::string>();
        if (j.contains("server_port")) config_.server_port = j["server_port"].get<int>();
        if (j.contains("max_concurrent_tasks")) config_.max_concurrent_tasks = j["max_concurrent_tasks"].get<int>();
        if (j.contains("ffmpeg_threads")) config_.ffmpeg_threads = j["ffmpeg_threads"].get<int>();
        if (j.contains("yt_dlp_concurrent_fragments")) config_.yt_dlp_concurrent_fragments = j["yt_dlp_concurrent_fragments"].get<int>();
        if (j.contains("translation_chunk_size")) config_.translation_chunk_size = j["translation_chunk_size"].get<int>();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Config load error: " << e.what() << std::endl;
        return false;
    }
}

bool ConfigManager::save(const std::string& filepath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    try {
        nlohmann::json j;
        j["default_model"] = config_.default_model;
        j["gemini_api_keys"] = config_.gemini_api_keys;
        j["deepseek_api_keys"] = config_.deepseek_api_keys;
        j["gemini_model_name"] = config_.gemini_model_name;
        j["deepseek_model_name"] = config_.deepseek_model_name;
        j["source_language"] = config_.source_language;
        j["target_language"] = config_.target_language;
        j["whisper_model_path"] = config_.whisper_model_path;
        j["whisper_threads"] = config_.whisper_threads;
        j["tts_engine"] = config_.tts_engine;
        j["tts_voice"] = config_.tts_voice;
        j["tts_speed_min"] = config_.tts_speed_min;
        j["tts_speed_max"] = config_.tts_speed_max;
        j["vibi_api_key"] = config_.vibi_api_key;
        j["vibi_default_voice_id"] = config_.vibi_default_voice_id;
        j["vibi_provider"] = config_.vibi_provider;
        j["vibi_model_id"] = config_.vibi_model_id;
        j["bgm_mode"] = config_.bgm_mode;
        j["bgm_volume"] = config_.bgm_volume;
        j["voice_volume"] = config_.voice_volume;
        j["blur_mode"] = config_.blur_mode;
        j["blur_sub_mode"] = config_.blur_sub_mode;
        j["blur_kernel_size"] = config_.blur_kernel_size;
        j["blur_bottom_ratio"] = config_.blur_bottom_ratio;
        j["pause_after_download"] = config_.pause_after_download;
        j["pause_after_stt"] = config_.pause_after_stt;
        j["pause_after_translate"] = config_.pause_after_translate;
        j["output_dir"] = config_.output_dir;
        j["output_naming_pattern"] = config_.output_naming_pattern;
        j["custom_output_prefix"] = config_.custom_output_prefix;
        j["server_port"] = config_.server_port;
        j["max_concurrent_tasks"] = config_.max_concurrent_tasks;
        j["ffmpeg_threads"] = config_.ffmpeg_threads;
        j["yt_dlp_concurrent_fragments"] = config_.yt_dlp_concurrent_fragments;
        j["translation_chunk_size"] = config_.translation_chunk_size;

        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Config save error: " << e.what() << std::endl;
        return false;
    }
}

AppConfig ConfigManager::getConfig() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return config_;
}

void ConfigManager::setConfig(const AppConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    config_ = config;
    save();
}

void ConfigManager::addGeminiKey(const std::string& key) {
    if (key.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& k : config_.gemini_api_keys) {
        if (k == key) return;
    }
    config_.gemini_api_keys.push_back(key);
    save();
}

void ConfigManager::addDeepSeekKey(const std::string& key) {
    if (key.empty()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& k : config_.deepseek_api_keys) {
        if (k == key) return;
    }
    config_.deepseek_api_keys.push_back(key);
    save();
}

void ConfigManager::removeGeminiKey(const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& vec = config_.gemini_api_keys;
    vec.erase(std::remove(vec.begin(), vec.end(), key), vec.end());
    save();
}

void ConfigManager::removeDeepSeekKey(const std::string& key) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& vec = config_.deepseek_api_keys;
    vec.erase(std::remove(vec.begin(), vec.end(), key), vec.end());
    save();
}

} // namespace VideoDubber