#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "../stt/SrtParser.h"
#include "../../include/json.hpp"

namespace VideoDubber {

struct AppConfig;

enum class TaskState {
    QUEUED,
    CLAIMED,
    DOWNLOADING,
    PAUSED_AFTER_DOWNLOAD,
    TRANSCRIBING_STT,
    PAUSED_AFTER_STT,
    TRANSLATING_AI,
    PAUSED_AFTER_TRANSLATE,
    PROCESSING_AUDIO,
    RENDERING_VIDEO,
    COMPLETED,
    FAILED,
    CANCELLED
};

struct VideoTask {
    std::string id;
    std::string input;
    std::string type; // "url" or "file"
    TaskState state = TaskState::QUEUED;
    int progress = 0;
    std::string status_msg = "Chờ xử lý";

    std::string title;
    std::string platform = "Unknown";
    std::string video_path;
    std::string source_srt_path;
    std::string translated_srt_path;
    std::vector<SrtItem> source_srt;
    std::vector<SrtItem> translated_srt;

    // Custom overrides per task
    std::string target_lang = "";
    std::string tts_voice = "";
    std::string tts_engine = "";
    std::string ai_model = "";       // Per-task: "gemini", "deepseek"
    std::string bgm_mode = "";
    std::string blur_sub_mode = "";  // Per-task: "none", "auto_ocr", "bottom_sub", "sub_only"

    // Output naming customization
    int sequence_num = 0;
    std::string output_naming_pattern = "";
    std::string custom_output_name = "";

    std::string output_video_path;
    std::string output_srt_path;
    std::string error;

    int64_t created_time = 0;

    // Multi-threading & checkpoint fields (muc 3.1 & OPT-2)
    std::string temp_dir;
    std::vector<std::string> expected_steps;
    std::map<std::string, std::string> step_checkpoints;
};

class PipelineManager {
public:
    static PipelineManager& instance();

    // Task CRUD
    std::string addTask(const std::string& input, const std::string& type = "url", const std::string& lang = "", const std::string& voice = "", const std::string& engine = "", const std::string& model = "", const std::string& bgm = "", const std::string& blur_mode = "", const std::string& naming_pattern = "", const std::string& custom_name = "");
    std::vector<std::string> addBatchLinks(const std::vector<std::string>& links, const std::string& lang = "", const std::string& voice = "", const std::string& engine = "", const std::string& model = "", const std::string& bgm = "", const std::string& blur_mode = "", const std::string& naming_pattern = "", const std::string& custom_name = "");
    std::vector<std::string> addBatchFromTxt(const std::string& txt_filepath, const std::string& lang = "", const std::string& voice = "", const std::string& engine = "", const std::string& model = "", const std::string& bgm = "", const std::string& blur_mode = "", const std::string& naming_pattern = "", const std::string& custom_name = "");

    std::string generateOutputBaseName(const VideoTask& task, const AppConfig& cfg);

    bool pauseTask(const std::string& id);
    bool resumeTask(const std::string& id);
    bool cancelTask(const std::string& id);
    bool retryTask(const std::string& id);
    bool resetTaskStep(const std::string& id, const std::string& step); // "stt", "translate", "audio"
    bool deleteTask(const std::string& id, bool delete_files = false);
    int deleteCompletedTasks();
    int clearAllTasks();
    bool updateTaskConfig(const std::string& id, const std::string& title, const std::string& lang, const std::string& voice, const std::string& bgm);

    // Subtitle CRUD & Helpers
    bool updateTaskSrt(const std::string& id, const std::vector<SrtItem>& items, bool is_translated);
    bool addSrtLine(const std::string& id, int after_index, const SrtItem& item, bool is_translated);
    bool deleteSrtLine(const std::string& id, int line_index, bool is_translated);
    bool retranslateLine(const std::string& id, int line_id, const std::string& target_lang, std::string& out_trans);
    bool regenerateVoiceLine(const std::string& id, int line_id, const std::string& text, const std::string& voice, const std::string& engine, std::string& out_audio_path);

    // JSON API
    nlohmann::json getTasksJson(const std::string& filter_status = "ALL", const std::string& search_query = "");
    nlohmann::json getTaskDetailJson(const std::string& id);

    void log(const std::string& msg);
    std::vector<std::string> getRecentLogs(size_t count = 60);

    void start();
    void stop();

    // Thread Budget (muc 3.5 & OPT-5, 6)
    int getThreadsPerTask() const;
    void setMaxConcurrentTasks(int max_tasks);
    int getMaxConcurrentTasks() const { return max_concurrent_tasks_; }

private:
    PipelineManager();
    ~PipelineManager();

    void workerLoop();
    void processTask(VideoTask& task);
    std::string detectPlatform(const std::string& input);

    std::vector<VideoTask> tasks_;
    std::vector<std::string> logs_;
    std::mutex tasks_mutex_;
    std::mutex logs_mutex_;
    std::vector<std::thread> worker_threads_;
    std::atomic<bool> is_running_{false};
    std::atomic<int> active_workers_{0};
    std::condition_variable cv_;
    int max_concurrent_tasks_ = 2;

    std::string stateToString(TaskState state);
};

} // namespace VideoDubber