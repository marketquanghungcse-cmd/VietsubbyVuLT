@echo off
chcp 65001 >nul
cd /d "%~dp0"
title Dang nhap tai khoan Douyin - VideoDubberPro
echo =======================================================
echo        TIEN ICH DANG NHAP TAI KHOAN DOUYIN
echo =======================================================
echo Dang khoi dong trinh duyet...
python scripts\douyin_login.py
pause
