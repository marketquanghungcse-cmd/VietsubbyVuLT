@echo off
chcp 65001 >nul
cd /d "%~dp0"
title VideoDubberPro - Cài Đặt Môi Trường Máy Mới
echo ========================================================
echo    VideoDubberPro - CÀI ĐẶT TỰ ĐỘNG CHO MÁY MỚI
echo ========================================================
echo.

echo [1/5] Kiểm tra Python...
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [THÔNG BÁO] Chưa tìm thấy Python. Đang thử tự động cài Python 3.11 qua winget...
    winget install Python.Python.3.11 --accept-package-agreements --accept-source-agreements
    echo.
    echo Nếu vừa cài xong, vui lòng ĐÓNG cửa sổ này và MỞ LẠI để nhận diện Python!
    pause
    exit /b 1
)
python --version

echo.
echo [2/5] Cài đặt thư viện Python cần thiết...
pip install -r requirements.txt
if %errorlevel% neq 0 (
    echo Đang thử cài lại với tùy chọn tương thích...
    pip install -r requirements.txt --user
)

echo.
echo [3/5] Cài đặt trình duyệt Playwright Chromium (Tải Douyin chống chặn)...
playwright install chromium

echo.
echo [4/5] Kiểm tra mô hình Whisper...
if not exist "models\ggml-base.bin" (
    echo Mô hình Whisper chưa có, đang tải mô hình tự động...
    call download_models.bat
) else (
    echo Đã có sẵn mô hình Whisper trong thư mục models!
)

echo.
echo [5/5] Mở port tường lửa Windows cho Web Studio...
call open_firewall.bat >nul 2>&1

echo.
echo ========================================================
echo   🎉 HOÀN TẤT CÀI ĐẶT 100%!
echo   Bây giờ bạn có thể nhấp đúp:
echo   👉 "2_CHAY_WEB_STUDIO.bat" để mở giao diện làm việc
echo   👉 "3_TAO_PUBLIC_LINK_CLOUDFLARE.bat" để lấy link public
echo ========================================================
pause
