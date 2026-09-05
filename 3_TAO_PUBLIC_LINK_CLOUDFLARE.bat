@echo off
chcp 65001 >nul
cd /d "%~dp0"
title VideoDubberPro - Cloudflare Public Tunnel
echo ===================================================
echo   VideoDubberPro - Tạo Public Link Truy Cập Từ Xa
echo ===================================================
echo.
echo Đang kết nối Cloudflare Tunnel tới port 8765...
echo Vui lòng đợi 3-5 giây để link https://...trycloudflare.com xuất hiện bên dưới:
echo ===================================================
bin\cloudflared.exe tunnel --url http://127.0.0.1:8765 --no-autoupdate
pause
