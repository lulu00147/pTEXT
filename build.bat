@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

:: ===== Bundled Android toolchain (tools/) =====
set "TOOLS=%~dp0tools"
set "NDK_PATH=%TOOLS%\android-ndk-r30"
set "BUILD_TOOLS=%TOOLS%\build-tools_r37"
set "CMAKE_PATH=%TOOLS%\cmake-4.5.0\bin\cmake.exe"
set "NINJA_PATH=%TOOLS%\ninja-1.13.2\ninja.exe"
set "JAVA_HOME=%TOOLS%\jdk-25.0.5.0.2+1"
set "PLATFORM=%TOOLS%\platform-37.2_r01\android.jar"
set "RELEASE_PROPS=release.properties"
set "STORE_FILE=release.keystore"
set "STORE_PASSWORD="
set "KEY_ALIAS=ptext"
set "KEY_PASSWORD="
if exist "%RELEASE_PROPS%" for /f "usebackq tokens=1,* delims==" %%A in ("%RELEASE_PROPS%") do set "%%A=%%B"

:: Derived paths
set "NDK_TOOLCHAIN=%NDK_PATH%\build\cmake\android.toolchain.cmake"
set "PATH=%JAVA_HOME%\bin;%TOOLS%\cmake-4.5.0\bin;%TOOLS%\ninja-1.13.2;%PATH%"

if not exist "%PLATFORM%" ( echo [ERROR] android.jar not found: %PLATFORM% & pause & exit )
if not exist "%CMAKE_PATH%" ( echo [ERROR] cmake not found: %CMAKE_PATH% & pause & exit )
if not exist "%NINJA_PATH%" ( echo [ERROR] ninja not found: %NINJA_PATH% & pause & exit )
if not exist "%NDK_TOOLCHAIN%" ( echo [ERROR] NDK toolchain not found: %NDK_TOOLCHAIN% & pause & exit )
if not exist "%JAVA_HOME%\bin\javac.exe" ( echo [ERROR] javac not found: %JAVA_HOME% & pause & exit )
if not exist "%BUILD_TOOLS%\aapt2.exe" ( echo [ERROR] aapt2 not found: %BUILD_TOOLS% & pause & exit )

set "STB_FILE=app\src\main\cpp\stb_truetype.h"
set "STB_URL=https://raw.githubusercontent.com/nothings/stb/9f755b604db3492df2cbeb39bf84b81e3e3932e3/stb_truetype.h"
set "STB_SHA256=428cfe93520f2aed5e413a220b0db5bba5b17e2ebe5a64e4a8efa9eeeb67ec5f"

echo [1/8] Checking stb_truetype...
set "STB_HASH="
if exist "%STB_FILE%" for /f "skip=1 tokens=* delims=" %%H in ('certutil -hashfile "%STB_FILE%" SHA256 2^>nul') do if not defined STB_HASH set "STB_HASH=%%H"
if /I "!STB_HASH!"=="%STB_SHA256%" goto stb_ready
if exist "%STB_FILE%" del /f /q "%STB_FILE%"
curl.exe -L --fail --silent --show-error -o "%STB_FILE%" "%STB_URL%"
if errorlevel 1 ( echo [ERROR] stb_truetype download failed & pause & exit )
set "STB_HASH="
for /f "skip=1 tokens=* delims=" %%H in ('certutil -hashfile "%STB_FILE%" SHA256 2^>nul') do if not defined STB_HASH set "STB_HASH=%%H"
if /I not "!STB_HASH!"=="%STB_SHA256%" ( del /f /q "%STB_FILE%" & echo [ERROR] stb_truetype checksum mismatch & pause & exit )
:stb_ready

echo [2/8] Building C code...
if exist "build_cpp" rmdir /s /q build_cpp
mkdir build_cpp
cd build_cpp

:: Tool paths are already prepended to PATH
"%CMAKE_PATH%" -DCMAKE_TOOLCHAIN_FILE="%NDK_TOOLCHAIN%" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-37 -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%cd%\..\build\lib\arm64-v8a" -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%" -G "Ninja" ../app/src/main/cpp
if %errorlevel% neq 0 ( echo [ERROR] CMake configure failed & cd .. & pause & exit )
"%CMAKE_PATH%" --build .
if %errorlevel% neq 0 ( echo [ERROR] C build failed & cd .. & pause & exit )
cd ..

echo [3/8] Compiling resources...
if not exist "build" mkdir build
%BUILD_TOOLS%\aapt2.exe compile --dir app\src\main\res -o build\res.zip
if %errorlevel% neq 0 ( echo [ERROR] Resource compile failed & pause & exit )

echo [4/8] Linking AndroidManifest...
%BUILD_TOOLS%\aapt2.exe link -o build\base.apk -I %PLATFORM% --manifest app\src\main\AndroidManifest.xml -R build\res.zip --auto-add-overlay --min-sdk-version 37 --target-sdk-version 37
if %errorlevel% neq 0 ( echo [ERROR] Resource link failed & pause & exit )

echo [5/8] Compiling Java code...
if not exist "build_java" mkdir build_java
%JAVA_HOME%\bin\javac.exe -d build_java --release 11 -classpath %PLATFORM% app\src\main\java\com\ptext\MainActivity.java
if %errorlevel% neq 0 ( echo [ERROR] Java compile failed & pause & exit )

echo [6/8] Generating DEX...
call %BUILD_TOOLS%\d8.bat --lib %PLATFORM% --output build build_java\com\ptext\*.class

echo [7/8] Packing APK...
%JAVA_HOME%\bin\jar.exe uf build\base.apk -C build lib\arm64-v8a\libptext.so
%JAVA_HOME%\bin\jar.exe uf build\base.apk -C build classes.dex

echo [8/8] Aligning and signing...
call %BUILD_TOOLS%\zipalign.exe -f 4 build\base.apk build\aligned.apk
if exist "!STORE_FILE!" if defined STORE_PASSWORD if defined KEY_PASSWORD (
    echo Signing with release keystore: !STORE_FILE!
    call %BUILD_TOOLS%\apksigner.bat sign --ks "!STORE_FILE!" --ks-pass "pass:!STORE_PASSWORD!" --ks-key-alias "!KEY_ALIAS!" --key-pass "pass:!KEY_PASSWORD!" --out pTEXT.apk build\aligned.apk
) else (
    echo No release.properties; signing with debug keystore.
    if not exist "debug.keystore" (
        echo First run, generating debug keystore...
        %JAVA_HOME%\bin\keytool.exe -genkey -v -keystore debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
    )
    call %BUILD_TOOLS%\apksigner.bat sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android --out pTEXT.apk build\aligned.apk
)
if %errorlevel% neq 0 ( echo [ERROR] APK signing failed & pause & exit )

echo Cleaning...
rmdir /s /q build
rmdir /s /q build_cpp
rmdir /s /q build_java
del /f /q pTEXT.apk.idsig

echo.
echo ==========================================
echo Build complete: pTEXT.apk
echo ==========================================
pause
