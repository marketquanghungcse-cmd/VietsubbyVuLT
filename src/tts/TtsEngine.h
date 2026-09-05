#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../stt/SrtParser.h"

namespace VideoDubber {

struct TtsResult {
    bool success = false;
    std::string final_audio_path;
    std::string error;
};

class TtsEngine {
public:
    static TtsResult generateAndSyncVoice(
        const std::vector<SrtItem>& items,
        const std::string& tts_engine = "edge",
        const std::string& voice_name = "vi-VN-HoaiMyNeural",
        const std::string& output_dir = "temp",
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr
    );

    static double getAudioDurationSec(const std::string& audio_file);
    static bool generateSingleTts(const std::string& text, const std::string& out_path, const std::string& engine, const std::string& voice);
    static bool timeStretchAudio(const std::string& in_wav, const std::string& out_wav, double speed_ratio);
};

} // namespace VideoDubber