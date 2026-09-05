#pragma once
#include <string>
#include <map>

namespace VideoDubber {

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;
};

class HttpClient {
public:
    static HttpResponse get(const std::string& url, const std::map<std::string, std::string>& headers = {}, int timeout_sec = 30);
    static HttpResponse post(const std::string& url, const std::string& json_body, const std::map<std::string, std::string>& headers = {}, int timeout_sec = 60);
};

} // namespace VideoDubber