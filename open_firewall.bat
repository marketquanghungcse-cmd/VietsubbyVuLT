@echo off
chcp 65001 >nul
echo ================================================================
echo    MO PORT 8765 WINDOWS FIREWALL DE HOSTING CHO CAC MAY KHAC
echo ================================================================
echo.

netsh advfirewall firewall add rule name="VideoDubberPro" dir=in action=allow protocol=TCP localport=8765

if %errorlevel% equ 0 (
    echo.
    echo [THANH CONG] Da mo cong 8765 tren Tuong lua Windows!
    echo.
    echo Cac may tinh khac trong cung mang LAN / Wi-Fi co the truy cap ngay:
    echo --^> http://192.168.1.92:8765
    echo.
) else (
    echo.
    echo [CANH BAO] Can quyen Administrator!
    echo Vui long click chuot phai vao file "open_firewall.bat" va chon "Run as administrator".
    echo.
)
pause
