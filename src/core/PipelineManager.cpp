#include "PipelineManager.h"
#include "Config.h"
#include "ProcessRunner.h"
#include "../downloader/VideoDownloader.h"
#include "../stt/WhisperTranscriber.h"
#include "../translator/AiTranslator.h"
#include "../tts/TtsEngine.h"
#include "../audio_separator/VocalSeparator.h"
#include "../video_processor/SubBlurrer.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <future>
#include <filesystem>
#include <algorithm>
#include <windows.h>

namespace fs = std::filesystem;

namespace VideoDubber {

PipelineManager& PipelineManager::instance() {
    static PipelineManager inst;
    return inst;
}

PipelineManager::PipelineManager() {
    start();
}

PipelineManager::~PipelineManager() {
    stop();
}

void PipelineManager::start() {
    if (is_running_) return;
    is_running_ = true;
    auto cfg = ConfigManager::instance().getConfig();
    if (cfg.max_concurrent_tasks > 0) {
        max_concurrent_tasks_ = cfg.max_concurrent_tasks;
    }
    int pool_size = std::max(4, max_concurrent_tasks_);
    for (int i = 0; i < pool_size; ++i) {
        worker_threads_.emplace_back(&PipelineManager::workerLoop, this);
    }
    log("Pipeline Manager worker pool khoi dong thanh cong voi " + std::to_string(pool_size) + " threads (Concurrent: " + std::to_string(max_concurrent_tasks_) + ").");
}

void PipelineManager::stop() {
    if (!is_running_) return;
    is_running_ = false;
    cv_.notify_all();
    for (auto& th : worker_threads_) {
        if (th.joinable()) {
            th.join();
        }
    }
    worker_threads_.clear();
    log("Pipeline Manager da dung an toan.");
}

int PipelineManager::getThreadsPerTask() const {
    int total = (int)std::thread::hardware_concurrency();
    if (total <= 0) total = 4;
    int per_task = total / std::max(1, max_concurrent_tasks_);
    return std::max(1, per_task);
}

void PipelineManager::setMaxConcurrentTasks(int max_tasks) {
    max_concurrent_tasks_ = std::max(1, max_tasks);
    while ((int)worker_threads_.size() < max_concurrent_tasks_) {
        worker_threads_.emplace_back(&PipelineManager::workerLoop, this);
    }
    cv_.notify_all();
}

void PipelineManager::log(const std::string& msg) {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    struct tm buf;
    localtime_s(&buf, &in_time_t);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "[%H:%M:%S] ", &buf);

    std::string line = time_str + msg;
    std::cout << line << std::endl;

    std::lock_guard<std::mutex> lock(logs_mutex_);
    logs_.push_back(line);
    if (logs_.size() > 500) {
        logs_.erase(logs_.begin(), logs_.begin() + 100);
    }
}

std::vector<std::string> PipelineManager::getRecentLogs(size_t count) {
    std::lock_guard<std::mutex> lock(logs_mutex_);
    if (logs_.empty()) return {};
    size_t start_idx = (logs_.size() > count) ? (logs_.size() - count) : 0;
    return std::vector<std::string>(logs_.begin() + start_idx, logs_.end());
}

std::string PipelineManager::detectPlatform(const std::string& input) {
    if (input.find("douyin.com") != std::string::npos) return "Douyin";
    if (input.find("tiktok.com") != std::string::npos) return "TikTok";
    if (input.find("facebook.com") != std::string::npos || input.find("fb.watch") != std::string::npos) return "Facebook";
    if (input.find("youtube.com") != std::string::npos || input.find("youtu.be") != std::string::npos) return "YouTube";
    if (input.find("kuaishou.com") != std::string::npos) return "Kuaishou";
    if (input.find("xiaohongshu.com") != std::string::npos || input.find("xhslink.com") != std::string::npos || input.find("rednote.com") != std::string::npos) return "Rednote";
    if (fs::exists(input)) return "Local File";
    return "Web Video";
}

std::string PipelineManager::stateToString(TaskState state) {
    switch (state) {
        case TaskState::QUEUED: return "QUEUED";
        case TaskState::CLAIMED: return "CLAIMED";
        case TaskState::DOWNLOADING: return "DOWNLOADING";
        case TaskState::PAUSED_AFTER_DOWNLOAD: return "PAUSED_AFTER_DOWNLOAD";
        case TaskState::TRANSCRIBING_STT: return "TRANSCRIBING_STT";
        case TaskState::PAUSED_AFTER_STT: return "PAUSED_AFTER_STT";
        case TaskState::TRANSLATING_AI: return "TRANSLATING_AI";
        case TaskState::PAUSED_AFTER_TRANSLATE: return "PAUSED_AFTER_TRANSLATE";
        case TaskState::PROCESSING_AUDIO: return "PROCESSING_AUDIO";
        case TaskState::RENDERING_VIDEO: return "RENDERING_VIDEO";
        case TaskState::COMPLETED: return "COMPLETED";
        case TaskState::FAILED: return "FAILED";
        case TaskState::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

std::string PipelineManager::addTask(const std::string& input, const std::string& type, const std::string& lang, const std::string& voice, const std::string& engine, const std::string& model, const std::string& bgm, const std::string& blur_mode, const std::string& naming_pattern, const std::string& custom_name) {
    if (input.empty()) return "";

    std::string actual_input = input;
    if (type == "url") {
        std::string cleaned = VideoDownloader::extractCleanUrl(input);
        if (!cleaned.empty()) actual_input = cleaned;
    }

    VideoTask task;
    uint64_t now_ms = (uint64_t)GetTickCount64();
    static int counter = 1;
    task.id = "task_" + std::to_string(now_ms) + "_" + std::to_string(counter++);
    task.input = actual_input;
    task.type = type;
    task.platform = (type == "file") ? "Local File" : detectPlatform(actual_input);
    task.title = (type == "file") ? fs::path(actual_input).stem().string() : ("Video " + task.platform);
    task.target_lang = lang;
    task.tts_voice = voice;
    task.tts_engine = engine;
    task.ai_model = model;
    task.bgm_mode = bgm;

    static std::atomic<int> s_seq_counter{0};
    task.sequence_num = ++s_seq_counter;
    task.output_naming_pattern = naming_pattern;
    task.custom_output_name = custom_name;

    auto cfg = ConfigManager::instance().getConfig();
    std::string eff_mode = !blur_mode.empty() ? blur_mode : cfg.blur_sub_mode;
    if (eff_mode.empty()) eff_mode = "auto_ocr";
    task.blur_sub_mode = eff_mode;

    // Task-scoped temp directory (OPT-2)
    task.temp_dir = cfg.temp_dir + "/" + task.id;
    try { fs::create_directories(fs::u8path(task.temp_dir)); } catch (...) {}

    // Expected steps & checkpoints calculated immediately on creation (Muc 3.1)
    if (eff_mode == "sub_only") {
        task.expected_steps = {"download_transcript", "translate", "render"};
        task.step_checkpoints = {
            {"download_transcript", "pending"},
            {"translate", "pending"},
            {"tts", "skipped"},
            {"mask", "skipped"},
            {"render", "pending"}
        };
    } else if (eff_mode == "none" || eff_mode == "sub_tts_mute") {
        task.expected_steps = {"download_transcript", "translate", "tts", "render"};
        task.step_checkpoints = {
            {"download_transcript", "pending"},
            {"translate", "pending"},
            {"tts", "pending"},
            {"mask", "skipped"},
            {"render", "pending"}
        };
    } else { // "auto_ocr", "bottom_sub", "frame_push", "always"
        task.expected_steps = {"download_transcript", "translate", "tts", "mask", "render"};
        task.step_checkpoints = {
            {"download_transcript", "pending"},
            {"translate", "pending"},
            {"tts", "pending"},
            {"mask", "pending"},
            {"render", "pending"}
        };
    }

    task.state = TaskState::QUEUED;
    task.status_msg = "Đang chờ xử lý trong hàng đợi";
    task.progress = 0;
    task.created_time = std::chrono::system_clock::now().time_since_epoch().count();

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        tasks_.push_back(task);
    }
    log("Đã thêm video vào hàng đợi: " + task.title + " (STT: " + std::to_string(task.sequence_num) + ", ID: " + task.id + ", Mode: " + eff_mode + ", Temp: " + task.temp_dir + ")");
    cv_.notify_all();
    return task.id;
}

std::vector<std::string> PipelineManager::addBatchLinks(const std::vector<std::string>& links, const std::string& lang, const std::string& voice, const std::string& engine, const std::string& model, const std::string& bgm, const std::string& blur_mode, const std::string& naming_pattern, const std::string& custom_name) {
    std::vector<std::string> ids;
    for (const auto& l : links) {
        if (!l.empty()) {
            std::string id = addTask(l, "url", lang, voice, engine, model, bgm, blur_mode, naming_pattern, custom_name);
            if (!id.empty()) ids.push_back(id);
        }
    }
    return ids;
}

std::vector<std::string> PipelineManager::addBatchFromTxt(const std::string& txt_filepath, const std::string& lang, const std::string& voice, const std::string& engine, const std::string& model, const std::string& bgm, const std::string& blur_mode, const std::string& naming_pattern, const std::string& custom_name) {
    auto links = VideoDownloader::parseLinkFile(txt_filepath);
    return addBatchLinks(links, lang, voice, engine, model, bgm, blur_mode, naming_pattern, custom_name);
}

std::string PipelineManager::generateOutputBaseName(const VideoTask& task, const AppConfig& cfg) {
    std::string pattern = !task.output_naming_pattern.empty() ? task.output_naming_pattern : cfg.output_naming_pattern;
    if (pattern.empty()) pattern = "seq_title";

    // Clean title for safe filename
    std::string clean_title = task.title;
    if (clean_title.empty()) {
        try {
            clean_title = fs::u8path(task.video_path).stem().u8string();
        } catch (...) {
            clean_title = "video";
        }
    }
    for (char& c : clean_title) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || c == '\r' || c == '\n' || c == '\t') {
            c = '_';
        }
    }
    if (clean_title.length() > 60) {
        clean_title = clean_title.substr(0, 60);
    }

    char seq_buf[16];
    if (task.sequence_num < 100) {
        snprintf(seq_buf, sizeof(seq_buf), "%02d", std::max(1, task.sequence_num));
    } else {
        snprintf(seq_buf, sizeof(seq_buf), "%03d", task.sequence_num);
    }
    std::string seq_str(seq_buf);

    if (pattern == "seq_title") {
        return seq_str + "_" + clean_title;
    } else if (pattern == "id_title") {
        return task.id + "_" + clean_title;
    } else if (pattern == "custom_seq") {
        std::string pfx = !task.custom_output_name.empty() ? task.custom_output_name : cfg.custom_output_prefix;
        if (pfx.empty()) pfx = "Video";
        return pfx + "_" + seq_str;
    } else if (pattern == "original_dubbed") {
        return clean_title + "_dubbed";
    }

    return seq_str + "_" + clean_title;
}

bool PipelineManager::pauseTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            t.state = TaskState::PAUSED_AFTER_DOWNLOAD;
            t.status_msg = "Đã tạm dừng bởi người dùng";
            log("Tạm dừng tác vụ: " + t.title);
            return true;
        }
    }
    return false;
}

bool PipelineManager::resumeTask(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == id) {
                if (t.state == TaskState::COMPLETED && !t.video_path.empty() && fs::exists(fs::u8path(t.video_path)) && !t.translated_srt.empty()) {
                    if (t.step_checkpoints["tts"] != "skipped") {
                        t.step_checkpoints["tts"] = "pending";
                    }
                    t.step_checkpoints["render"] = "pending";
                }
                t.state = TaskState::QUEUED;
                t.status_msg = "Sẵn sàng tiếp tục...";
                log("Tiếp tục tác vụ: " + t.title);
                break;
            }
        }
    }
    cv_.notify_all();
    return true;
}

bool PipelineManager::cancelTask(const std::string& id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            t.state = TaskState::CANCELLED;
            t.status_msg = "Đã hủy";
            log("Hủy tác vụ: " + t.title);
            return true;
        }
    }
    return false;
}

bool PipelineManager::retryTask(const std::string& id) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == id) {
                // Retry tu buoc bi loi tro di theo step_checkpoints (Muc 3.4)
                bool reset_subsequent = false;
                for (const auto& s : t.expected_steps) {
                    if (t.step_checkpoints[s] != "done" && t.step_checkpoints[s] != "skipped") {
                        reset_subsequent = true;
                    }
                    if (reset_subsequent && t.step_checkpoints[s] != "skipped") {
                        t.step_checkpoints[s] = "pending";
                    }
                }
                t.state = TaskState::QUEUED;
                t.status_msg = "Thử lại từ bước chưa hoàn thành...";
                t.error = "";
                log("Thử lại tác vụ: " + t.title);
                break;
            }
        }
    }
    cv_.notify_all();
    return true;
}

bool PipelineManager::resetTaskStep(const std::string& id, const std::string& step) {
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == id) {
                if (step == "stt") {
                    t.step_checkpoints["download_transcript"] = "pending";
                    t.step_checkpoints["translate"] = "pending";
                    if (t.step_checkpoints["tts"] != "skipped") t.step_checkpoints["tts"] = "pending";
                    t.step_checkpoints["render"] = "pending";
                } else if (step == "translate") {
                    t.step_checkpoints["translate"] = "pending";
                    if (t.step_checkpoints["tts"] != "skipped") t.step_checkpoints["tts"] = "pending";
                    t.step_checkpoints["render"] = "pending";
                } else if (step == "audio") {
                    if (t.step_checkpoints["tts"] != "skipped") t.step_checkpoints["tts"] = "pending";
                    t.step_checkpoints["render"] = "pending";
                }
                t.state = TaskState::QUEUED;
                t.status_msg = "Reset bước [" + step + "]...";
                log("Reset bước [" + step + "] cho tác vụ: " + t.title);
                break;
            }
        }
    }
    cv_.notify_all();
    return true;
}

bool PipelineManager::deleteTask(const std::string& id, bool delete_files) {
    std::string vid_path, out_path;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        auto it = std::remove_if(tasks_.begin(), tasks_.end(), [&](const VideoTask& t) {
            if (t.id == id) {
                vid_path = t.video_path;
                out_path = t.output_video_path;
                return true;
            }
            return false;
        });
        if (it != tasks_.end()) {
            tasks_.erase(it, tasks_.end());
        }
    }
    if (delete_files) {
        if (!vid_path.empty() && fs::exists(vid_path)) fs::remove(vid_path);
        if (!out_path.empty() && fs::exists(out_path)) fs::remove(out_path);
    }
    log("Đã xóa tác vụ ID: " + id);
    return true;
}

int PipelineManager::deleteCompletedTasks() {
    int cnt = 0;
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto it = std::remove_if(tasks_.begin(), tasks_.end(), [&](const VideoTask& t) {
        if (t.state == TaskState::COMPLETED) {
            cnt++;
            return true;
        }
        return false;
    });
    if (it != tasks_.end()) {
        tasks_.erase(it, tasks_.end());
    }
    log("Đã dọn dẹp " + std::to_string(cnt) + " tác vụ hoàn thành.");
    return cnt;
}

int PipelineManager::clearAllTasks() {
    int cnt = 0;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        cnt = (int)tasks_.size();
        tasks_.clear();
    }
    log("Đã xóa toàn bộ hàng đợi (" + std::to_string(cnt) + " tác vụ).");
    return cnt;
}

bool PipelineManager::updateTaskConfig(const std::string& id, const std::string& title, const std::string& lang, const std::string& voice, const std::string& bgm) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            if (!title.empty()) t.title = title;
            if (!lang.empty()) t.target_lang = lang;
            if (!voice.empty()) t.tts_voice = voice;
            if (!bgm.empty()) t.bgm_mode = bgm;
            return true;
        }
    }
    return false;
}

bool PipelineManager::updateTaskSrt(const std::string& id, const std::vector<SrtItem>& items, bool is_translated) {
    std::string srt_path = "";
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == id) {
                if (is_translated) {
                    t.translated_srt = items;
                    srt_path = t.translated_srt_path;
                } else {
                    t.source_srt = items;
                    srt_path = t.source_srt_path;
                }
                break;
            }
        }
    }
    if (!srt_path.empty() && !items.empty()) {
        SrtParser::saveToFile(srt_path, items, is_translated);
    }
    log("Đã cập nhật phụ đề (" + std::string(is_translated ? "Translated" : "Source") + ") cho task ID: " + id);
    return true;
}

bool PipelineManager::addSrtLine(const std::string& id, int after_index, const SrtItem& item, bool is_translated) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            auto& list = is_translated ? t.translated_srt : t.source_srt;
            if (after_index >= 0 && after_index < (int)list.size()) {
                list.insert(list.begin() + after_index + 1, item);
            } else {
                list.push_back(item);
            }
            for (size_t i = 0; i < list.size(); ++i) {
                list[i].id = (int)i + 1;
            }
            return true;
        }
    }
    return false;
}

bool PipelineManager::deleteSrtLine(const std::string& id, int line_index, bool is_translated) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (auto& t : tasks_) {
        if (t.id == id) {
            auto& list = is_translated ? t.translated_srt : t.source_srt;
            if (line_index >= 0 && line_index < (int)list.size()) {
                list.erase(list.begin() + line_index);
                for (size_t i = 0; i < list.size(); ++i) {
                    list[i].id = (int)i + 1;
                }
                return true;
            }
        }
    }
    return false;
}

bool PipelineManager::retranslateLine(const std::string& id, int line_id, const std::string& target_lang, std::string& out_trans) {
    std::string src_text = "";
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (const auto& t : tasks_) {
            if (t.id == id) {
                for (const auto& s : t.source_srt) {
                    if (s.id == line_id) {
                        src_text = s.text;
                        break;
                    }
                }
                break;
            }
        }
    }

    if (src_text.empty()) return false;

    SrtItem single;
    single.id = line_id;
    single.text = src_text;
    std::vector<SrtItem> vec = {single};

    auto cfg = ConfigManager::instance().getConfig();
    auto res = AiTranslator::translateSrt(vec, target_lang, cfg.default_model);
    if (res.success && !res.translated_items.empty()) {
        out_trans = res.translated_items[0].translated_text;
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == id) {
                for (auto& tr : t.translated_srt) {
                    if (tr.id == line_id) {
                        tr.translated_text = out_trans;
                        break;
                    }
                }
                break;
            }
        }
        return true;
    }
    return false;
}

bool PipelineManager::regenerateVoiceLine(const std::string& id, int line_id, const std::string& text, const std::string& voice, const std::string& engine, std::string& out_audio_path) {
    std::string task_temp_dir;
    std::string task_video_path;
    std::string eng = engine;
    std::string v = voice;
    int line_idx = -1;
    std::string text_to_speak = text;

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == id) {
                task_temp_dir = t.temp_dir.empty() ? ("temp/" + t.id) : t.temp_dir;
                task_video_path = t.video_path;
                if (eng.empty()) eng = !t.tts_engine.empty() ? t.tts_engine : "edge";
                if (v.empty()) v = !t.tts_voice.empty() ? t.tts_voice : "vi-VN-HoaiMyNeural";

                for (size_t i = 0; i < t.translated_srt.size(); ++i) {
                    if (t.translated_srt[i].id == line_id) {
                        line_idx = static_cast<int>(i);
                        if (text_to_speak.empty()) {
                            text_to_speak = !t.translated_srt[i].translated_text.empty() 
                                ? t.translated_srt[i].translated_text 
                                : t.translated_srt[i].text;
                        } else {
                            t.translated_srt[i].translated_text = text_to_speak;
                        }
                        break;
                    }
                }
                break;
            }
        }
    }

    if (line_idx < 0 || text_to_speak.empty() || task_temp_dir.empty()) {
        return false;
    }

    fs::create_directories(fs::u8path(task_temp_dir));
    std::string seg_mp3 = task_temp_dir + "/seg_" + std::to_string(line_idx) + ".mp3";
    std::string norm_wav = task_temp_dir + "/seg_" + std::to_string(line_idx) + "_aligned.wav";

    // 1. Generate single TTS segment
    bool ok = TtsEngine::generateSingleTts(text_to_speak, seg_mp3, eng, v);
    if (!ok || !fs::exists(fs::u8path(seg_mp3))) {
        return false;
    }
    out_audio_path = seg_mp3;

    // Remove old aligned file so timeline_aligner recalculates speed if needed
    try {
        if (fs::exists(fs::u8path(norm_wav))) {
            fs::remove(fs::u8path(norm_wav));
        }
    } catch (...) {}

    // 2. Export updated subtitles to tts_batch_input.json
    std::string json_input = task_temp_dir + "/tts_batch_input.json";
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (const auto& t : tasks_) {
            if (t.id == id) {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& it : t.translated_srt) {
                    arr.push_back({
                        {"id", it.id},
                        {"start_time", it.start_time},
                        {"end_time", it.end_time},
                        {"text", it.text},
                        {"translated_text", it.translated_text},
                        {"speaker", it.speaker}
                    });
                }
                std::ofstream jf(json_input);
                if (jf.is_open()) {
                    jf << arr.dump(2);
                }
                break;
            }
        }
    }

    // 3. Re-align timeline seamlessly
    std::string final_wav = task_temp_dir + "/voice_dubbed_final.wav";
    std::string align_cmd = "python timeline_aligner.py \"" + json_input + "\" \"" + task_temp_dir + "\" \"" + final_wav + "\"";
    if (!task_video_path.empty() && fs::exists(fs::u8path(task_video_path))) {
        align_cmd += " \"" + task_video_path + "\"";
    }
    int ret = ProcessRunner::execute(align_cmd);
    log("Tạo lại giọng dòng #" + std::to_string(line_id) + " cho tác vụ " + id + (ret == 0 ? " thành công" : " thất bại"));

    return (ret == 0 && fs::exists(fs::u8path(final_wav)));
}

nlohmann::json PipelineManager::getTasksJson(const std::string& filter_status, const std::string& search_query) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    nlohmann::json arr = nlohmann::json::array();

    for (const auto& t : tasks_) {
        std::string st_str = stateToString(t.state);
        if (filter_status != "ALL") {
            if (filter_status == "RUNNING" && (t.state == TaskState::COMPLETED || t.state == TaskState::FAILED || t.state == TaskState::CANCELLED || st_str.find("PAUSED") != std::string::npos)) continue;
            if (filter_status == "PAUSED" && st_str.find("PAUSED") == std::string::npos) continue;
            if (filter_status == "COMPLETED" && t.state != TaskState::COMPLETED) continue;
            if (filter_status == "FAILED" && t.state != TaskState::FAILED) continue;
        }

        if (!search_query.empty()) {
            std::string q_lower = search_query;
            std::transform(q_lower.begin(), q_lower.end(), q_lower.begin(), ::tolower);
            std::string t_lower = t.title + " " + t.input;
            std::transform(t_lower.begin(), t_lower.end(), t_lower.begin(), ::tolower);
            if (t_lower.find(q_lower) == std::string::npos) continue;
        }

        arr.push_back({
            {"id", t.id},
            {"title", t.title},
            {"input", t.input},
            {"type", t.type},
            {"platform", t.platform},
            {"state", st_str},
            {"status_msg", t.status_msg},
            {"progress", t.progress},
            {"video_path", t.video_path},
            {"output_video", t.output_video_path},
            {"error", t.error},
            {"has_source_srt", !t.source_srt.empty()},
            {"has_translated_srt", !t.translated_srt.empty()},
            {"blur_sub_mode", t.blur_sub_mode},
            {"ai_model", t.ai_model},
            {"tts_engine", t.tts_engine},
            {"temp_dir", t.temp_dir},
            {"sequence_num", t.sequence_num},
            {"output_naming_pattern", t.output_naming_pattern},
            {"custom_output_name", t.custom_output_name},
            {"expected_steps", t.expected_steps},
            {"step_checkpoints", t.step_checkpoints}
        });
    }
    return arr;
}

nlohmann::json PipelineManager::getTaskDetailJson(const std::string& id) {
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    for (const auto& t : tasks_) {
        if (t.id == id) {
            auto formatSrt = [](const std::vector<SrtItem>& srt) {
                nlohmann::json arr = nlohmann::json::array();
                for (const auto& s : srt) {
                    arr.push_back({
                        {"id", s.id},
                        {"start_time", s.start_time},
                        {"end_time", s.end_time},
                        {"text", s.text},
                        {"translated_text", s.translated_text}
                    });
                }
                return arr;
            };

            return {
                {"id", t.id},
                {"title", t.title},
                {"input", t.input},
                {"type", t.type},
                {"platform", t.platform},
                {"state", stateToString(t.state)},
                {"status_msg", t.status_msg},
                {"progress", t.progress},
                {"video_path", t.video_path},
                {"source_srt_path", t.source_srt_path},
                {"translated_srt_path", t.translated_srt_path},
                {"output_video", t.output_video_path},
                {"output_srt", t.output_srt_path},
                {"error", t.error},
                {"target_lang", t.target_lang},
                {"tts_voice", t.tts_voice},
                {"tts_engine", t.tts_engine},
                {"ai_model", t.ai_model},
                {"bgm_mode", t.bgm_mode},
                {"blur_sub_mode", t.blur_sub_mode},
                {"temp_dir", t.temp_dir},
                {"sequence_num", t.sequence_num},
                {"output_naming_pattern", t.output_naming_pattern},
                {"custom_output_name", t.custom_output_name},
                {"expected_steps", t.expected_steps},
                {"step_checkpoints", t.step_checkpoints},
                {"source_srt", formatSrt(t.source_srt)},
                {"translated_srt", formatSrt(t.translated_srt)}
            };
        }
    }
    return nlohmann::json::object();
}

void PipelineManager::workerLoop() {
    while (is_running_) {
        std::string target_id = "";
        {
            std::unique_lock<std::mutex> lock(tasks_mutex_);
            cv_.wait(lock, [this] {
                if (!is_running_) return true;
                if (active_workers_ >= max_concurrent_tasks_) return false;
                for (const auto& t : tasks_) {
                    if (t.state == TaskState::QUEUED) {
                        return true;
                    }
                }
                return false;
            });

            if (!is_running_) break;

            for (auto& t : tasks_) {
                if (t.state == TaskState::QUEUED) {
                    target_id = t.id;
                    t.state = TaskState::CLAIMED; // trung gian — KHONG dung DOWNLOADING
                    t.status_msg = "Đã nhận tác vụ...";
                    break;
                }
            }
        }

        if (!target_id.empty()) {
            active_workers_++;
            VideoTask copy_task;
            {
                std::lock_guard<std::mutex> lock(tasks_mutex_);
                for (const auto& t : tasks_) {
                    if (t.id == target_id) {
                        copy_task = t;
                        break;
                    }
                }
            }

            if (!copy_task.id.empty()) {
                processTask(copy_task);
            }
            active_workers_--;
            cv_.notify_all();
        }
    }
}

void PipelineManager::processTask(VideoTask& task) {
    auto cfg = ConfigManager::instance().getConfig();
    std::string target_lang = !task.target_lang.empty() ? task.target_lang : cfg.target_language;
    std::string voice = !task.tts_voice.empty() ? task.tts_voice : cfg.tts_voice;
    std::string bgm = !task.bgm_mode.empty() ? task.bgm_mode : cfg.bgm_mode;
    if (task.blur_sub_mode == "sub_tts_mute" && (task.bgm_mode.empty() || task.bgm_mode == "keep_bgm")) {
        // Tùy chọn sub_tts_mute: Xóa sạch âm thanh gốc, chỉ giữ lại giọng đọc TTS mới
        bgm = "mute_old";
    }

    // Ensure task temp directory is established (OPT-2)
    if (task.temp_dir.empty()) {
        task.temp_dir = cfg.temp_dir + "/" + task.id;
    }
    try { fs::create_directories(fs::u8path(task.temp_dir)); } catch (...) {}

    // Ensure expected_steps & checkpoints are initialized
    if (task.expected_steps.empty()) {
        std::string eff = !task.blur_sub_mode.empty() ? task.blur_sub_mode : cfg.blur_sub_mode;
        if (eff == "sub_only") {
            task.expected_steps = {"download_transcript", "translate", "render"};
            task.step_checkpoints = {{"download_transcript", "pending"}, {"translate", "pending"}, {"tts", "skipped"}, {"mask", "skipped"}, {"render", "pending"}};
        } else if (eff == "none" || eff == "sub_tts_mute") {
            task.expected_steps = {"download_transcript", "translate", "tts", "render"};
            task.step_checkpoints = {{"download_transcript", "pending"}, {"translate", "pending"}, {"tts", "pending"}, {"mask", "skipped"}, {"render", "pending"}};
        } else {
            task.expected_steps = {"download_transcript", "translate", "tts", "mask", "render"};
            task.step_checkpoints = {{"download_transcript", "pending"}, {"translate", "pending"}, {"tts", "pending"}, {"mask", "pending"}, {"render", "pending"}};
        }
    }

    // Dynamic progress calculator based on individual task's expected_steps (Muc 3.3)
    auto calcProgress = [&task](const std::string& current_step, int step_fraction_pct) {
        if (task.expected_steps.empty()) return step_fraction_pct;
        int step_idx = 0;
        for (size_t i = 0; i < task.expected_steps.size(); ++i) {
            if (task.expected_steps[i] == current_step) {
                step_idx = (int)i;
                break;
            }
        }
        int total_steps = (int)task.expected_steps.size();
        int step_weight = 100 / total_steps;
        int base_pct = step_idx * step_weight;
        int add_pct = (step_fraction_pct * step_weight) / 100;
        return std::min(99, base_pct + add_pct);
    };

    auto updateLiveStatus = [this, &task](TaskState st, int prog, const std::string& msg) {
        task.state = st;
        task.progress = prog;
        task.status_msg = msg;
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        for (auto& t : tasks_) {
            if (t.id == task.id) {
                t.state = st;
                t.progress = prog;
                t.status_msg = msg;
                t.video_path = task.video_path;
                t.title = task.title;
                t.source_srt_path = task.source_srt_path;
                t.translated_srt_path = task.translated_srt_path;
                t.source_srt = task.source_srt;
                t.translated_srt = task.translated_srt;
                t.output_video_path = task.output_video_path;
                t.output_srt_path = task.output_srt_path;
                t.error = task.error;
                t.step_checkpoints = task.step_checkpoints;
                t.expected_steps = task.expected_steps;
                t.temp_dir = task.temp_dir;
                break;
            }
        }
    };

    // ==========================================
    // 1. DOWNLOAD & TRANSCRIPT (Whisper STT)
    // ==========================================
    if (task.step_checkpoints["download_transcript"] != "done") {
        // 1a. Download video
        if (task.video_path.empty() || !fs::exists(fs::u8path(task.video_path))) {
            if (task.type == "url") {
                updateLiveStatus(TaskState::DOWNLOADING, calcProgress("download_transcript", 10), "Đang tải video từ " + task.platform + "...");
                log("[1/5] Tải video (" + task.platform + "): " + task.input);

                auto dl = VideoDownloader::downloadVideo(task.input, task.temp_dir, [this, &updateLiveStatus, &calcProgress](int p, const std::string& m) {
                    updateLiveStatus(TaskState::DOWNLOADING, calcProgress("download_transcript", p / 2), m);
                });

                if (!dl.success) {
                    task.error = dl.error;
                    updateLiveStatus(TaskState::FAILED, 0, dl.error);
                    log("[Lỗi] Tải thất bại: " + dl.error);
                    return;
                }
                task.video_path = dl.video_path;
                task.title = dl.title;
            } else {
                task.video_path = task.input;
                task.title = fs::path(task.input).stem().string();
            }

            if (cfg.pause_after_download) {
                updateLiveStatus(TaskState::PAUSED_AFTER_DOWNLOAD, calcProgress("download_transcript", 50), "Tạm dừng sau Download");
                log("Tạm dừng tác vụ sau khi tải xong: " + task.title);
                return;
            }
        }

        // 1b. Whisper STT
        if (task.source_srt.empty()) {
            updateLiveStatus(TaskState::TRANSCRIBING_STT, calcProgress("download_transcript", 55), "Bóc tách phụ đề Whisper...");
            log("[2/5] Bóc sub tiếng Trung bằng Whisper.cpp: " + task.title);

            int stt_threads = (cfg.whisper_threads <= 0) ? getThreadsPerTask() : cfg.whisper_threads;
            auto stt = WhisperTranscriber::transcribeVideo(
                task.video_path,
                cfg.whisper_model_path,
                cfg.source_language,
                stt_threads,
                task.temp_dir,
                [this, &updateLiveStatus, &calcProgress](int p, const std::string& m) {
                    updateLiveStatus(TaskState::TRANSCRIBING_STT, calcProgress("download_transcript", 50 + p / 2), m);
                }
            );

            if (!stt.success) {
                task.error = stt.error;
                updateLiveStatus(TaskState::FAILED, 0, stt.error);
                log("[Lỗi] STT thất bại: " + stt.error);
                return;
            }

            task.source_srt_path = stt.srt_path;
            task.source_srt = SrtParser::parseFile(stt.srt_path);

            if (cfg.pause_after_stt) {
                updateLiveStatus(TaskState::PAUSED_AFTER_STT, calcProgress("download_transcript", 100), "Tạm dừng sau STT (Chờ sửa tiếng Trung)");
                log("Tạm dừng tác vụ sau STT: " + task.title);
                return;
            }
        }

        task.step_checkpoints["download_transcript"] = "done";
    }

    // ==========================================
    // 2. AI TRANSLATION
    // ==========================================
    if (task.step_checkpoints["translate"] != "done") {
        updateLiveStatus(TaskState::TRANSLATING_AI, calcProgress("translate", 10), "Đang dịch phụ đề AI...");
        log("[3/5] Dịch phụ đề AI: " + task.title);

        if (task.source_srt.empty() && !task.source_srt_path.empty()) {
            task.source_srt = SrtParser::parseFile(task.source_srt_path);
        }

        std::string model_to_use = !task.ai_model.empty() ? task.ai_model : cfg.default_model;
        auto trans = AiTranslator::translateSrt(
            task.source_srt,
            target_lang,
            model_to_use,
            task.temp_dir,
            [this, &updateLiveStatus, &calcProgress](int p, const std::string& m) {
                updateLiveStatus(TaskState::TRANSLATING_AI, calcProgress("translate", p), m);
            }
        );

        if (!trans.success) {
            task.error = trans.error;
            updateLiveStatus(TaskState::FAILED, 0, trans.error);
            log("[Lỗi] Dịch thuật thất bại: " + trans.error);
            return;
        }

        task.translated_srt = trans.translated_items;
        std::string trans_srt_file = task.temp_dir + "/trans_" + target_lang + ".srt";
        SrtParser::saveToFile(trans_srt_file, task.translated_srt, true);
        task.translated_srt_path = trans_srt_file;

        task.step_checkpoints["translate"] = "done";

        if (cfg.pause_after_translate) {
            updateLiveStatus(TaskState::PAUSED_AFTER_TRANSLATE, calcProgress("translate", 100), "Tạm dừng sau Dịch (Chờ sửa bản dịch)");
            log("Tạm dừng tác vụ sau dịch AI: " + task.title);
            return;
        }
    }

    // ==========================================
    // 3. TTS & BGM PROCESSING (Rẽ nhánh sub_only)
    // ==========================================
    std::string final_audio_path = "";
    if (task.blur_sub_mode == "sub_only" || task.step_checkpoints["tts"] == "skipped") {
        // Preset Chỉ Sub (Bang 2.3): Dub giong moi = Khong - giu am goc; Xu ly am = Khong ap dung
        task.step_checkpoints["tts"] = "skipped";
        task.step_checkpoints["mask"] = "skipped";
        log("[3/5 Skipped] Preset Chỉ Sub: Giữ nguyên 100% âm thanh gốc: " + task.title);
        std::string orig_audio = task.temp_dir + "/orig_audio.wav";
        if (!fs::exists(fs::u8path(orig_audio))) {
            std::string cmd = "ffmpeg -y -i \"" + task.video_path + "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 \"" + orig_audio + "\"";
            ProcessRunner::execute(cmd);
        }
        final_audio_path = fs::exists(fs::u8path(orig_audio)) ? orig_audio : task.video_path;
    } else {
        if (task.step_checkpoints["tts"] != "done") {
            updateLiveStatus(TaskState::PROCESSING_AUDIO, calcProgress("tts", 20), "Sinh giọng TTS và đồng bộ atempo...");
            log("[4/5] Sinh giọng TTS & Tách BGM: " + task.title);

            if (task.translated_srt.empty() && !task.translated_srt_path.empty()) {
                task.translated_srt = SrtParser::parseFile(task.translated_srt_path);
                for (auto& it : task.translated_srt) {
                    if (it.translated_text.empty() && !it.text.empty()) {
                        it.translated_text = it.text;
                    }
                }
            }

            if (task.translated_srt.empty()) {
                log("[TTS] Không có phụ đề thoại để lồng tiếng. Giữ nguyên âm thanh gốc: " + task.title);
                std::string orig_audio = task.temp_dir + "/orig_audio.wav";
                std::string cmd = "ffmpeg -y -i \"" + task.video_path + "\" -vn -acodec pcm_s16le -ar 44100 -ac 2 \"" + orig_audio + "\"";
                ProcessRunner::execute(cmd);
                final_audio_path = fs::exists(fs::u8path(orig_audio)) ? orig_audio : task.video_path;
            } else {
                std::string tts_eng = !task.tts_engine.empty() ? task.tts_engine : cfg.tts_engine;
                std::string tts_vc = !task.tts_voice.empty() ? task.tts_voice : cfg.tts_voice;
                if (tts_vc == "google") tts_eng = "google";
                else if (tts_vc == "vibi") tts_eng = "vibi";
                else if (tts_eng.empty()) tts_eng = "edge";

                // OPT-3: Parallelize BGM separation with TTS generation using std::async
                std::string bgm_wav = task.temp_dir + "/extracted_bgm.wav";
                bool bgm_needed = (bgm == "keep_bgm" || bgm == "ducking");
                std::future<bool> bgm_future;
                if (bgm_needed && !fs::exists(fs::u8path(bgm_wav))) {
                    bgm_future = std::async(std::launch::async, [v_path = task.video_path, bgm_wav]() {
                        return VocalSeparator::separateBgm(v_path, bgm_wav);
                    });
                }

                auto tts_res = TtsEngine::generateAndSyncVoice(
                    task.translated_srt,
                    tts_eng,
                    tts_vc,
                    task.temp_dir,
                    [this, &updateLiveStatus, &calcProgress](int p, const std::string& m) {
                        updateLiveStatus(TaskState::PROCESSING_AUDIO, calcProgress("tts", (int)(p * 0.7)), m);
                    }
                );

                if (!tts_res.success) {
                    task.error = tts_res.error;
                    updateLiveStatus(TaskState::FAILED, 0, tts_res.error);
                    log("[Lỗi] TTS thất bại: " + tts_res.error);
                    return;
                }

                // Wait for BGM separation future with try/catch to ensure exceptions never crash worker
                if (bgm_future.valid()) {
                    try {
                        bool bgm_ok = bgm_future.get();
                        if (!bgm_ok) {
                            log("[Cảnh báo] Tách BGM thất bại, sẽ giữ âm thanh giọng đọc thuần túy.");
                        }
                    } catch (const std::exception& e) {
                        log(std::string("[Cảnh báo] Lỗi tách BGM song song: ") + e.what());
                    } catch (...) {
                        log("[Cảnh báo] Lỗi không xác định khi tách BGM song song.");
                    }
                }

                final_audio_path = tts_res.final_audio_path;
                if (bgm_needed) {
                    double bgm_vol = (bgm == "ducking") ? 0.15 : 0.25;
                    updateLiveStatus(TaskState::PROCESSING_AUDIO, calcProgress("tts", 85), "Hòa âm BGM Ducking...");
                    auto bgm_res = VocalSeparator::mixVoiceAndBgm(
                        task.video_path,
                        tts_res.final_audio_path,
                        bgm,
                        bgm_vol,
                        cfg.voice_volume,
                        task.temp_dir
                    );
                    if (bgm_res.success) {
                        final_audio_path = bgm_res.mixed_audio_path;
                    }
                }
            }
            task.step_checkpoints["tts"] = "done";
        } else {
            std::string tts_audio = task.temp_dir + "/mixed_voice_bgm.wav";
            if (!fs::exists(fs::u8path(tts_audio))) tts_audio = task.temp_dir + "/voice_dubbed_final.wav";
            final_audio_path = fs::exists(fs::u8path(tts_audio)) ? tts_audio : task.video_path;
        }
    }

    // ==========================================
    // 4. MASK (OCR/Delogo check)
    // ==========================================
    if (task.step_checkpoints["mask"] != "skipped") {
        task.step_checkpoints["mask"] = "done";
    }

    // ==========================================
    // 5. RENDER FINAL MP4 & EXPORT SRT
    // ==========================================
    if (task.step_checkpoints["render"] != "done") {
        std::string blur_mode = !task.blur_sub_mode.empty() ? task.blur_sub_mode : cfg.blur_sub_mode;
        std::string mode_desc;
        if (blur_mode == "none") mode_desc = "Không che mờ (Chỉ lồng tiếng)";
        else if (blur_mode == "sub_tts_mute") mode_desc = "Chèn Sub + Lồng tiếng TTS + Xóa âm gốc";
        else if (blur_mode == "auto_ocr") mode_desc = "Việt hóa & Che mờ động OCR";
        else if (blur_mode == "bottom_sub") mode_desc = "Chỉ che phụ đề đáy";
        else if (blur_mode == "sub_only") mode_desc = "Chèn sub Việt (Không che mờ)";
        else if (blur_mode == "always") mode_desc = "Luôn che mờ";
        else mode_desc = "Tự động che khi có sub";

        updateLiveStatus(TaskState::RENDERING_VIDEO, calcProgress("render", 10), "Khóa ô vuông delogo & Render MP4 (" + mode_desc + ")...");
        log("[5/5] Render Master MP4 (Chế độ: " + mode_desc + ", Số câu sub: " + std::to_string(task.translated_srt.size()) + "): " + task.title);

        fs::create_directories(cfg.output_dir);
        std::string desired_name = generateOutputBaseName(task, cfg);

        auto blur_res = SubBlurrer::renderFinalVideo(
            task.video_path,
            final_audio_path,
            task.translated_srt_path,
            task.translated_srt,
            blur_mode,
            cfg.output_dir,
            cfg.blur_bottom_ratio,
            cfg.blur_kernel_size,
            getThreadsPerTask(),
            task.temp_dir,
            [this, &updateLiveStatus, &calcProgress](int p, const std::string& m) {
                updateLiveStatus(TaskState::RENDERING_VIDEO, calcProgress("render", p), m);
            },
            desired_name
        );

        if (!blur_res.success) {
            task.error = blur_res.error;
            updateLiveStatus(TaskState::FAILED, 0, blur_res.error);
            log("[Lỗi] Render thất bại: " + blur_res.error);
            return;
        }

        task.output_video_path = blur_res.output_video_path;
        task.output_srt_path = blur_res.output_srt_path;
        task.step_checkpoints["render"] = "done";
    }

    updateLiveStatus(TaskState::COMPLETED, 100, "Hoàn thành 100%!");
    log("🎉 HOÀN THÀNH XUẤT SẮC: " + task.output_video_path);
}

} // namespace VideoDubber
