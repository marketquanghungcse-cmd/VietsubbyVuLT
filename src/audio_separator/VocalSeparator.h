#pragma once
#include <string>
#include <functional>

namespace VideoDubber {

struct MixAudioResult {
    bool success = false;
    std::string mixed_audio_path;
    std::string error;
};

class VocalSeparator {
public:
    static bool separateBgm(
        const std::string& input_video_path,
        const std::string& output_bgm_wav,
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr
    );

    static MixAudioResult mixVoiceAndBgm(
        const std::string& video_path,
        const std::string& voice_wav_path,
        const std::string& bgm_mode,
        double bgm_volume = 0.75,
        double voice_volume = 1.0,
        const std::string& output_dir = "temp",
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr
    );
};

} // namespace VideoDubber