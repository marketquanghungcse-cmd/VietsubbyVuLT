#include "ProcessRunner.h"
#include <windows.h>
#include <vector>
#include <iostream>

namespace VideoDubber {

HANDLE ProcessRunner::launchProcess(const std::string& command_line) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    ZeroMemory(&pi, sizeof(pi));

    // Chuyển đổi lệnh UTF-8 sang UTF-16 Wide Char chuẩn xác cho Windows
    int wlen = MultiByteToWideChar(CP_UTF8, 0, command_line.c_str(), -1, NULL, 0);
    if (wlen <= 0) return INVALID_HANDLE_VALUE;
    std::wstring wcmd(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, command_line.c_str(), -1, &wcmd[0], wlen);

    std::wstring full_cmd = L"cmd.exe /s /c \"" + std::wstring(wcmd.c_str()) + L"\"";
    std::vector<wchar_t> cmd_buf(full_cmd.begin(), full_cmd.end());
    cmd_buf.push_back(L'\0');

    if (!CreateProcessW(
        NULL,
        cmd_buf.data(),
        NULL,
        NULL,
        FALSE,
        CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
        NULL,
        NULL,
        &si,
        &pi
    )) {
        return INVALID_HANDLE_VALUE;
    }

    CloseHandle(pi.hThread);
    return pi.hProcess;
}

int ProcessRunner::waitProcess(HANDLE hProcess) {
    if (hProcess == NULL || hProcess == INVALID_HANDLE_VALUE) return -1;
    WaitForSingleObject(hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(hProcess, &exit_code);
    CloseHandle(hProcess);
    return static_cast<int>(exit_code);
}

std::vector<int> ProcessRunner::waitAll(std::vector<HANDLE>& handles) {
    std::vector<int> results(handles.size(), -1);
    if (handles.empty()) return results;

    const size_t batch_size = MAXIMUM_WAIT_OBJECTS; // Giới hạn WinAPI 64 handles / lần gọi
    for (size_t i = 0; i < handles.size(); i += batch_size) {
        size_t count = std::min(batch_size, handles.size() - i);
        std::vector<HANDLE> batch(handles.begin() + i, handles.begin() + i + count);

        WaitForMultipleObjects(static_cast<DWORD>(count), batch.data(), TRUE, INFINITE);

        for (size_t j = 0; j < count; ++j) {
            HANDLE h = batch[j];
            DWORD exit_code = 0;
            if (h != NULL && h != INVALID_HANDLE_VALUE) {
                GetExitCodeProcess(h, &exit_code);
                CloseHandle(h);
                results[i + j] = static_cast<int>(exit_code);
            }
        }
    }
    return results;
}

int ProcessRunner::execute(const std::string& command_line) {
    HANDLE h = launchProcess(command_line);
    if (h == NULL || h == INVALID_HANDLE_VALUE) return -1;
    return waitProcess(h);
}

} // namespace VideoDubber
