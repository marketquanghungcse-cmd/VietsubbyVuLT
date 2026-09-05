#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include "HttpGuiServer.h"
#include "../core/Config.h"
#include "../core/ApiKeyPool.h"
#include "../core/PipelineManager.h"
#include "../core/ProcessRunner.h"
#include "../downloader/VideoDownloader.h"
#include "../tts/TtsEngine.h"
#include "../translator/AiTranslator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <vector>
#include <map>
#include <set>
#include <chrono>

namespace VideoDubber {

HttpGuiServer& HttpGuiServer::instance() {
    static HttpGuiServer inst;
    return inst;
}

HttpGuiServer::~HttpGuiServer() {
    stop();
}

bool HttpGuiServer::start(int port, const std::string& web_root) {
    if (is_running_) return true;
    port_ = port;
    web_root_ = web_root;

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[HttpGuiServer] WSAStartup failed.\n";
        return false;
    }

    SOCKET listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock == INVALID_SOCKET) {
        std::cerr << "[HttpGuiServer] socket creation failed.\n";
        WSACleanup();
        return false;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons((u_short)port_);

    if (bind(listen_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[HttpGuiServer] bind failed on port " << port_ << ".\n";
        closesocket(listen_sock);
        WSACleanup();
        return false;
    }

    if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "[HttpGuiServer] listen failed.\n";
        closesocket(listen_sock);
        WSACleanup();
        return false;
    }

    listen_socket_ = (uintptr_t)listen_sock;
    is_running_ = true;

    server_thread_ = std::thread(&HttpGuiServer::serverLoop, this);
    std::cout << "[HttpGuiServer] Web Studio Dashboard running on: http://127.0.0.1:" << port_ << "\n";
    return true;
}

void HttpGuiServer::stop() {
    if (!is_running_) return;
    is_running_ = false;
    if (listen_socket_ != INVALID_SOCKET && listen_socket_ != 0) {
        closesocket((SOCKET)listen_socket_);
        listen_socket_ = 0;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    WSACleanup();
}

void HttpGuiServer::serverLoop() {
    while (is_running_) {
        sockaddr_in client_addr{};
        int client_len = sizeof(client_addr);
        SOCKET client_sock = accept((SOCKET)listen_socket_, (sockaddr*)&client_addr, &client_len);
        if (client_sock == INVALID_SOCKET) {
            if (!is_running_) break;
            continue;
        }

        std::thread([this, client_sock]() {
            handleClient((uintptr_t)client_sock);
        }).detach();
    }
}

static std::string getMimeType(const std::string& path) {
    if (path.rfind(".html") != std::string::npos) return "text/html; charset=utf-8";
    if (path.rfind(".css") != std::string::npos) return "text/css; charset=utf-8";
    if (path.rfind(".js") != std::string::npos) return "application/javascript; charset=utf-8";
    if (path.rfind(".json") != std::string::npos) return "application/json; charset=utf-8";
    if (path.rfind(".mp4") != std::string::npos) return "video/mp4";
    if (path.rfind(".mp3") != std::string::npos) return "audio/mpeg";
    if (path.rfind(".wav") != std::string::npos) return "audio/wav";
    if (path.rfind(".srt") != std::string::npos) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static std::string urlDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto fromHex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h1 = fromHex(in[i + 1]);
            int h2 = fromHex(in[i + 2]);
            if (h1 >= 0 && h2 >= 0) {
                out.push_back((char)((h1 << 4) | h2));
                i += 2;
                continue;
            }
        } else if (in[i] == '+') {
            out.push_back(' ');
            continue;
        }
        out.push_back(in[i]);
    }
    return out;
}

static void serveFileWithRange(SOCKET sock, const std::filesystem::path& file_path, const std::string& header_part, const std::string& mime_type, const std::string& attachment_filename = "") {
    namespace fs = std::filesystem;
    std::error_code ec;
    uintmax_t fsize = fs::file_size(file_path, ec);
    if (ec) {
        std::string err_resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nFile Not Found";
        send(sock, err_resp.c_str(), (int)err_resp.size(), 0);
        closesocket(sock);
        return;
    }

    int64_t file_size = (int64_t)fsize;
    int64_t start_byte = 0;
    int64_t end_byte = file_size - 1;
    bool is_range = false;

    // Check for Range: bytes=start-end
    size_t r_pos = header_part.find("Range: bytes=");
    if (r_pos == std::string::npos) r_pos = header_part.find("range: bytes=");
    if (r_pos != std::string::npos) {
        size_t val_start = r_pos + 13;
        size_t r_end_line = header_part.find("\r\n", val_start);
        std::string range_val = header_part.substr(val_start, r_end_line - val_start);
        size_t dash = range_val.find('-');
        if (dash != std::string::npos) {
            std::string s_str = range_val.substr(0, dash);
            std::string e_str = range_val.substr(dash + 1);
            if (!s_str.empty()) {
                try { start_byte = std::stoll(s_str); } catch (...) {}
            }
            if (!e_str.empty()) {
                try { end_byte = std::stoll(e_str); } catch (...) {}
            }
            if (end_byte >= file_size) end_byte = file_size - 1;
            if (start_byte < 0) start_byte = 0;
            if (start_byte <= end_byte) {
                is_range = true;
            }
        }
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        std::string err_resp = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nCannot open file";
        send(sock, err_resp.c_str(), (int)err_resp.size(), 0);
        closesocket(sock);
        return;
    }

    int64_t content_length = is_range ? (end_byte - start_byte + 1) : file_size;
    std::ostringstream ss;
    if (is_range) {
        ss << "HTTP/1.1 206 Partial Content\r\n";
        ss << "Content-Range: bytes " << start_byte << "-" << end_byte << "/" << file_size << "\r\n";
    } else {
        ss << "HTTP/1.1 200 OK\r\n";
    }
    ss << "Content-Type: " << mime_type << "\r\n";
    ss << "Content-Length: " << content_length << "\r\n";
    if (!attachment_filename.empty()) {
        ss << "Content-Disposition: attachment; filename=\"" << attachment_filename << "\"\r\n";
    }
    ss << "Accept-Ranges: bytes\r\n";
    ss << "Access-Control-Allow-Origin: *\r\n";
    ss << "Access-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n";
    ss << "Access-Control-Allow-Headers: Range, Content-Type\r\n";
    ss << "Connection: close\r\n\r\n";

    std::string header_str = ss.str();
    int h_sent = 0;
    while (h_sent < (int)header_str.size()) {
        int s = send(sock, header_str.c_str() + h_sent, (int)header_str.size() - h_sent, 0);
        if (s <= 0) { closesocket(sock); return; }
        h_sent += s;
    }

    if (header_part.find("HEAD ") == 0) {
        closesocket(sock);
        return;
    }

    if (start_byte > 0) {
        file.seekg(start_byte);
    }

    char chunk[65536];
    int64_t remaining = content_length;
    while (remaining > 0) {
        int to_read = (int)std::min<int64_t>((int64_t)sizeof(chunk), remaining);
        file.read(chunk, to_read);
        int bytes_read = (int)file.gcount();
        if (bytes_read <= 0) break;

        int sent_total = 0;
        while (sent_total < bytes_read) {
            int s = send(sock, chunk + sent_total, bytes_read - sent_total, 0);
            if (s <= 0) { remaining = 0; break; } // Client disconnected
            sent_total += s;
        }
        remaining -= bytes_read;
    }

    closesocket(sock);
}

void HttpGuiServer::handleClient(uintptr_t client_socket) {
    SOCKET sock = (SOCKET)client_socket;
    std::vector<char> buffer(65536);
    int bytes_read = recv(sock, buffer.data(), (int)buffer.size() - 1, 0);
    if (bytes_read <= 0) {
        closesocket(sock);
        return;
    }
    buffer[bytes_read] = '\0';

    std::string req(buffer.data(), bytes_read);
    size_t header_end = req.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        closesocket(sock);
        return;
    }

    std::string header_part = req.substr(0, header_end);
    std::string body = req.substr(header_end + 4);

    std::istringstream req_stream(header_part);
    std::string method, raw_path, protocol;
    req_stream >> method >> raw_path >> protocol;

    // Read full body if Content-Length specified
    size_t cl_pos = header_part.find("Content-Length:");
    if (cl_pos == std::string::npos) cl_pos = header_part.find("content-length:");
    if (cl_pos != std::string::npos) {
        size_t line_end = header_part.find("\r\n", cl_pos);
        std::string cl_str = header_part.substr(cl_pos + 15, line_end - (cl_pos + 15));
        int total_len = std::atoi(cl_str.c_str());
        while ((int)body.length() < total_len) {
            int extra = recv(sock, buffer.data(), (int)buffer.size() - 1, 0);
            if (extra <= 0) break;
            body.append(buffer.data(), extra);
        }
    }

    // Split path and query params
    std::string path = raw_path;
    std::string query_str = "";
    size_t q_pos = raw_path.find('?');
    if (q_pos != std::string::npos) {
        path = raw_path.substr(0, q_pos);
        query_str = raw_path.substr(q_pos + 1);
    }

    std::map<std::string, std::string> query_params;
    if (!query_str.empty()) {
        std::istringstream qs(query_str);
        std::string pair;
        while (std::getline(qs, pair, '&')) {
            size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                query_params[pair.substr(0, eq)] = pair.substr(eq + 1);
            } else {
                query_params[pair] = "";
            }
        }
    }

    auto sendResponse = [sock](int status, const std::string& mime, const std::string& content) {
        std::string status_txt = (status == 200) ? "OK" : (status == 404 ? "Not Found" : (status == 400 ? "Bad Request" : "Internal Server Error"));
        std::ostringstream ss;
        ss << "HTTP/1.1 " << status << " " << status_txt << "\r\n";
        ss << "Content-Type: " << mime << "\r\n";
        ss << "Content-Length: " << content.length() << "\r\n";
        ss << "Access-Control-Allow-Origin: *\r\n";
        ss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        ss << "Access-Control-Allow-Headers: Content-Type\r\n";
        ss << "Connection: close\r\n\r\n";
        ss << content;
        std::string resp_str = ss.str();
        send(sock, resp_str.c_str(), (int)resp_str.length(), 0);
        closesocket(sock);
    };

    if (method == "OPTIONS") {
        sendResponse(200, "text/plain", "");
        return;
    }

    try {
        // ==========================================
        // 1. CONFIG & PRESETS
        // ==========================================
        if (path == "/api/config" && method == "GET") {
            auto cfg = ConfigManager::instance().getConfig();
            nlohmann::json j;
            j["default_model"] = cfg.default_model;
            j["gemini_model_name"] = cfg.gemini_model_name;
            j["deepseek_model_name"] = cfg.deepseek_model_name;
            j["target_language"] = cfg.target_language;
            j["source_language"] = cfg.source_language;
            j["tts_engine"] = cfg.tts_engine;
            j["tts_voice"] = cfg.tts_voice;
            j["vibi_api_key"] = cfg.vibi_api_key;
            j["vibi_default_voice_id"] = cfg.vibi_default_voice_id;
            j["vibi_provider"] = cfg.vibi_provider;
            j["vibi_model_id"] = cfg.vibi_model_id;
            j["bgm_mode"] = cfg.bgm_mode;
            j["blur_mode"] = cfg.blur_mode;
            j["blur_sub_mode"] = cfg.blur_sub_mode;
            j["blur_bottom_ratio"] = cfg.blur_bottom_ratio;
            j["blur_kernel_size"] = cfg.blur_kernel_size;
            j["pause_after_download"] = cfg.pause_after_download;
            j["pause_after_stt"] = cfg.pause_after_stt;
            j["pause_after_translate"] = cfg.pause_after_translate;
            j["output_dir"] = cfg.output_dir;
            j["output_naming_pattern"] = cfg.output_naming_pattern;
            j["custom_output_prefix"] = cfg.custom_output_prefix;
            j["server_port"] = cfg.server_port;
            j["max_concurrent_tasks"] = cfg.max_concurrent_tasks;
            sendResponse(200, "application/json", j.dump());
            return;
        }

        if (path == "/api/config" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            auto cfg = ConfigManager::instance().getConfig();
            if (j.contains("default_model")) cfg.default_model = j["default_model"].get<std::string>();
            if (j.contains("target_language")) cfg.target_language = j["target_language"].get<std::string>();
            if (j.contains("tts_engine")) cfg.tts_engine = j["tts_engine"].get<std::string>();
            if (j.contains("tts_voice")) cfg.tts_voice = j["tts_voice"].get<std::string>();
            if (j.contains("vibi_api_key")) cfg.vibi_api_key = j["vibi_api_key"].get<std::string>();
            if (j.contains("vibi_default_voice_id")) cfg.vibi_default_voice_id = j["vibi_default_voice_id"].get<std::string>();
            if (j.contains("vibi_provider")) cfg.vibi_provider = j["vibi_provider"].get<std::string>();
            if (j.contains("vibi_model_id")) cfg.vibi_model_id = j["vibi_model_id"].get<std::string>();
            if (j.contains("bgm_mode")) cfg.bgm_mode = j["bgm_mode"].get<std::string>();
            if (j.contains("blur_sub_mode")) cfg.blur_sub_mode = j["blur_sub_mode"].get<std::string>();
            if (j.contains("blur_bottom_ratio")) cfg.blur_bottom_ratio = j["blur_bottom_ratio"].get<double>();
            if (j.contains("blur_kernel_size")) cfg.blur_kernel_size = j["blur_kernel_size"].get<int>();
            if (j.contains("pause_after_download")) cfg.pause_after_download = j["pause_after_download"].get<bool>();
            if (j.contains("pause_after_stt")) cfg.pause_after_stt = j["pause_after_stt"].get<bool>();
            if (j.contains("pause_after_translate")) cfg.pause_after_translate = j["pause_after_translate"].get<bool>();
            if (j.contains("output_dir")) cfg.output_dir = j["output_dir"].get<std::string>();
            if (j.contains("output_naming_pattern")) cfg.output_naming_pattern = j["output_naming_pattern"].get<std::string>();
            if (j.contains("custom_output_prefix")) cfg.custom_output_prefix = j["custom_output_prefix"].get<std::string>();
            if (j.contains("max_concurrent_tasks")) {
                cfg.max_concurrent_tasks = j["max_concurrent_tasks"].get<int>();
                PipelineManager::instance().setMaxConcurrentTasks(cfg.max_concurrent_tasks);
            }

            ConfigManager::instance().setConfig(cfg);
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/vibi/test" && method == "POST") {
            std::string key = "";
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                key = j.value("api_key", "");
            } catch (...) {}
            if (key.empty()) {
                auto cfg = ConfigManager::instance().getConfig();
                key = cfg.vibi_api_key;
            }
            std::string cmd = "python vibi_client.py test_json \"" + key + "\" > temp/vibi_test.json";
            ProcessRunner::execute(cmd);
            std::ifstream f("temp/vibi_test.json");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (content.empty()) content = "{\"success\": false, \"error\": \"Thực thi kiểm tra thất bại\"}";
            sendResponse(200, "application/json", content);
            return;
        }

        if (path == "/api/vibi/voices" && method == "POST") {
            std::string key = "";
            std::string prov = "minimax";
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                key = j.value("api_key", "");
                prov = j.value("provider", "minimax");
            } catch (...) {}
            if (key.empty()) {
                auto cfg = ConfigManager::instance().getConfig();
                key = cfg.vibi_api_key;
            }
            std::string cmd = "python vibi_client.py voices_json \"" + key + "\" \"" + prov + "\" > temp/vibi_voices.json";
            ProcessRunner::execute(cmd);
            std::ifstream f("temp/vibi_voices.json");
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            if (content.empty()) content = "{\"success\": false, \"voices\": []}";
            sendResponse(200, "application/json", content);
            return;
        }

        // ==========================================
        // DOUYIN LOGIN & ACCOUNT STATUS
        // ==========================================
        if (path == "/api/douyin/status" && method == "GET") {
            bool has_json = std::filesystem::exists("config/douyin_cookies.json");
            bool has_txt = std::filesystem::exists("config/douyin_cookies.txt");
            nlohmann::json res;
            res["logged_in"] = has_json || has_txt;
            res["cookie_file"] = has_json ? "config/douyin_cookies.json" : (has_txt ? "config/douyin_cookies.txt" : "");
            sendResponse(200, "application/json", res.dump());
            return;
        }

        if (path == "/api/douyin/login" && method == "POST") {
            std::thread([]() {
                ProcessRunner::executeVisible("dang_nhap_douyin.bat");
            }).detach();
            sendResponse(200, "application/json", "{\"success\": true, \"message\": \"Đã mở cửa sổ đăng nhập Douyin trên máy tính\"}");
            return;
        }

        if (path == "/api/douyin/save_cookies" && method == "POST") {
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                std::string raw = j.value("cookies", "");
                if (raw.empty()) {
                    sendResponse(400, "application/json", "{\"success\": false, \"message\": \"Nội dung cookie rỗng!\"}");
                    return;
                }

                std::filesystem::create_directories("config");
                std::filesystem::create_directories("temp");

                try {
                    auto test_json = nlohmann::json::parse(raw);
                    if (test_json.is_array() || test_json.is_object()) {
                        std::ofstream f1("config/douyin_cookies.json");
                        f1 << test_json.dump(2);
                        std::ofstream f2("temp/douyin_cookies.json");
                        f2 << test_json.dump(2);
                    }
                } catch (...) {}

                std::ofstream f_txt1("config/douyin_cookies.txt");
                f_txt1 << raw;
                std::ofstream f_txt2("temp/douyin_cookies.txt");
                f_txt2 << raw;

                sendResponse(200, "application/json", "{\"success\": true, \"message\": \"Đã lưu cookie thành công!\"}");
                return;
            } catch (const std::exception& ex) {
                sendResponse(500, "application/json", std::string("{\"success\": false, \"message\": \"") + ex.what() + "\"}");
                return;
            }
        }

        if (path == "/api/douyin/logout" && method == "POST") {
            std::filesystem::remove("config/douyin_cookies.json");
            std::filesystem::remove("config/douyin_cookies.txt");
            std::filesystem::remove("temp/douyin_cookies.json");
            std::filesystem::remove("temp/douyin_cookies.txt");
            sendResponse(200, "application/json", "{\"success\": true, \"message\": \"Đã xóa cookie Douyin\"}");
            return;
        }

        // ==========================================
        // 2. API KEYS CRUD & LIVE TEST
        // ==========================================
        if (path == "/api/keys" && method == "GET") {
            auto keys = ApiKeyPool::instance().getKeysJson();
            sendResponse(200, "application/json", keys.dump());
            return;
        }

        if (path == "/api/keys/add" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string provider = j.value("provider", "gemini");
            std::string key = j.value("key", "");
            std::string label = j.value("label", "");
            bool ok = ApiKeyPool::instance().addKey(provider, key, label);
            sendResponse(200, "application/json", ok ? "{\"success\": true}" : "{\"error\": \"Key đã tồn tại hoặc không hợp lệ\"}");
            return;
        }

        if (path == "/api/keys/delete" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string provider = j.value("provider", "gemini");
            std::string key = j.value("key", "");
            bool ok = ApiKeyPool::instance().deleteKey(provider, key);
            sendResponse(200, "application/json", ok ? "{\"success\": true}" : "{\"error\": \"Không tìm thấy key\"}");
            return;
        }

        if (path == "/api/keys/toggle" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string provider = j.value("provider", "gemini");
            std::string key = j.value("key", "");
            bool enabled = j.value("enabled", true);
            bool ok = ApiKeyPool::instance().toggleKey(provider, key, enabled);
            sendResponse(200, "application/json", ok ? "{\"success\": true}" : "{\"error\": \"Không tìm thấy key\"}");
            return;
        }

        if (path == "/api/keys/test" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string provider = j.value("provider", "gemini");
            std::string key = j.value("key", "");
            std::string msg;
            bool ok = ApiKeyPool::instance().testKey(provider, key, msg);
            nlohmann::json res;
            res["success"] = ok;
            res["message"] = msg;
            sendResponse(200, "application/json", res.dump());
            return;
        }

        // ==========================================
        // 3. TASKS & QUEUE CRUD
        // ==========================================
        if (path == "/api/tasks" && method == "GET") {
            std::string filter = query_params["filter"];
            std::string q = query_params["q"];
            auto tasks = PipelineManager::instance().getTasksJson(filter, q);
            sendResponse(200, "application/json", tasks.dump());
            return;
        }

        if (path == "/api/tasks/detail" && method == "GET") {
            std::string id = query_params["id"];
            auto detail = PipelineManager::instance().getTaskDetailJson(id);
            sendResponse(200, "application/json", detail.dump());
            return;
        }

        if (path == "/api/tasks/add" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string input = j.value("input", "");
            std::string type = j.value("type", "url");
            std::string lang = j.value("lang", "vi");
            std::string voice = j.value("voice", "vi-VN-HoaiMyNeural");
            std::string engine = j.value("tts_engine", "");
            std::string model = j.value("model", "gemini");
            std::string bgm = j.value("bgm", "keep_bgm");
            std::string blur_mode = j.value("blur_sub_mode", "");

            std::string naming_pattern = j.value("output_naming_pattern", "");
            std::string custom_name = j.value("custom_output_name", "");

            if (engine.empty()) {
                if (voice == "google") engine = "google";
                else if (voice == "vibi") engine = "vibi";
                else engine = "edge";
            }

            if (type == "url") {
                std::string cleaned = VideoDownloader::extractCleanUrl(input);
                if (!cleaned.empty()) input = cleaned;
            }

            std::string taskId = PipelineManager::instance().addTask(input, type, lang, voice, engine, model, bgm, blur_mode, naming_pattern, custom_name);
            nlohmann::json res;
            res["success"] = !taskId.empty();
            res["id"] = taskId;
            sendResponse(200, "application/json", res.dump());
            return;
        }

        if (path == "/api/tasks/batch" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::vector<std::string> links = j.value("links", std::vector<std::string>{});
            std::string lang = j.value("lang", "vi");
            std::string voice = j.value("voice", "vi-VN-HoaiMyNeural");
            std::string engine = j.value("tts_engine", "");
            std::string model = j.value("model", "gemini");
            std::string bgm = j.value("bgm", "keep_bgm");
            std::string blur_mode = j.value("blur_sub_mode", "");
            std::string naming_pattern = j.value("output_naming_pattern", "");
            std::string custom_name = j.value("custom_output_name", "");

            if (engine.empty()) {
                if (voice == "google") engine = "google";
                else if (voice == "vibi") engine = "vibi";
                else engine = "edge";
            }

            int added = 0;
            for (const auto& l : links) {
                std::string clean = VideoDownloader::extractCleanUrl(l);
                if (!clean.empty() && PipelineManager::instance().addTask(clean, "url", lang, voice, engine, model, bgm, blur_mode, naming_pattern, custom_name) != "") {
                    added++;
                }
            }
            nlohmann::json res;
            res["success"] = true;
            res["added"] = added;
            sendResponse(200, "application/json", res.dump());
            return;
        }

        if (path == "/api/tasks/pause" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("id", "");
            PipelineManager::instance().pauseTask(id);
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/tasks/resume" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("id", "");
            PipelineManager::instance().resumeTask(id);
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/tasks/retry" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("id", "");
            PipelineManager::instance().retryTask(id);
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/tasks/delete" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("id", "");
            bool delete_files = j.value("delete_files", false);
            PipelineManager::instance().deleteTask(id, delete_files);
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/tasks/delete_completed" && method == "POST") {
            PipelineManager::instance().deleteCompletedTasks();
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/tasks/clear_all" && method == "POST") {
            PipelineManager::instance().clearAllTasks();
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/tasks/update_srt" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("id", "");
            bool is_translated = j.value("is_translated", false);
            std::vector<SrtItem> subs;
            if (j.contains("subtitles")) {
                for (const auto& sj : j["subtitles"]) {
                    SrtItem it;
                    it.id = sj.value("id", 1);
                    it.start_time = sj.value("start_time", "00:00:00,000");
                    it.end_time = sj.value("end_time", "00:00:02,000");
                    it.text = sj.value("text", "");
                    it.translated_text = sj.value("translated_text", "");
                    subs.push_back(it);
                }
            }
            bool ok = PipelineManager::instance().updateTaskSrt(id, subs, is_translated);
            sendResponse(200, "application/json", ok ? "{\"success\": true}" : "{\"error\": \"Failed to update srt\"}");
            return;
        }

        if (path == "/api/tasks/retranslate_line" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("id", "");
            int line_id = j.value("line_id", 0);
            std::string target_lang = j.value("target_lang", "vi");
            std::string out_trans;
            bool ok = PipelineManager::instance().retranslateLine(id, line_id, target_lang, out_trans);
            nlohmann::json res;
            res["success"] = ok;
            res["translation"] = out_trans;
            sendResponse(200, "application/json", res.dump());
            return;
        }

        if (path == "/api/tasks/regenerate_voice_line" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string id = j.value("task_id", "");
            if (id.empty()) id = j.value("id", "");
            int line_id = j.value("line_id", 0);
            std::string text = j.value("text", "");
            std::string voice = j.value("voice", "");
            std::string engine = j.value("engine", "");

            std::string out_audio;
            bool ok = PipelineManager::instance().regenerateVoiceLine(id, line_id, text, voice, engine, out_audio);
            nlohmann::json res;
            res["success"] = ok;
            if (ok) {
                res["audio_url"] = "/preview_file?path=" + out_audio;
            } else {
                res["error"] = "Không thể tạo lại giọng đọc cho dòng này";
            }
            sendResponse(200, "application/json", res.dump());
            return;
        }

        if (path == "/api/tts_preview_line" && method == "POST") {
            nlohmann::json j = nlohmann::json::parse(body);
            std::string text = j.value("text", "");
            std::string voice = j.value("voice", "vi-VN-HoaiMyNeural");
            std::string engine = j.value("engine", "edge");

            std::filesystem::create_directories("temp");
            std::string out_mp3 = "temp/preview_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + ".mp3";
            bool ok = TtsEngine::generateSingleTts(text, out_mp3, engine, voice);
            nlohmann::json res;
            res["success"] = ok;
            if (ok) {
                res["audio_url"] = "/preview_file?path=" + out_mp3;
            }
            sendResponse(200, "application/json", res.dump());
            return;
        }

        if (path == "/preview_file" && (method == "GET" || method == "HEAD")) {
            std::string file_path = query_params["path"];
            std::string decoded = urlDecode(file_path);

            namespace fs = std::filesystem;
            fs::path p = fs::u8path(decoded);
            if (!fs::exists(p)) {
                // If not found as relative, try looking inside output_dir
                auto cfg = ConfigManager::instance().getConfig();
                fs::path p_out = fs::u8path(cfg.output_dir) / p.filename();
                if (fs::exists(p_out)) {
                    p = p_out;
                }
            }

            if (fs::exists(p) && !fs::is_directory(p)) {
                serveFileWithRange(sock, p, header_part, getMimeType(p.u8string()));
                return;
            }

            sendResponse(404, "text/plain", "File Not Found: " + decoded);
            return;
        }

        if (path == "/download_file" && (method == "GET" || method == "HEAD")) {
            std::string file_path = query_params["path"];
            std::string decoded = urlDecode(file_path);

            namespace fs = std::filesystem;
            fs::path p = fs::u8path(decoded);
            if (!fs::exists(p)) {
                auto cfg = ConfigManager::instance().getConfig();
                fs::path p_out = fs::u8path(cfg.output_dir) / p.filename();
                if (fs::exists(p_out)) {
                    p = p_out;
                }
            }

            if (fs::exists(p) && !fs::is_directory(p)) {
                std::string fname = p.filename().u8string();
                serveFileWithRange(sock, p, header_part, getMimeType(p.u8string()), fname);
                return;
            }

            sendResponse(404, "text/plain", "File Not Found: " + decoded);
            return;
        }

        if ((path == "/api/tasks/download_batch" || path == "/api/download_batch") && (method == "GET" || method == "HEAD")) {
            std::string scope = query_params["scope"];
            std::string ids_str = query_params["ids"];
            bool include_srt = (query_params["include_srt"] != "0" && query_params["include_srt"] != "false");

            auto all_tasks = PipelineManager::instance().getTasksJson();
            std::vector<std::pair<std::string, std::string>> files_to_pack;

            std::set<std::string> target_ids;
            if (!ids_str.empty()) {
                std::stringstream ss(ids_str);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    std::string dec = urlDecode(item);
                    if (!dec.empty()) target_ids.insert(dec);
                }
            }

            auto cfg = ConfigManager::instance().getConfig();
            for (const auto& t : all_tasks) {
                std::string state = t.value("state", "");
                if (state != "COMPLETED") continue;

                std::string tid = t.value("id", "");
                if (!target_ids.empty() && target_ids.find(tid) == target_ids.end()) {
                    continue;
                }

                std::string out_vid = t.value("output_video", "");
                if (out_vid.empty()) continue;

                namespace fs = std::filesystem;
                fs::path p_vid = fs::u8path(out_vid);
                if (!fs::exists(p_vid)) {
                    fs::path alt = fs::u8path(cfg.output_dir) / p_vid.filename();
                    if (fs::exists(alt)) p_vid = alt;
                }

                if (fs::exists(p_vid) && !fs::is_directory(p_vid)) {
                    std::string vid_name = p_vid.filename().u8string();
                    files_to_pack.push_back({p_vid.u8string(), vid_name});

                    if (include_srt) {
                        fs::path p_srt = p_vid;
                        p_srt.replace_extension(".srt");
                        if (fs::exists(p_srt)) {
                            files_to_pack.push_back({p_srt.u8string(), p_srt.filename().u8string()});
                        }
                    }
                }
            }

            // Fallback: If no tasks in queue or downloading all, also scan the output directory
            if (files_to_pack.empty() && (scope == "all" || target_ids.empty())) {
                namespace fs = std::filesystem;
                fs::path out_p = fs::u8path(cfg.output_dir);
                if (fs::exists(out_p) && fs::is_directory(out_p)) {
                    for (const auto& entry : fs::directory_iterator(out_p)) {
                        if (entry.is_regular_file() && entry.path().extension() == ".mp4") {
                            std::string fname = entry.path().filename().u8string();
                            files_to_pack.push_back({entry.path().u8string(), fname});
                            if (include_srt) {
                                fs::path srt_p = entry.path();
                                srt_p.replace_extension(".srt");
                                if (fs::exists(srt_p)) {
                                    files_to_pack.push_back({srt_p.u8string(), srt_p.filename().u8string()});
                                }
                            }
                        }
                    }
                }
            }

            if (files_to_pack.empty()) {
                sendResponse(404, "text/html; charset=utf-8", 
                    "<html><body style='background:#0b1120;color:#f87171;font-family:sans-serif;padding:40px;text-align:center;'>"
                    "<h2>⚠️ Không tìm thấy video nào đã hoàn thành để tải về</h2>"
                    "<p style='color:#94a3b8'>Vui lòng đợi các video render xong hoặc kiểm tra lại danh sách chọn.</p>"
                    "<br><a href='/' style='color:#38bdf8;text-decoration:none;'>⬅️ Quay lại Studio</a>"
                    "</body></html>");
                return;
            }

            // Clean up old temp zip files older than 1 hour
            namespace fs = std::filesystem;
            fs::create_directories("temp");
            try {
                auto now_time = fs::file_time_type::clock::now();
                for (const auto& entry : fs::directory_iterator("temp")) {
                    if (entry.is_regular_file() && entry.path().extension() == ".zip") {
                        std::string fname = entry.path().filename().string();
                        if (fname.rfind("VideoDubber_Completed_", 0) == 0) {
                            auto ftime = fs::last_write_time(entry);
                            if (std::chrono::duration_cast<std::chrono::hours>(now_time - ftime).count() >= 1) {
                                std::error_code ec;
                                fs::remove(entry.path(), ec);
                            }
                        }
                    }
                }
            } catch (...) {}

            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
            std::string manifest_path = "temp/batch_manifest_" + std::to_string(now_ms) + ".json";
            std::string zip_path = "temp/VideoDubber_Completed_" + std::to_string(now_ms) + ".zip";

            {
                nlohmann::json mj = nlohmann::json::array();
                for (const auto& f : files_to_pack) {
                    mj.push_back({{"path", f.first}, {"name", f.second}});
                }
                std::ofstream mf(manifest_path);
                mf << mj.dump(2);
            }

            std::string cmd = "python scripts/make_batch_zip.py \"" + manifest_path + "\" \"" + zip_path + "\"";
            int ret = ProcessRunner::execute(cmd);

            std::error_code ec;
            fs::remove(manifest_path, ec);

            if (ret != 0 || !fs::exists(fs::u8path(zip_path))) {
                sendResponse(500, "text/plain; charset=utf-8", "Lỗi tạo file ZIP đóng gói trên máy chủ.");
                return;
            }

            int count_vids = 0;
            for (const auto& f : files_to_pack) {
                if (f.second.rfind(".mp4") != std::string::npos) count_vids++;
            }
            if (count_vids == 0) count_vids = (int)files_to_pack.size();

            std::string zip_filename = "VideoDubber_Completed_" + std::to_string(count_vids) + "_Videos.zip";
            serveFileWithRange(sock, fs::u8path(zip_path), header_part, "application/zip", zip_filename);
            return;
        }

        if (path == "/api/open_output" && method == "POST") {
            auto cfg = ConfigManager::instance().getConfig();
            std::filesystem::create_directories(cfg.output_dir);
            std::string cmd = "explorer " + std::filesystem::absolute(cfg.output_dir).string();
            system(cmd.c_str());
            sendResponse(200, "application/json", "{\"success\": true}");
            return;
        }

        if (path == "/api/logs" && method == "GET") {
            auto logs = PipelineManager::instance().getRecentLogs(60);
            nlohmann::json j = logs;
            sendResponse(200, "application/json", j.dump());
            return;
        }

        // ==========================================
        // 4. STATIC FILE SERVING
        // ==========================================
        std::string rel_path = path;
        if (rel_path == "/" || rel_path.empty()) rel_path = "/index.html";

        std::string full_path = web_root_ + rel_path;
        namespace fs = std::filesystem;
        fs::path static_p = fs::u8path(full_path);
        if (!fs::exists(static_p)) {
            static_p = fs::u8path("ui" + rel_path);
        }

        if (fs::exists(static_p) && !fs::is_directory(static_p)) {
            serveFileWithRange(sock, static_p, header_part, getMimeType(static_p.u8string()));
            return;
        }

        sendResponse(404, "text/plain", "404 Not Found: " + path);
    } catch (const std::exception& ex) {
        std::cerr << "[HttpGuiServer] Exception handling route: " << ex.what() << "\n";
        sendResponse(400, "application/json", std::string("{\"error\": \"") + ex.what() + "\"}");
    }
}

} // namespace VideoDubber
