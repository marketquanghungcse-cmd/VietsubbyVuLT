#include "WhisperTranscriber.h"
#include "../core/ProcessRunner.h"
#include <iostream>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace VideoDubber {

bool WhisperTranscriber::extractAudioWav(
    const std::string& video_path, 
    const std::string& output_wav_path,
    std::function<void(int, const std::string&)> on_progress
) {
    if (on_progress) on_progress(20, "Tách audio chuẩn 16kHz mono WAV...");
    std::string cmd = "ffmpeg -y -i \"" + video_path + "\" -vn -ar 16000 -ac 1 -c:a pcm_s16le \"" + output_wav_path + "\"";
    int ret = ProcessRunner::execute(cmd);
    return (ret == 0 && fs::exists(output_wav_path));
}

TranscribeResult WhisperTranscriber::transcribeVideo(
    const std::string& video_path,
    const std::string& model_path,
    const std::string& language,
    int threads,
    const std::string& output_dir,
    std::function<void(int progress, const std::string& msg)> on_progress
) {
    TranscribeResult result;
    fs::create_directories(output_dir);

    std::string wav_path = output_dir + "/audio_16k.wav";
    if (!extractAudioWav(video_path, wav_path, on_progress)) {
        result.error = "Khong the trich xuat audio tu video: " + video_path;
        return result;
    }

    if (on_progress) on_progress(30, "Đang bóc tách sub bằng Whisper.cpp (" + language + ")...");

    std::string actual_model = model_path;
    if (actual_model.empty() || actual_model == "models/ggml-base.bin") {
        if (fs::exists("models/ggml-small.bin")) {
            actual_model = "models/ggml-small.bin";
        } else {
            actual_model = "models/ggml-base.bin";
        }
    }

    std::string whisper_bin = "bin/whisper-cli.exe";
    std::string out_prefix = output_dir + "/whisper_out";
    std::string srt_path = out_prefix + ".srt";

    if (fs::exists(srt_path)) {
        fs::remove(srt_path);
    }

    std::string abs_bin = fs::absolute(whisper_bin).string();
    std::string abs_model = fs::absolute(actual_model).string();
    std::string abs_wav = fs::absolute(wav_path).string();
    std::string abs_out = fs::absolute(out_prefix).string();

    int actual_threads = (threads <= 0) ? 4 : threads;
    std::string cmd = "\"" + abs_bin + "\" -m \"" + abs_model + "\" -f \"" + abs_wav + "\" -l " + language + " -t " + std::to_string(actual_threads) + " --no-fallback --suppress-nst -sow -ml 25 -osrt -of \"" + abs_out + "\"";

    int ret = ProcessRunner::execute(cmd);

    if (ret != 0 || !fs::exists(srt_path)) {
        result.error = "Whisper transcription that bai voi ma loi: " + std::to_string(ret);
        return result;
    }

    result.srt_path = srt_path;
    result.items = SrtParser::parseFile(srt_path);
    result.success = !result.items.empty();

    if (on_progress) on_progress(100, "Bóc tách thành công " + std::to_string(result.items.size()) + " đoạn thoại.");
    return result;
}

} // namespace VideoDubber
