#include <iostream>
#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <thread>
#include <chrono>
#include <atomic>
#include "core/Config.h"
#include "core/ApiKeyPool.h"
#include "core/PipelineManager.h"
#include "ui/HttpGuiServer.h"

static std::atomic<bool> g_app_running{true};

BOOL WINAPI ConsoleCtrlHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
        g_app_running = false;
        VideoDubber::HttpGuiServer::instance().stop();
        VideoDubber::PipelineManager::instance().stop();
        return TRUE;
    }
    return FALSE;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    try {
        char exe_buf[MAX_PATH];
        GetModuleFileNameA(NULL, exe_buf, MAX_PATH);
        std::filesystem::path exe_dir = std::filesystem::path(exe_buf).parent_path();
        std::filesystem::current_path(exe_dir);

        std::cout << "=======================================================" << std::endl;
        std::cout << "   VideoDubberPro - C++ Video Dubbing & Translation    " << std::endl;
        std::cout << "=======================================================" << std::endl;

        auto& cfgMgr = VideoDubber::ConfigManager::instance();
        auto cfg = cfgMgr.getConfig();

        VideoDubber::ApiKeyPool::instance().loadKeysFromConfig();
        auto& pipeline = VideoDubber::PipelineManager::instance();
        pipeline.log("Khoi dong he thong VideoDubberPro tai: " + exe_dir.string());

        auto& server = VideoDubber::HttpGuiServer::instance();
        if (server.start(cfg.server_port, "ui")) {
            std::string url = "http://127.0.0.1:" + std::to_string(cfg.server_port);
            std::cout << "\n[INFO] Dang mo Dashboard tai: " << url << std::endl;
            ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
        } else {
            std::cerr << "[ERROR] Khong the khoi dong GUI Server tren cong " << cfg.server_port << std::endl;
        }

        while (g_app_running && server.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        pipeline.stop();
        server.stop();
        std::cout << "Da dung ung dung an toan." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[CRITICAL ERROR] Ngoại lệ chưa bắt: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[CRITICAL ERROR] Ngoại lệ không xác định!" << std::endl;
    }
    return 0;
}
