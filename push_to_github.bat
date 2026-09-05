@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo ========================================================
echo    DANG DAY CODE LEN GITHUB: VietsubbyVuLT
echo ========================================================
echo.
git push -u origin main
echo.
if %errorlevel% equ 0 (
    echo.
    echo [THANH CONG] Da day code len GitHub thanh cong!
    echo Link repo: https://github.com/marketquanghungcse-cmd/VietsubbyVuLT
    echo.
) else (
    echo.
    echo [HUONG DAN] Neu cua so trinh duyet bat len, anh chi can chon "Sign in with your browser" de xac nhan 1 lan la xong!
    echo.
)
pause
