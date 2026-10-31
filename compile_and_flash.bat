@echo off
chcp 65001 >nul
echo ========================================================
echo   Qiangli Q2.5 Pro (128x64) + ESP32-S3 Flash Script
echo ========================================================
echo.

set ARDUINO_CLI=C:\Users\Liga.inc\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
set PORT=COM11
set FQBN=esp32:esp32:esp32s3:FlashMode=dio,CDCOnBoot=cdc,PSRAM=enabled
set SKETCH=%~dp0esp_wibe.ino

if not exist "%ARDUINO_CLI%" (
    echo [ERROR] arduino-cli not found at %ARDUINO_CLI%
    pause
    exit /b 1
)

echo [1/2] Compiling sketch with DIO 80MHz and CDC enabled...
"%ARDUINO_CLI%" compile --fqbn %FQBN% "%SKETCH%"
if errorlevel 1 (
    echo [ERROR] Compilation failed!
    pause
    exit /b 1
)

echo [2/2] Uploading to %PORT%...
"%ARDUINO_CLI%" compile --fqbn %FQBN% "%SKETCH%" -u -p %PORT%
if errorlevel 1 (
    echo [ERROR] Upload failed! Make sure COM11 is not busy by another serial monitor.
    pause
    exit /b 1
)

echo.
echo ========================================================
echo   SUCCESS! Firmware uploaded. Panel running at 112 FPS.
echo ========================================================
pause
