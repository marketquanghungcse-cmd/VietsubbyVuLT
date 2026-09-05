#include "SubBlurrer.h"
#include "../core/ProcessRunner.h"
#include "../../include/json.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <iomanip>

namespace fs = std::filesystem;

namespace VideoDubber {

static std::string formatAssTime(int64_t ms) {
    int64_t total_sec = ms / 1000;
    int64_t cs = (ms % 1000) / 10;
    int h = (int)(total_sec / 3600);
    int m = (int)((total_sec % 3600) / 60);
    int s = (int)(total_sec % 60);
    char buf[64];
    snprintf(buf, sizeof(buf), "%d:%02d:%02d.%02lld", h, m, s, cs);
    return std::string(buf);
}

static bool checkNvencAvailable() {
    static int cached = -1;
    if (cached != -1) return (cached == 1);
    int ret = ProcessRunner::execute("ffmpeg -hide_banner -encoders 2>nul | findstr /i nvenc >nul");
    cached = (ret == 0) ? 1 : 0;
    return (cached == 1);
}

RenderResult SubBlurrer::renderFinalVideo(
    const std::string& input_video_path,
    const std::string& mixed_audio_path,
    const std::string& translated_srt_path,
    const std::vector<SrtItem>& subtitle_items,
    const std::string& blur_sub_mode,
    const std::string& output_dir,
    double blur_bottom_ratio,
    int blur_kernel,
    int threads,
    const std::string& temp_dir,
    std::function<void(int progress, const std::string& msg)> on_progress,
    const std::string& desired_output_filename
) {
    RenderResult result;
    fs::create_directories(output_dir);
    fs::create_directories(temp_dir);

    fs::path vPath = fs::u8path(input_video_path);
    std::string base_name = vPath.stem().u8string();
    std::string final_name = !desired_output_filename.empty() ? desired_output_filename : (base_name + "_dubbed");
    std::string out_mp4 = output_dir + "/" + final_name + ".mp4";
    std::string out_srt = output_dir + "/" + final_name + ".srt";

    if (on_progress) on_progress(10, "Phân tích thông số kích thước video gốc...");

    // 1. Probe video width & height via ffprobe
    int vid_w = 1920, vid_h = 1080;
    std::string probe_txt = temp_dir + "/probe_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".txt";
    std::string probe_cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 \"" + input_video_path + "\" > \"" + probe_txt + "\"";
    ProcessRunner::execute(probe_cmd);

    std::ifstream pw(probe_txt);
    if (pw.is_open()) {
        std::string line;
        if (std::getline(pw, line) && !line.empty()) {
            size_t x_pos = line.find('x');
            if (x_pos != std::string::npos) {
                try {
                    int w = std::stoi(line.substr(0, x_pos));
                    int h = std::stoi(line.substr(x_pos + 1));
                    if (w > 0 && h > 0) {
                        vid_w = w;
                        vid_h = h;
                    }
                } catch (...) {}
            }
        }
        pw.close();
    }
    try { fs::remove(probe_txt); } catch (...) {}

    // 2. Parse subtitles if not provided in memory
    std::vector<SrtItem> items = subtitle_items;
    if (items.empty() && !translated_srt_path.empty() && fs::exists(fs::u8path(translated_srt_path))) {
        items = SrtParser::parseFile(translated_srt_path);
    }
    bool has_subtitles = !items.empty();

    // 3. Determine blur mode behavior
    // Modes:
    //   "none"       → 0% blur, 0% sub overlay (voice-only, keep original 100%)
    //   "auto_ocr"   → dynamic OCR delogo from JSON boxes + Vietnamese subs
    //   "bottom_sub" → fixed bottom strip delogo (hardsub anime/drama) + Vietnamese subs
    //   "sub_only"   → 0% blur, add Vietnamese subtitle overlay on original
    //   "always"     → always delogo fixed box + subs (legacy)
    //   "auto"       → delogo only if subs exist (legacy default)
    bool should_blur = false;
    bool should_add_subs = has_subtitles; // default: add subs if they exist
    bool use_dynamic_boxes = false;
    bool use_bottom_strip = false;

    if (blur_sub_mode == "none") {
        should_blur = false;
        should_add_subs = false; // Voice-only, no sub overlay, no blur
    } else if (blur_sub_mode == "sub_only") {
        should_blur = false;
        should_add_subs = has_subtitles; // Overlay Vietnamese subs but don't blur anything
    } else if (blur_sub_mode == "sub_tts_mute") {
        should_blur = false;
        should_add_subs = has_subtitles; // Overlay Vietnamese subs, voice dubbed, no blur
    } else if (blur_sub_mode == "auto_ocr") {
        should_blur = true;
        use_dynamic_boxes = true; // Use dynamic_delogo_boxes.json
        should_add_subs = has_subtitles;
    } else if (blur_sub_mode == "bottom_sub") {
        should_blur = has_subtitles; // Only blur when there are subs
        use_bottom_strip = true;
        should_add_subs = has_subtitles;
    } else if (blur_sub_mode == "always") {
        should_blur = true;
        should_add_subs = has_subtitles;
    } else if (blur_sub_mode == "disabled") {
        should_blur = false;
        should_add_subs = false;
    } else {
        // "auto" mode (default): blur only if subtitles exist
        should_blur = has_subtitles;
        should_add_subs = has_subtitles;
    }

    std::string safe_srt = temp_dir + "/active_render_sub.srt";
    if (!items.empty()) {
        SrtParser::saveToFile(safe_srt, items, true);
    } else if (fs::exists(fs::u8path(translated_srt_path))) {
        try {
            fs::copy_file(fs::u8path(translated_srt_path), fs::u8path(safe_srt), fs::copy_options::overwrite_existing);
        } catch (...) {
            safe_srt = translated_srt_path;
        }
    }
    std::replace(safe_srt.begin(), safe_srt.end(), '\\', '/');

    // Calculate aspect ratio and geometry parameters
    bool is_portrait = (vid_h > vid_w);
    int box_w = 0, box_h = 0, box_x = 0, box_y = 0;
    int fontsize = 28, margin_v = 45;

    if (!is_portrait) {
        // Landscape (16:9) - Subtitle font increased for clear readability
        box_w = (int)std::round(0.75 * vid_w);
        box_x = (vid_w - box_w) / 2;
        box_y = (int)std::round(0.885 * vid_h);
        box_h = (int)std::round(0.088 * vid_h);
        fontsize = (vid_h >= 1080) ? 38 : ((vid_h >= 720) ? 28 : 22);
        margin_v = (vid_h >= 1080) ? 58 : 40;
    } else {
        // Portrait (9:16) - Subtitle font increased significantly for mobile screens
        box_w = (int)std::round(0.86 * vid_w);
        box_x = (vid_w - box_w) / 2;
        box_y = (int)std::round(0.720 * vid_h);
        box_h = (int)std::round(0.080 * vid_h);
        fontsize = (vid_h >= 1920) ? 46 : ((vid_h >= 1280) ? 34 : 26);
        margin_v = (vid_h >= 1920) ? 175 : 115;
    }

    // Ensure within bounds and even numbers for codecs
    box_x = std::max(0, box_x);
    box_y = std::max(0, std::min(box_y, vid_h - 20));
    box_w = std::min(box_w, vid_w - box_x);
    box_h = std::min(box_h, vid_h - box_y);
    if (box_w % 2 != 0) box_w--;
    if (box_h % 2 != 0) box_h--;

    // Build perfect resolution-matched ASS subtitles file
    std::string safe_ass = temp_dir + "/active_render_sub.ass";
    if (should_add_subs && has_subtitles) {
        std::ofstream af(fs::u8path(safe_ass));
        if (af.is_open()) {
            af << "[Script Info]\nTitle: VideoDubberPro Subtitles\nScriptType: v4.00+\nWrapStyle: 0\n"
               << "PlayResX: " << vid_w << "\nPlayResY: " << vid_h << "\nScaledBorderAndShadow: yes\n\n"
               << "[V4+ Styles]\n"
               << "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
               << "Style: Default,Arial," << fontsize << ",&H00FFFFFF,&H000000FF,&H00000000,&H25000000,-1,0,0,0,100,100,0,0,3,8,0,2,40,40," << margin_v << ",1\n\n"
               << "[Events]\n"
               << "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
            for (const auto& it : items) {
                std::string txt = !it.translated_text.empty() ? it.translated_text : it.text;
                std::replace(txt.begin(), txt.end(), '\n', ' ');
                af << "Dialogue: 0," << formatAssTime(it.start_ms) << "," << formatAssTime(it.end_ms) << ",Default,,0,0,0,," << txt << "\n";
            }
            af.close();
        }
    }
    std::replace(safe_ass.begin(), safe_ass.end(), '\\', '/');

    std::string cmd;
    std::string vf;
    bool direct_copy = false;
    if (on_progress) on_progress(40, "Thiết lập cấu hình FFmpeg Render Master...");

    if (!should_blur) {
        // KỊCH BẢN 1: KHÔNG LÀM MỜ
        if (should_add_subs && has_subtitles) {
            // sub_only or auto with subs: overlay Vietnamese subs without blurring
            vf = "subtitles=filename='" + safe_ass + "'";
        } else {
            // none mode: Copy trực tiếp video stream 100% nguyên bản nét căng!
            direct_copy = true;
        }
    } else {
        // KỊCH BẢN 2: LÀM MỜ DELOGO
        std::vector<std::string> delogo_filter_list;

        if (use_dynamic_boxes) {
            // auto_ocr mode: Run OCR detector script if available to produce dynamic_delogo_boxes.json
            std::string dynamic_boxes_file = temp_dir + "/dynamic_delogo_boxes.json";
            std::string ocr_script = "scripts/detect_sub_boxes.py";
            if (fs::exists(fs::u8path(ocr_script)) && fs::exists(fs::u8path(safe_srt))) {
                if (on_progress) on_progress(45, "Quét RapidOCR nhận diện vị trí phụ đề chữ Hán...");
                std::string ocr_cmd = "python \"" + ocr_script + "\" \"" + input_video_path + "\" \"" + safe_srt + "\" \"" + dynamic_boxes_file + "\"";
                ProcessRunner::execute(ocr_cmd);
            }

            if (fs::exists(fs::u8path(dynamic_boxes_file))) {
                try {
                    std::ifstream f(fs::u8path(dynamic_boxes_file));
                    nlohmann::json dj = nlohmann::json::parse(f);
                    for (const auto& b : dj) {
                        double st = b.value("start", 0.0);
                        double en = b.value("end", 0.0);
                        int bx = b.value("x", box_x);
                        int by = b.value("y", box_y);
                        int bw = b.value("w", box_w);
                        int bh = b.value("h", box_h);
                        if (bw % 2 != 0) bw--;
                        if (bh % 2 != 0) bh--;
                        std::ostringstream bss;
                        bss << std::fixed << std::setprecision(2);
                        bss << "delogo=x=" << bx << ":y=" << by << ":w=" << bw << ":h=" << bh
                            << ":show=0:enable='between(t," << st << "," << en << ")'";
                        delogo_filter_list.push_back(bss.str());
                    }
                } catch (...) {}
            }
            // Fallback: if no dynamic boxes found, use dialogue time-enabled bottom box
            if (delogo_filter_list.empty()) {
                std::string enable_expr = "1";
                if (!items.empty()) {
                    std::vector<std::string> time_ranges;
                    for (const auto& it : items) {
                        double t_start = std::max<double>(0.0, (double)(it.start_ms - 100) / 1000.0);
                        double t_end = (double)(it.end_ms + 50) / 1000.0;
                        std::ostringstream ss;
                        ss << std::fixed << std::setprecision(2) << "between(t," << t_start << "," << t_end << ")";
                        time_ranges.push_back(ss.str());
                    }
                    std::ostringstream expr_ss;
                    for (size_t i = 0; i < time_ranges.size(); ++i) {
                        if (i > 0) expr_ss << "+";
                        expr_ss << time_ranges[i];
                    }
                    enable_expr = expr_ss.str();
                }
                std::string single_delogo = "delogo=x=" + std::to_string(box_x) +
                                           ":y=" + std::to_string(box_y) +
                                           ":w=" + std::to_string(box_w) +
                                           ":h=" + std::to_string(box_h) +
                                           ":show=0:enable='" + enable_expr + "'";
                delogo_filter_list.push_back(single_delogo);
            }
        } else if (use_bottom_strip) {
            // bottom_sub mode: Fixed bottom strip delogo, enabled only during dialogue
            std::string enable_expr = "1";
            if (!items.empty()) {
                std::vector<std::string> time_ranges;
                for (const auto& it : items) {
                    double t_start = std::max<double>(0.0, (double)(it.start_ms - 100) / 1000.0);
                    double t_end = (double)(it.end_ms + 50) / 1000.0;
                    std::ostringstream ss;
                    ss << std::fixed << std::setprecision(2) << "between(t," << t_start << "," << t_end << ")";
                    time_ranges.push_back(ss.str());
                }
                std::ostringstream expr_ss;
                for (size_t i = 0; i < time_ranges.size(); ++i) {
                    if (i > 0) expr_ss << "+";
                    expr_ss << time_ranges[i];
                }
                enable_expr = expr_ss.str();
            }
            std::string single_delogo = "delogo=x=" + std::to_string(box_x) +
                                       ":y=" + std::to_string(box_y) +
                                       ":w=" + std::to_string(box_w) +
                                       ":h=" + std::to_string(box_h) +
                                       ":show=0:enable='" + enable_expr + "'";
            delogo_filter_list.push_back(single_delogo);
        } else {
            // "always" or legacy "auto" mode
            std::string dynamic_boxes_file = temp_dir + "/dynamic_delogo_boxes.json";
            if (fs::exists(dynamic_boxes_file)) {
                try {
                    std::ifstream f(dynamic_boxes_file);
                    nlohmann::json dj = nlohmann::json::parse(f);
                    for (const auto& b : dj) {
                        double st = b.value("start", 0.0);
                        double en = b.value("end", 0.0);
                        int bx = b.value("x", box_x);
                        int by = b.value("y", box_y);
                        int bw = b.value("w", box_w);
                        int bh = b.value("h", box_h);
                        if (bw % 2 != 0) bw--;
                        if (bh % 2 != 0) bh--;
                        std::ostringstream bss;
                        bss << std::fixed << std::setprecision(2);
                        bss << "delogo=x=" << bx << ":y=" << by << ":w=" << bw << ":h=" << bh
                            << ":show=0:enable='between(t," << st << "," << en << ")'";
                        delogo_filter_list.push_back(bss.str());
                    }
                } catch (...) {}
            }

            if (delogo_filter_list.empty()) {
                std::string enable_expr = "1";
                if (blur_sub_mode != "always" && !items.empty()) {
                    std::vector<std::string> time_ranges;
                    for (const auto& it : items) {
                        double t_start = std::max<double>(0.0, (double)(it.start_ms - 100) / 1000.0);
                        double t_end = (double)(it.end_ms + 50) / 1000.0;
                        std::ostringstream ss;
                        ss << std::fixed << std::setprecision(2) << "between(t," << t_start << "," << t_end << ")";
                        time_ranges.push_back(ss.str());
                    }
                    std::ostringstream expr_ss;
                    for (size_t i = 0; i < time_ranges.size(); ++i) {
                        if (i > 0) expr_ss << "+";
                        expr_ss << time_ranges[i];
                    }
                    enable_expr = expr_ss.str();
                }
                std::string single_delogo = "delogo=x=" + std::to_string(box_x) +
                                           ":y=" + std::to_string(box_y) +
                                           ":w=" + std::to_string(box_w) +
                                           ":h=" + std::to_string(box_h) +
                                           ":show=0:enable='" + enable_expr + "'";
                delogo_filter_list.push_back(single_delogo);
            }
        }

        std::ostringstream delogos_ss;
        for (size_t i = 0; i < delogo_filter_list.size(); ++i) {
            if (i > 0) delogos_ss << ",";
            delogos_ss << delogo_filter_list[i];
        }
        std::string delogo_filters_combined = delogos_ss.str();

        if (should_add_subs && has_subtitles) {
            std::string sub_filter = "subtitles=filename='" + safe_ass + "'";
            vf = delogo_filters_combined + "," + sub_filter;
        } else {
            vf = delogo_filters_combined;
        }
    }

    int th = (threads > 0) ? threads : 4;
    std::string th_str = std::to_string(th);
    bool has_nvenc = checkNvencAvailable();

    if (on_progress) on_progress(60, "Đang Render Master MP4 hoàn chỉnh với FFmpeg...");

    int ret = -1;
    if (direct_copy) {
        cmd = "ffmpeg -y -i \"" + input_video_path + "\" -i \"" + mixed_audio_path + "\" "
              "-c:v copy -c:a aac -b:a 192k -shortest \"" + out_mp4 + "\"";
        ret = ProcessRunner::execute(cmd + " 2> temp/ffmpeg_render_error.log");
    } else {
        if (has_nvenc) {
            cmd = "ffmpeg -y -threads " + th_str + " -hwaccel auto "
                  "-i \"" + input_video_path + "\" -i \"" + mixed_audio_path + "\" "
                  "-vf \"" + vf + "\" -map 0:v -map 1:a "
                  "-c:v h264_nvenc -preset p4 -cq 21 -c:a aac -b:a 192k -shortest \"" + out_mp4 + "\"";
            ret = ProcessRunner::execute(cmd + " 2> \"" + temp_dir + "/ffmpeg_render_error.log\"");
        }

        // Fallback sang libx264 neu NVENC khong kha dung hoac gap loi / het session
        if (!has_nvenc || ret != 0 || !fs::exists(fs::u8path(out_mp4)) || fs::file_size(fs::u8path(out_mp4)) == 0) {
            if (has_nvenc && ret != 0) {
                std::cerr << "[SubBlurrer] NVENC encoder failed (exit code " << ret << "). Falling back to libx264 software encoder...\n";
            }
            cmd = "ffmpeg -y -threads " + th_str + " "
                  "-i \"" + input_video_path + "\" -i \"" + mixed_audio_path + "\" "
                  "-vf \"" + vf + "\" -map 0:v -map 1:a "
                  "-c:v libx264 -preset fast -crf 21 -threads " + th_str + " "
                  "-c:a aac -b:a 192k -shortest \"" + out_mp4 + "\"";
            ret = ProcessRunner::execute(cmd + " 2> \"" + temp_dir + "/ffmpeg_render_error.log\"");
        }
    }

    std::ofstream lf(fs::u8path(temp_dir + "/last_render_cmd.txt"));
    if (lf.is_open()) {
        lf << cmd << std::endl;
        lf.close();
    }

    if (fs::exists(fs::u8path(out_mp4))) {
        result.success = true;
        result.output_video_path = out_mp4;
        result.output_srt_path = out_srt;
        if (!items.empty()) {
            SrtParser::saveToFile(out_srt, items, true);
        } else if (fs::exists(fs::u8path(translated_srt_path))) {
            try {
                fs::copy_file(fs::u8path(translated_srt_path), fs::u8path(out_srt), fs::copy_options::overwrite_existing);
            } catch (...) {}
        }
        if (on_progress) on_progress(100, "Render hoàn tất video: " + out_mp4);
        return result;
    }

    result.error = "Lỗi khi Render MP4 bằng FFmpeg";
    return result;
}

} // namespace VideoDubber
