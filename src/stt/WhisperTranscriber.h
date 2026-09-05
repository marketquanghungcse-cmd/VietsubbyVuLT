#pragma once
#include <string>
#include <vector>
#include <functional>
#include "SrtParser.h"

namespace VideoDubber {

struct TranscribeResult {
    bool success = false;
    std::string srt_path;
    std::vector<SrtItem> items;
    std::string error;
};

class WhisperTranscriber {
public:
    static TranscribeResult transcribeVideo(
        const std::string& video_path,
        const std::string& model_path = "models/ggml-base.bin",
        const std::string& language = "zh",
        int threads = 4,
        const std::string& output_dir = "temp",
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr
    );

    static bool extractAudioWav(
        const std::string& video_path, 
        const std::string& output_wav_path,
        std::function<void(int, const std::string&)> on_progress = nullptr
    );
};

} // namespace VideoDubber