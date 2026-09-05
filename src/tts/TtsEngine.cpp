#include "TtsEngine.h"
#include "../core/ProcessRunner.h"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include "../../include/json.hpp"

namespace fs = std::filesystem;

namespace VideoDubber {

double TtsEngine::getAudioDurationSec(const std::string& audio_file) {
    if (!fs::exists(audio_file)) return 0.0;
    std::string parent_dir = fs::path(audio_file).parent_path().string();
    if (parent_dir.empty()) parent_dir = "temp";
    std::string dur_file = parent_dir + "/dur_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt";
    std::string cmd = "ffprobe -v error -show_entries format=duration -of default=noprint_wrappers=1:nokey=1 \"" + audio_file + "\" > \"" + dur_file + "\"";
    ProcessRunner::execute(cmd);
    std::ifstream f(dur_file);
    double dur = 0.0;
    if (f.is_open()) {
        f >> dur;
        f.close();
        try { fs::remove(dur_file); } catch (...) {}
    }
    return dur;
}

bool TtsEngine::generateSingleTts(
    const std::string& text, 
    const std::string& out_path, 
    const std::string& engine, 
    const std::string& voice
) {
    if (text.empty()) return false;
    std::string eng = engine;
    std::string v = voice;
    if (v == "google" || eng == "google") {
        eng = "google";
    } else if (v == "vibi" || eng == "vibi") {
        eng = "vibi";
    } else {
        eng = "edge";
        if (v.empty() || v == "edge") v = "vi-VN-HoaiMyNeural";
    }

    std::string cmd;
    if (eng == "google") {
        cmd = "python -c \"import sys, urllib.request, urllib.parse; url='https://translate.google.com/translate_tts?ie=UTF-8&tl=vi&client=tw-ob&q='+urllib.parse.quote(sys.argv[1]); req=urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'}); open(sys.argv[2], 'wb').write(urllib.request.urlopen(req).read())\" \"" + text + "\" \"" + out_path + "\"";
    } else if (eng == "vibi") {
        cmd = "python -c \"import sys; from vibi_client import VibiClient; c=VibiClient(); c.generate_speech(sys.argv[1], '', sys.argv[2])\" \"" + text + "\" \"" + out_path + "\"";
    } else {
        cmd = "edge-tts --voice " + v + " --text \"" + text + "\" --write-media \"" + out_path + "\"";
    }
    int ret = ProcessRunner::execute(cmd);
    return (ret == 0 && fs::exists(out_path) && fs::file_size(out_path) > 100);
}

bool TtsEngine::timeStretchAudio(
    const std::string& in_wav, 
    const std::string& out_wav, 
    double speed_ratio
) {
    speed_ratio = std::clamp(speed_ratio, 0.5, 2.0);
    std::string cmd = "ffmpeg -y -i \"" + in_wav + "\" -filter:a \"atempo=" + std::to_string(speed_ratio) + "\" -vn \"" + out_wav + "\"";
    int ret = ProcessRunner::execute(cmd);
    return (ret == 0 && fs::exists(out_wav));
}

TtsResult TtsEngine::generateAndSyncVoice(
    const std::vector<SrtItem>& items,
    const std::string& tts_engine,
    const std::string& voice_name,
    const std::string& output_dir,
    std::function<void(int progress, const std::string& msg)> on_progress
) {
    TtsResult result;
    fs::create_directories(output_dir);

    if (items.empty()) {
        result.error = "Danh sách phụ đề rỗng";
        return result;
    }

    if (on_progress) on_progress(10, "Đang sinh toàn bộ giọng đọc siêu tốc Edge TTS...");

    // Export items to JSON
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& it : items) {
        arr.push_back({
            {"id", it.id},
            {"start_time", it.start_time},
            {"end_time", it.end_time},
            {"text", it.text},
            {"translated_text", it.translated_text},
            {"speaker", it.speaker}
        });
    }

    std::string json_input = output_dir + "/tts_batch_input.json";
    {
        std::ofstream jf(json_input);
        if (jf.is_open()) {
            jf << arr.dump(2);
        }
    }

    std::string v = !voice_name.empty() ? voice_name : "vi-VN-HoaiMyNeural";
    std::string eng = !tts_engine.empty() ? tts_engine : "edge";
    std::string batch_cmd = "python batch_tts.py \"" + json_input + "\" \"" + output_dir + "\" \"" + v + "\" \"" + eng + "\"";
    ProcessRunner::execute(batch_cmd);

    if (on_progress) on_progress(75, "Đang căn chỉnh chính xác từng mili-giây theo timeline video...");

    std::string final_wav = output_dir + "/voice_dubbed_final.wav";
    std::string align_cmd = "python timeline_aligner.py \"" + json_input + "\" \"" + output_dir + "\" \"" + final_wav + "\"";
    int ret = ProcessRunner::execute(align_cmd);

    if (fs::exists(final_wav)) {
        result.success = true;
        result.final_audio_path = final_wav;
        if (on_progress) on_progress(100, "Đồng bộ timeline âm thanh thành công 100%");
        return result;
    }

    result.error = "Không thể đồng bộ timeline giọng đọc";
    return result;
}

} // namespace VideoDubber
