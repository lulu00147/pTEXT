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

:: Derived paths
set "NDK_TOOLCHAIN=%NDK_PATH%\build\cmake\android.toolchain.cmake"
set "PATH=%JAVA_HOME%\bin;%TOOLS%\cmake-4.5.0\bin;%TOOLS%\ninja-1.13.2;%PATH%"

if not exist "%PLATFORM%" ( echo [ERROR] android.jar not found: %PLATFORM% & pause & exit )
if not exist "%CMAKE_PATH%" ( echo [ERROR] cmake not found: %CMAKE_PATH% & pause & exit )
if not exist "%NINJA_PATH%" ( echo [ERROR] ninja not found: %NINJA_PATH% & pause & exit )
if not exist "%NDK_TOOLCHAIN%" ( echo [ERROR] NDK toolchain not found: %NDK_TOOLCHAIN% & pause & exit )
if not exist "%JAVA_HOME%\bin\javac.exe" ( echo [ERROR] javac not found: %JAVA_HOME% & pause & exit )
if not exist "%BUILD_TOOLS%\aapt2.exe" ( echo [ERROR] aapt2 not found: %BUILD_TOOLS% & pause & exit )

echo [1/6] Building C code...
if exist "build_cpp" rmdir /s /q build_cpp
mkdir build_cpp
cd build_cpp

:: Tool paths are already prepended to PATH
"%CMAKE_PATH%" -DCMAKE_TOOLCHAIN_FILE="%NDK_TOOLCHAIN%" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="%cd%\..\build\lib\arm64-v8a" -DCMAKE_MAKE_PROGRAM="%NINJA_PATH%" -G "Ninja" ../app/src/main/cpp
if %errorlevel% neq 0 ( echo [ERROR] CMake configure failed & cd .. & pause & exit )
"%CMAKE_PATH%" --build .
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
