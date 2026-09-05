#include "VideoDownloader.h"
#include "../core/ProcessRunner.h"
#include "../../include/json.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <regex>
#include <chrono>

namespace fs = std::filesystem;

namespace VideoDubber {

std::string VideoDownloader::extractCleanUrl(const std::string& input) {
    if (input.empty()) return "";
    // Match URL starting with http:// or https:// and continuing until whitespace, quotes, or bracket characters
    std::regex url_regex(R"(https?://[^\s"'`\(\)\[\]<>，。！？]+)");
    std::smatch match;
    if (std::regex_search(input, match, url_regex)) {
        std::string url = match.str();
        // Strip trailing punctuation like ), ], >, ., ,, ;, ', ", etc.
        while (!url.empty()) {
            char last = url.back();
            if (last == ')' || last == ']' || last == '>' || last == '.' || 
                last == ',' || last == ';' || last == '\'' || last == '\"') {
                url.pop_back();
                continue;
            }
            // Check multi-byte UTF-8 punctuation (Chinese comma, period, bracket)
            if (url.size() >= 3) {
                std::string tail = url.substr(url.size() - 3);
                if (tail == "\xEF\xBC\x8C" || tail == "\xE3\x80\x82" || 
                    tail == "\xEF\xBC\x81" || tail == "\xEF\xBC\x9F" || 
                    tail == "\xE3\x80\x91" || tail == "\xEF\xBC\x89") {
                    url.resize(url.size() - 3);
                    continue;
                }
            }
            break;
        }

        // If it's a Douyin modal link (e.g. https://www.douyin.com/jingxuan?modal_id=7670158001537371407), normalize to direct video URL
        if (url.find("douyin.com") != std::string::npos && url.find("modal_id=") != std::string::npos) {
            std::regex modal_regex(R"(modal_id=([0-9]+))");
            std::smatch m_match;
            if (std::regex_search(url, m_match, modal_regex)) {
                return "https://www.douyin.com/video/" + m_match[1].str();
            }
        }

        return url;
    }
    return input;
}

static void ensureDouyinCookie(const std::string& cookie_path) {
    if (fs::exists(cookie_path)) return;
    fs::create_directories(fs::path(cookie_path).parent_path());

    std::string ttwid = "1%7CbNuq88bX7oVD5_XwJ587ibK1evAXR72RiE-urP8_ftI%7C1788161753%7C7376ca741fd5a75f9399b75f0f04296bea1ccc349e81759cfe2eedc655222ccf";

    std::ofstream f(cookie_path);
    if (f.is_open()) {
        f << "# Netscape HTTP Cookie File\n";
        f << ".douyin.com\tTRUE\t/\tFALSE\t2000000000\tttwid\t" << ttwid << "\n";
        f << ".douyin.com\tTRUE\t/\tFALSE\t2000000000\tpassport_csrf_token\t1234567890abcdef\n";
    }
}

std::vector<std::string> VideoDownloader::parseLinkFile(const std::string& txt_filepath) {
    std::vector<std::string> links;
    std::ifstream file(txt_filepath);
    if (!file.is_open()) return links;

    std::string line;
    std::regex url_regex(R"(https?://[^\s"'`\(\)\[\]<>，。！？]+)");
    while (std::getline(file, line)) {
        auto words_begin = std::sregex_iterator(line.begin(), line.end(), url_regex);
        auto words_end = std::sregex_iterator();
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            std::string clean = extractCleanUrl(i->str());
            if (!clean.empty() && clean.rfind("http", 0) == 0) {
                links.push_back(clean);
            }
        }
    }
    return links;
}

DownloadResult VideoDownloader::downloadVideo(
    const std::string& raw_input, 
    const std::string& output_dir, 
    std::function<void(int, const std::string&)> on_progress
) {
    DownloadResult result;
    fs::create_directories(output_dir);

    std::string clean_url = extractCleanUrl(raw_input);
    if (on_progress) on_progress(10, "Bắt đầu tải video từ: " + clean_url);

    bool is_douyin = (clean_url.find("douyin.com") != std::string::npos);

    auto now_ms = std::chrono::system_clock::now().time_since_epoch().count();
    std::string expected_prefix = "vid_" + std::to_string(now_ms);
    std::string out_template = output_dir + "/" + expected_prefix + ".%(ext)s";

    // Multi-connection & high-speed chunked download flags:
    std::string extra_args = " --no-progress --no-check-certificates --no-warnings --no-playlist --concurrent-fragments 8 --buffersize 1024k --http-chunk-size 10M ";
    if (is_douyin) {
        std::string cookie_file = "config/douyin_cookies.txt";
        if (!fs::exists(cookie_file)) {
            cookie_file = "temp/douyin_cookies.txt";
            ensureDouyinCookie(cookie_file);
        }
        extra_args += " --cookies \"" + cookie_file + "\" ";
    }

    std::string cmd = "yt-dlp " + extra_args +
                      "-f \"best[ext=mp4]/best\" "
                      "-o \"" + out_template + "\" \"" + clean_url + "\"";

    if (on_progress) on_progress(30, "Đang tải luồng video chất lượng cao...");

    int ret = ProcessRunner::execute(cmd);

    // Check if yt-dlp succeeded
    try {
        for (const auto& entry : fs::directory_iterator(output_dir)) {
            if (entry.is_regular_file()) {
                auto fn_u8 = entry.path().filename().u8string();
                std::string fn(fn_u8.begin(), fn_u8.end());
                if (fn.find(expected_prefix) != std::string::npos) {
                    result.success = true;
                    auto p_u8 = entry.path().u8string();
                    result.video_path = std::string(p_u8.begin(), p_u8.end());
                    auto stem_u8 = entry.path().stem().u8string();
                    result.title = std::string(stem_u8.begin(), stem_u8.end());
                    if (on_progress) on_progress(100, "Tải hoàn tất: " + result.title);
                    return result;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "[VideoDownloader] Lỗi quét thư mục: " << ex.what() << "\n";
    }

    // FALLBACK: For Douyin, use Playwright headless browser download
    if (is_douyin) {
        if (on_progress) on_progress(40, "yt-dlp thất bại, chuyển sang Playwright tải Douyin...");
        std::cout << "[VideoDownloader] yt-dlp failed for Douyin. Trying Playwright fallback..." << std::endl;

        std::string err_log_file = output_dir + "/playwright_dl_error.log";
        std::string py_cmd = "python scripts/douyin_playwright_dl.py \"" + clean_url + "\" \"" + output_dir + "\" 2>\"" + err_log_file + "\"";
        std::string json_out_file = output_dir + "/playwright_dl_result.json";
        std::string full_cmd = py_cmd + " > \"" + json_out_file + "\"";

        if (on_progress) on_progress(50, "Playwright đang mở trang Douyin và tìm video...");
        int py_ret = ProcessRunner::execute(full_cmd);

        // Parse JSON result
        if (fs::exists(json_out_file)) {
            try {
                std::ifstream jf(json_out_file);
                std::string json_str((std::istreambuf_iterator<char>(jf)), std::istreambuf_iterator<char>());
                jf.close();

                if (!json_str.empty()) {
                    auto j = nlohmann::json::parse(json_str);
                    if (j.value("success", false)) {
                        result.success = true;
                        result.video_path = j.value("video_path", "");
                        result.title = j.value("title", "Douyin Video");
                        if (on_progress) on_progress(100, "Playwright tải thành công: " + result.title);
                        std::cout << "[VideoDownloader] Playwright download SUCCESS: " << result.video_path << std::endl;
                        return result;
                    } else {
                        result.error = j.value("error", "Playwright download failed");
                        std::cerr << "[VideoDownloader] Playwright error: " << result.error << std::endl;
                    }
                }
            } catch (const std::exception& ex) {
                std::cerr << "[VideoDownloader] JSON parse error: " << ex.what() << std::endl;
            }
        }

        // Check if Playwright error log has useful info
        if (fs::exists(err_log_file)) {
            try {
                std::ifstream ef(err_log_file);
                std::string err_str((std::istreambuf_iterator<char>(ef)), std::istreambuf_iterator<char>());
                if (!err_str.empty() && result.error.empty()) {
                    result.error = "Playwright fallback lỗi: " + err_str.substr(0, 200);
                }
            } catch (...) {}
        }
    }

    if (result.error.empty()) {
        result.error = "Lỗi khi tải video từ link: " + clean_url;
    }
    result.success = false;
    if (on_progress) on_progress(0, result.error);
    return result;
}

} // namespace VideoDubber
