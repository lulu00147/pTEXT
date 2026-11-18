@echo off
setlocal
cd /d "%~dp0"
if not defined VCINSTALLDIR (
    echo [ERROR] Run this from a Visual Studio x64 developer prompt.
    exit /b 1
)

cl /nologo /O1 /Zi /MD /fsanitize=address /I..\app\src\main\cpp ^
    test_glyph_blit.c /Fe:asan_glyph.exe
if errorlevel 1 exit /b 1
asan_glyph.exe
if errorlevel 1 exit /b 1

cl /nologo /O1 /Zi /MD /fsanitize=address /I..\app\src\main\cpp ^
    test_utf8_gbk.c ..\app\src\main\cpp\encoding.c /Fe:asan_utf8.exe
if errorlevel 1 exit /b 1
asan_utf8.exe
if errorlevel 1 exit /b 1

del /q asan_glyph.exe asan_glyph.pdb asan_utf8.exe asan_utf8.pdb ^
       test_glyph_blit.obj test_utf8_gbk.obj encoding.obj vc140.pdb >nul 2>nul
exit /b 0
