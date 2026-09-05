@echo off
chcp 65001 >nul
cd /d "%~dp0"
title VideoDubberPro - Web Studio Dashboard
echo ===================================================
echo   VideoDubberPro - Khởi Động Web Studio Dashboard
echo ===================================================
echo.
if not exist "temp" mkdir temp
if not exist "output" mkdir output
if not exist "config" mkdir config

echo Đang bật máy chủ VideoDubberPro...
start "" VideoDubberPro.exe
timeout /t 2 >nul
start http://127.0.0.1:8765
echo.
echo ===================================================
echo Web Studio đã khởi động tại: http://127.0.0.1:8765
echo ===================================================
