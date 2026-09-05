@echo off
if not exist VideoDubberPro.exe (
    call build.bat
)
start VideoDubberPro.exe