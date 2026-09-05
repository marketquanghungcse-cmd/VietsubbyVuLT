@echo off
chcp 65001 >nul
cd /d "%~dp0"
title Quản lý Đăng nhập Douyin (Cookie Pool) - VideoDubberPro

:menu
cls
echo =======================================================
echo    QUẢN LÝ ĐĂNG NHẬP DOUYIN (COOKIE POOL ĐA TÀI KHOẢN)
echo =======================================================
echo.
echo [1] Đăng nhập / Cập nhật Tài khoản chính (#1)
echo [2] Đăng nhập thêm Tài khoản phụ (#2) [Khuyên dùng]
echo [3] Đăng nhập thêm Tài khoản phụ (#3)
echo [4] Đăng nhập thêm Tài khoản phụ (#4)
echo [5] Xem danh sách các tài khoản Douyin đã kết nối
echo [0] Thoát
echo.
echo =======================================================
set /p opt="Vui lòng chọn thao tác (1/2/3/4/5/0): "

if "%opt%"=="1" goto slot1
if "%opt%"=="2" goto slot2
if "%opt%"=="3" goto slot3
if "%opt%"=="4" goto slot4
if "%opt%"=="5" goto list_acc
if "%opt%"=="0" exit /b 0

echo Lựa chọn không hợp lệ.
timeout /t 2 >nul
goto menu

:slot1
echo.
echo Đang mở trình duyệt đăng nhập Tài khoản #1...
python scripts\douyin_login.py 1
echo.
pause
goto menu

:slot2
echo.
echo Đang mở trình duyệt đăng nhập Tài khoản phụ #2...
python scripts\douyin_login.py 2
echo.
pause
goto menu

:slot3
echo.
echo Đang mở trình duyệt đăng nhập Tài khoản phụ #3...
python scripts\douyin_login.py 3
echo.
pause
goto menu

:slot4
echo.
echo Đang mở trình duyệt đăng nhập Tài khoản phụ #4...
python scripts\douyin_login.py 4
echo.
pause
goto menu

:list_acc
echo.
echo =======================================================
echo   DANH SÁCH FILE COOKIE TRONG THƯ MỤC CONFIG/:
echo =======================================================
dir /b config\douyin_cookies*.json 2>nul
echo.
python -c "import glob, os; files = sorted(glob.glob('config/douyin_cookies*.json')); print(f'>> Tổng cộng: {len(files)} tài khoản Douyin đã kết nối vào Cookie Pool!')"
echo =======================================================
echo.
pause
goto menu
