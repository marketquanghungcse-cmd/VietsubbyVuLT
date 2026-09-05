#pragma once
#include <string>
#include <vector>
#include <functional>
#include "../stt/SrtParser.h"

namespace VideoDubber {

struct RenderResult {
    bool success = false;
    std::string output_video_path;
    std::string output_srt_path;
    std::string error;
};

class SubBlurrer {
public:
    static RenderResult renderFinalVideo(
        const std::string& input_video_path,
        const std::string& mixed_audio_path,
        const std::string& translated_srt_path,
        const std::vector<SrtItem>& subtitle_items = {},
        const std::string& blur_sub_mode = "auto",
        const std::string& output_dir = "output",
        double blur_bottom_ratio = 0.18,
        int blur_kernel = 25,
        int threads = 0,
        const std::string& temp_dir = "temp",
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr,
        const std::string& desired_output_filename = ""
    );
};

} // namespace VideoDubber