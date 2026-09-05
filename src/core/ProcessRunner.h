#pragma once
#include <string>
#include <vector>

#ifndef _WINDEF_
typedef void *HANDLE;
#endif

namespace VideoDubber {

class ProcessRunner {
public:
    static int execute(const std::string& command_line);

    // OPT-8: WinAPI Async Process Execution
    static HANDLE launchProcess(const std::string& command_line);
    static int waitProcess(HANDLE hProcess);
    static std::vector<int> waitAll(std::vector<HANDLE>& handles);
};

} // namespace VideoDubber
