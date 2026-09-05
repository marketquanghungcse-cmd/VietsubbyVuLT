@echo off
title VideoDubberPro - Setup Environment
cd /d "%~dp0"
echo ========================================================
echo    VideoDubberPro - Cai Dat Moi Truong Tu Dong
echo ========================================================
echo.

echo [1/4] Kiem tra Python...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [LOI] Khong tim thay Python! Vui long cai dat Python 3.10+ va tich vao "Add python.exe to PATH".
    pause
    exit /b 1
)
python --version

echo.
echo [2/4] Cai dat thu vien Python (requirements.txt)...
pip install -r requirements.txt
if %errorlevel% neq 0 (
    echo [CANH BAO] Co loi khi cai pip. Dang thu lai...
    pip install -r requirements.txt
)

echo.
echo [3/4] Cai dat trinh duyet Playwright Chromium (Tai Douyin khong lo chan)...
playwright install chromium

echo.
echo [4/4] Kiem tra FFmpeg...
ffmpeg -version >nul 2>&1
if %errorlevel% neq 0 (
    echo [THONG BAO] Chua co FFmpeg trong PATH he thong.
    echo Dang cai dat FFmpeg qua winget...
    winget install Gyan.FFmpeg --accept-package-agreements --accept-source-agreements
) else (
    echo FFmpeg da san sang!
)

echo.
echo [BO SUNG] Mo port Firewall Windows neu can truy cap noi bo LAN...
call open_firewall.bat >nul 2>&1

echo.
echo ========================================================
echo   HOAN TAT CAI DAT! BAN CO THE CHAY host_web.bat NGAY.
echo ========================================================
pause
