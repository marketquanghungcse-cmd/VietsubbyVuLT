@echo off
setlocal
echo ====================================================
echo   Building VideoDubberPro (MSVC x64 Release)
echo ====================================================

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist build mkdir build
cd build
"%CMAKE_EXE%" .. -DCMAKE_BUILD_TYPE=Release
"%CMAKE_EXE%" --build . --config Release

if %errorlevel% neq 0 (
    echo [ERROR] Build Failed!
    exit /b %errorlevel%
)

echo [SUCCESS] Build Finished!
copy Release\VideoDubberPro.exe ..\VideoDubberPro.exe >nul 2>&1
if not exist ..\VideoDubberPro.exe copy VideoDubberPro.exe ..\VideoDubberPro.exe >nul 2>&1
cd ..
echo Ready to run: VideoDubberPro.exe