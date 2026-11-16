@echo off
setlocal
cd /d "%~dp0"
if not defined VCINSTALLDIR (
    echo [ERROR] Run this from a Visual Studio x64 developer prompt.
    exit /b 1
)
cl /nologo /O2 /I..\app\src\main\cpp test_utf8_gbk.c ..\app\src\main\cpp\encoding.c /Fe:test_utf8_gbk.exe
if errorlevel 1 exit /b 1
test_utf8_gbk.exe
set "RC=%errorlevel%"
del /q test_utf8_gbk.exe test_utf8_gbk.obj >nul 2>nul
exit /b %RC%
