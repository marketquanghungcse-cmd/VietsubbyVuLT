#pragma once
#include <string>
#include <vector>
#include <functional>

namespace VideoDubber {

struct DownloadResult {
    bool success = false;
    std::string video_path;
    std::string title;
    int duration_sec = 0;
    std::string error;
};

class VideoDownloader {
public:
    static std::string extractCleanUrl(const std::string& input);
    static std::vector<std::string> parseLinkFile(const std::string& txt_filepath);
    static DownloadResult downloadVideo(
        const std::string& url, 
        const std::string& output_dir = "temp", 
        std::function<void(int progress, const std::string& msg)> on_progress = nullptr
    );
};

} // namespace VideoDubber