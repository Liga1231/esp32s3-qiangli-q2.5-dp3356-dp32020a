@echo off
chcp 65001 >nul
echo ========================================================
echo   Matrix Studio Pro (ESP32-S3 + Qiangli Q2.5 Pro)
echo ========================================================
echo.
echo Select project to compile and upload:
echo   [1] 128x128 TrueColor Studio 77Hz (WiFi + Brightness Slider) [Default]
echo   [2] 128x128 WiFi GDMA 50FPS (Classic Baseline)
echo   [3] Single-Panel 128x64 Standalone (Splash Screen 99.4 FPS)
echo   [4] Hardware Benchmark & Silicon Trial (Serial Terminal)
echo.

set CHOICE=1
set /p CHOICE="Enter choice [1-4, default=1]: "

if "%CHOICE%"=="2" (
    set SKETCH=%~dp0matrix_128x128_WiFi_GDMA\matrix_128x128_WiFi_GDMA.ino
    echo [Selected] Dual-Panel 128x128 WiFi GDMA (50 FPS)
) else if "%CHOICE%"=="3" (
    set SKETCH=%~dp0matrix_128x64_single\matrix_128x64_single.ino
    echo [Selected] Single-Panel 128x64 Standalone
) else if "%CHOICE%"=="4" (
    set SKETCH=%~dp0lcd_cam_dma_trial\lcd_cam_dma_trial.ino
    echo [Selected] Hardware Benchmark & Silicon Trial
) else (
    set SKETCH=%~dp0matrix_128x128_TrueColor_Brightness_Studio\matrix_128x128_TrueColor_Brightness_Studio.ino
    echo [Selected] 128x128 TrueColor Studio 77Hz (Flagship)
)

set ARDUINO_CLI=C:\Users\Liga.inc\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe
set PORT=COM11
set FQBN=esp32:esp32:esp32s3:FlashMode=dio,CDCOnBoot=cdc,PSRAM=enabled,FlashSize=4M

if not exist "%ARDUINO_CLI%" (
    echo [ERROR] arduino-cli not found at %ARDUINO_CLI%
    pause
    exit /b 1
)

echo.
echo [1/2] Compiling sketch with DIO 80MHz, CDC enabled and QSPI PSRAM...
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
echo   UPLOAD SUCCESSFUL! Enjoy your LED Matrix!
echo ========================================================
pause
