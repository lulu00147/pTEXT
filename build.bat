@echo off
setlocal enabledelayedexpansion

:: ===== Set these paths to your local Android SDK =====
set "SDK_ROOT=C:\Users\%USERNAME%\AppData\Local\Android\Sdk"
set "JAVA_HOME=C:\Users\%USERNAME%\.jdks\jbr-17.0.14"
set "NDK_VERSION=26.1.10909125"
set "CMAKE_VER=3.22.1"
set "BUILD_TOOLS_VER=34.0.0"
set "API_LEVEL=34"

:: Derived paths
set "NDK_PATH=%SDK_ROOT%\ndk\%NDK_VERSION%"
set "PLATFORM=%SDK_ROOT%\platforms\android-%API_LEVEL%\android.jar"
set "BUILD_TOOLS=%SDK_ROOT%\build-tools\%BUILD_TOOLS_VER%"
set "CMAKE_PATH=%SDK_ROOT%\cmake\%CMAKE_VER%\bin\cmake.exe"

if not exist "%PLATFORM%" ( echo [ERROR] android.jar not found. Check SDK_ROOT and API_LEVEL. & pause & exit )
if not exist "%CMAKE_PATH%" ( echo [ERROR] NDK cmake not found. Check NDK_VERSION. & pause & exit )

echo [1/6] Building C code...
if exist "build_cpp" rmdir /s /q build_cpp
mkdir build_cpp
cd build_cpp

:: Add the NDK-bundled ninja and cmake to PATH
set "PATH=%SDK_ROOT%\cmake\%CMAKE_VER%\bin;%PATH%"

%CMAKE_PATH% -DCMAKE_TOOLCHAIN_FILE=%NDK_PATH%/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=%cd%\..\build\lib\arm64-v8a -G "Ninja" ../app/src/main/cpp
if %errorlevel% neq 0 ( echo [ERROR] CMake configure failed & cd .. & pause & exit )
cmake --build .
if %errorlevel% neq 0 ( echo [ERROR] C build failed & cd .. & pause & exit )
cd ..

echo [2/6] Processing AndroidManifest and assets...
if not exist "res" mkdir res
%BUILD_TOOLS%\aapt2.exe compile --dir res -o build\res.zip
%BUILD_TOOLS%\aapt2.exe link -o build\base.apk -I %PLATFORM% --manifest app\src\main\AndroidManifest.xml --java gen -R build\res.zip -A app\src\main\assets --min-sdk-version 29 --target-sdk-version 34 -0 ttf

echo [3/6] Compiling Java code...
if not exist "build_java" mkdir build_java
%JAVA_HOME%\bin\javac.exe -d build_java --release 11 -classpath %PLATFORM% gen\com\ptext\R.java app\src\main\java\com\ptext\MainActivity.java
if %errorlevel% neq 0 ( echo [ERROR] Java compile failed & pause & exit )

echo [4/6] Generating DEX...
call %BUILD_TOOLS%\d8.bat --lib %PLATFORM% --output build build_java\com\ptext\*.class

echo [5/6] Packing APK...
%JAVA_HOME%\bin\jar.exe uf build\base.apk -C build lib\arm64-v8a\libptext.so
%JAVA_HOME%\bin\jar.exe uf build\base.apk -C build classes.dex

echo [6/6] Aligning and signing...
call %BUILD_TOOLS%\zipalign.exe -f 4 build\base.apk build\aligned.apk
if not exist "debug.keystore" (
    echo First run, generating debug keystore...
    %JAVA_HOME%\bin\keytool.exe -genkey -v -keystore debug.keystore -storepass android -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US"
)
call %BUILD_TOOLS%\apksigner.bat sign --ks debug.keystore --ks-pass pass:android --key-pass pass:android --out pTEXT.apk build\aligned.apk

echo Cleaning...
rmdir /s /q build
rmdir /s /q build_cpp
rmdir /s /q build_java
rmdir /s /q gen
rmdir /s /q res
del /f /q pTEXT.apk.idsig

echo.
echo ==========================================
echo Build complete: pTEXT.apk
echo ==========================================
pause
