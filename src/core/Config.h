#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <algorithm>
#include "../../include/json.hpp"

namespace VideoDubber {

struct AppConfig {
    std::string default_model = "gemini";
    std::vector<std::string> gemini_api_keys;
    std::vector<std::string> deepseek_api_keys;
    std::string gemini_model_name = "gemini-2.0-flash";
    std::string deepseek_model_name = "deepseek-chat";

    std::string source_language = "zh";
    std::string target_language = "vi";

    std::string whisper_model_path = "models/ggml-base.bin";
    int whisper_threads = 4;

    std::string tts_engine = "edge";
    std::string tts_voice = "vi-VN-HoaiMyNeural";
    double tts_speed_min = 0.85;
    double tts_speed_max = 1.40;

    std::string vibi_api_key = "";
    std::string vibi_default_voice_id = "";
    std::string vibi_provider = "minimax";
    std::string vibi_model_id = "speech-2.8-turbo";

    std::string bgm_mode = "keep_bgm";
    double bgm_volume = 0.75;
    double voice_volume = 1.0;

    std::string blur_mode = "auto_ocr";
    std::string blur_sub_mode = "auto_ocr"; // "auto_ocr" (dynamic OCR + sub), "sub_only" (subs only), "bottom_sub", "none"
    int blur_kernel_size = 25;
    double blur_bottom_ratio = 0.18;

    bool pause_after_download = false;
    bool pause_after_stt = false;
    bool pause_after_translate = false;

    std::string output_dir = "output";
    std::string temp_dir = "temp";
    std::string output_naming_pattern = "seq_title"; // "seq_title", "id_title", "custom_seq", "original_dubbed"
    std::string custom_output_prefix = "";
    int server_port = 8765;

    // Multi-threading config (Group A, B, D)
    int max_concurrent_tasks = 2;
    int ffmpeg_threads = 0;
    int yt_dlp_concurrent_fragments = 4;
    int translation_chunk_size = 40;
};

class ConfigManager {
public:
    static ConfigManager& instance();

    bool load(const std::string& filepath = "config.json");
    bool save(const std::string& filepath = "config.json");

    AppConfig getConfig();
    void setConfig(const AppConfig& config);

    void addGeminiKey(const std::string& key);
    void addDeepSeekKey(const std::string& key);
    void removeGeminiKey(const std::string& key);
    void removeDeepSeekKey(const std::string& key);

private:
    ConfigManager();
    AppConfig config_;
    std::recursive_mutex mutex_;
};

} // namespace VideoDubber