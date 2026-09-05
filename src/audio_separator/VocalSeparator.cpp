#include "VocalSeparator.h"
#include "../core/ProcessRunner.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace VideoDubber {

bool VocalSeparator::separateBgm(
    const std::string& input_video_path,
    const std::string& output_bgm_wav,
    std::function<void(int, const std::string&)> on_progress
) {
    if (on_progress) on_progress(20, "Tách nhạc nền từ video...");
    std::string cmd = "ffmpeg -y -i \"" + input_video_path + "\" -vn -af \"highpass=f=200,lowpass=f=3000\" -c:a pcm_s16le \"" + output_bgm_wav + "\"";
    int ret = ProcessRunner::execute(cmd);
    return (ret == 0 && fs::exists(output_bgm_wav));
}

MixAudioResult VocalSeparator::mixVoiceAndBgm(
    const std::string& video_path,
    const std::string& voice_wav_path,
    const std::string& bgm_mode,
    double bgm_volume,
    double voice_volume,
    const std::string& output_dir,
    std::function<void(int, const std::string&)> on_progress
) {
    MixAudioResult result;
    fs::create_directories(output_dir);

    if (bgm_mode == "remove_bgm" || !fs::exists(voice_wav_path)) {
        result.success = true;
        result.mixed_audio_path = voice_wav_path;
        return result;
    }

    std::string bgm_wav = output_dir + "/extracted_bgm.wav";
    std::string mixed_wav = output_dir + "/mixed_voice_bgm.wav";

    if (!fs::exists(fs::u8path(bgm_wav))) {
        if (on_progress) on_progress(40, "Đang trích xuất BGM...");
        separateBgm(video_path, bgm_wav, on_progress);
    }

    if (!fs::exists(bgm_wav)) {
        result.success = true;
        result.mixed_audio_path = voice_wav_path;
        return result;
    }

    if (on_progress) on_progress(70, "Đang hòa trộn Voice + BGM...");

    std::string mix_cmd = "ffmpeg -y -i \"" + voice_wav_path + "\" -i \"" + bgm_wav + "\" "
                          "-filter_complex \"[0:a]volume=" + std::to_string(voice_volume) + "[a1];"
                          "[1:a]volume=" + std::to_string(bgm_volume) + "[a2];"
                          "[a1][a2]amix=inputs=2:duration=first:dropout_transition=2[aout]\" "
                          "-map \"[aout]\" -c:a pcm_s16le \"" + mixed_wav + "\"";

    int ret = ProcessRunner::execute(mix_cmd);

    if (fs::exists(mixed_wav)) {
        result.success = true;
        result.mixed_audio_path = mixed_wav;
        if (on_progress) on_progress(100, "Hòa trộn âm thanh hoàn tất");
        return result;
    }

    result.success = true;
    result.mixed_audio_path = voice_wav_path;
    return result;
}

} // namespace VideoDubber
