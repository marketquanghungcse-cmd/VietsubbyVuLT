#pragma once
#include <string>
#include <thread>
#include <atomic>

namespace VideoDubber {

class HttpGuiServer {
public:
    static HttpGuiServer& instance();

    bool start(int port = 8765, const std::string& web_root = "ui");
    void stop();
    bool isRunning() const { return is_running_; }

private:
    HttpGuiServer() = default;
    ~HttpGuiServer();

    void serverLoop();
    void handleClient(uintptr_t client_socket);

    int port_ = 8765;
    std::string web_root_ = "ui";
    std::atomic<bool> is_running_{false};
    std::thread server_thread_;
    uintptr_t listen_socket_ = 0;
};

} // namespace VideoDubber