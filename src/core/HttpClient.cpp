#include "HttpClient.h"
#include <windows.h>
#include <winhttp.h>
#include <iostream>
#include <sstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace VideoDubber {

static std::wstring toWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

static std::string toUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    return result;
}

static bool parseUrl(const std::string& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& isHttps) {
    std::wstring wUrl = toWide(url);
    URL_COMPONENTS urlComp = { sizeof(urlComp) };
    urlComp.dwHostNameLength = (DWORD)-1;
    urlComp.dwUrlPathLength = (DWORD)-1;
    urlComp.dwExtraInfoLength = (DWORD)-1;

    if (!WinHttpCrackUrl(wUrl.c_str(), (DWORD)wUrl.length(), 0, &urlComp)) {
        return false;
    }

    host = std::wstring(urlComp.lpszHostName, urlComp.dwHostNameLength);
    path = std::wstring(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        path += std::wstring(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }
    port = urlComp.nPort;
    isHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);
    return true;
}

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers, int timeout_sec) {
    HttpResponse resp;
    std::wstring host, path;
    INTERNET_PORT port;
    bool isHttps = true;

    if (!parseUrl(url, host, path, port, isHttps)) {
        resp.error = "Invalid URL format: " + url;
        return resp;
    }

    HINTERNET hSession = WinHttpOpen(L"VideoDubberPro/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "Failed to open WinHttp session";
        return resp;
    }

    WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000);
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        resp.error = "Failed to connect to host: " + toUtf8(host);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    if (!hRequest) {
        resp.error = "Failed to open GET request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    std::wstring headerStr;
    for (const auto& [k, v] : headers) {
        headerStr += toWide(k) + L": " + toWide(v) + L"\r\n";
    }

    BOOL bResults = WinHttpSendRequest(hRequest, headerStr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerStr.c_str(), (DWORD)headerStr.length(), WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (bResults) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        resp.status_code = (int)dwStatusCode;

        DWORD dwDownloaded = 0;
        std::vector<char> buffer;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            std::vector<char> tempBuf(dwSize);
            if (WinHttpReadData(hRequest, tempBuf.data(), dwSize, &dwDownloaded)) {
                buffer.insert(buffer.end(), tempBuf.begin(), tempBuf.begin() + dwDownloaded);
            }
        } while (dwSize > 0);

        resp.body = std::string(buffer.begin(), buffer.end());
    } else {
        resp.error = "WinHttp request failed: " + std::to_string(GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

HttpResponse HttpClient::post(const std::string& url, const std::string& json_body, const std::map<std::string, std::string>& headers, int timeout_sec) {
    HttpResponse resp;
    std::wstring host, path;
    INTERNET_PORT port;
    bool isHttps = true;

    if (!parseUrl(url, host, path, port, isHttps)) {
        resp.error = "Invalid URL format: " + url;
        return resp;
    }

    HINTERNET hSession = WinHttpOpen(L"VideoDubberPro/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        resp.error = "Failed to open WinHttp session";
        return resp;
    }

    WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000);
    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        resp.error = "Failed to connect to host: " + toUtf8(host);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    DWORD dwFlags = isHttps ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, dwFlags);
    if (!hRequest) {
        resp.error = "Failed to open POST request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    std::wstring headerStr = L"Content-Type: application/json\r\n";
    for (const auto& [k, v] : headers) {
        if (k != "Content-Type") {
            headerStr += toWide(k) + L": " + toWide(v) + L"\r\n";
        }
    }

    DWORD bodyLen = (DWORD)json_body.length();
    LPVOID pData = bodyLen > 0 ? (LPVOID)json_body.c_str() : WINHTTP_NO_REQUEST_DATA;

    BOOL bResults = WinHttpSendRequest(hRequest, headerStr.c_str(), (DWORD)headerStr.length(), pData, bodyLen, bodyLen, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (bResults) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        resp.status_code = (int)dwStatusCode;

        DWORD dwDownloaded = 0;
        std::vector<char> buffer;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;
            std::vector<char> tempBuf(dwSize);
            if (WinHttpReadData(hRequest, tempBuf.data(), dwSize, &dwDownloaded)) {
                buffer.insert(buffer.end(), tempBuf.begin(), tempBuf.begin() + dwDownloaded);
            }
        } while (dwSize > 0);

        resp.body = std::string(buffer.begin(), buffer.end());
    } else {
        resp.error = "WinHttp POST request failed: " + std::to_string(GetLastError());
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
}

} // namespace VideoDubber