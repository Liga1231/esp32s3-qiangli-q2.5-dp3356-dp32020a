@echo off
chcp 65001 >nul
echo ========================================================
echo   Matrix Studio Pro (ESP32-S3 + Qiangli Q2.5 Pro)
echo ========================================================
echo.
echo Select project to compile and upload:
echo   [1] Dual-Panel 128x128 Web Studio (WiFi + LittleFS) [Default]
echo   [2] Single-Panel 128x64 Standalone (Splash Screen)
echo.

set CHOICE=1
set /p CHOICE="Enter choice [1 or 2, default=1]: "

if "%CHOICE%"=="2" (
    set SKETCH=%~dp0matrix_128x64_single\matrix_128x64_single.ino
    echo [Selected] Single-Panel 128x64 Standalone
) else (
    set SKETCH=%~dp0matrix_128x128_WiFi\matrix_128x128_WiFi.ino
    echo [Selected] Dual-Panel 128x128 WiFi Web Studio
)

set ARDUINO_CLI=C:\Users\Liga.inc\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
set PORT=COM11
set FQBN=esp32:esp32:esp32s3:FlashMode=dio,CDCOnBoot=cdc,PSRAM=enabled

if not exist "%ARDUINO_CLI%" (
    echo [ERROR] arduino-cli not found at %ARDUINO_CLI%
    pause
    exit /b 1
)

echo.
echo [1/2] Compiling sketch with DIO 80MHz and CDC enabled...
"%ARDUINO_CLI%" compile --fqbn %FQBN% "%SKETCH%"
if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo.
echo [2/2] Uploading to %PORT%...
"%ARDUINO_CLI%" compile --fqbn %FQBN% "%SKETCH%" -u -p %PORT%
if errorlevel 1 (
    echo [ERROR] Upload failed! Make sure %PORT% is not busy by another serial monitor.
    pause
    exit /b 1
)

echo.
echo ========================================================
echo   SUCCESS! Firmware uploaded successfully.
echo ========================================================
pause
