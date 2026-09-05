@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ========================================================
echo   VideoDubberPro - Tu dong tai AI Models (Whisper)
echo ========================================================
echo.

if not exist models mkdir models

if not exist "models\ggml-base.bin" (
    echo [1/2] Dang tai model ggml-base.bin (~141 MB)...
    powershell -Command "Invoke-WebRequest -Uri 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.bin' -OutFile 'models\ggml-base.bin'"
) else (
    echo [1/2] Model ggml-base.bin da ton tai.
)

if not exist "models\ggml-small.bin" (
    echo [2/2] Dang tai model ggml-small.bin (~465 MB)...
    powershell -Command "Invoke-WebRequest -Uri 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin' -OutFile 'models\ggml-small.bin'"
) else (
    echo [2/2] Model ggml-small.bin da ton tai.
)

echo.
echo [HOAN TAT] Tat ca AI Models da san sang trong thu muc models/!
pause
