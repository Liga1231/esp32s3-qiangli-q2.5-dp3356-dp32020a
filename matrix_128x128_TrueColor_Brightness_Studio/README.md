# ESP32-S3 TrueColor & Tone Control Web Studio (Dual 128×128 Matrix @ 77 Hz Turbo)

[![Release](https://img.shields.io/badge/Release-v3.0.0%20TrueColor%2077Hz-blueviolet.svg)]()
[![Hardware](https://img.shields.io/badge/SoC-ESP32--S3%20Super%20Mini-blue.svg)]()
[![Display](https://img.shields.io/badge/Matrix-128x128%20(Qiangli%20Q2.5%20Pro)-green.svg)]()

> **English & Русский**  
> Firmware for dual **Qiangli Q2.5 Pro** ($128 \times 128$) LED matrix driven by **DP3356** (14-bit S-PWM) and **DP32020A** row switches on **ESP32-S3 Super Mini**.

---

## 🌐 Language Navigation / Выбор языка
* [🇷🇺 **Русская версия (Russian Version)**](#-русская-версия)
* [🇬🇧 **English Version**](#-english-version)

---

# 🇷🇺 Русская версия

Прошивка для светодиодного экрана $128 \times 128$ на базе двух панелей **Qiangli Q2.5 Pro** с чипами **DP3356** (14-bit S-PWM) и **DP32020A**, управляемая контроллером **ESP32-S3 Super Mini**.

Включает аппаратный турбо-движок прямого доступа к памяти **LCD_CAM + GDMA** на частоте **20.21 МГц** (стабильные **77.00 FPS / 77 Гц** при 0% нагрузки на CPU), физическое раскрытие ШИМ-окна чипа DP3356 (`0x04FF` + `0x0640` GCLK x1) и встроенную беспроводную **Wi-Fi Web Studio** с аппаратной регулировкой яркости экрана, полутонов, гаммы, резкости и 24-битного TrueColor цвета.

## 🌟 Основные возможности

### 1. Аппаратный движок отображения (LCD_CAM + GDMA 77 Гц Turbo)
- **Частота тактового сигнала (PCLK):** ровно **20.21 МГц** (`PLL160M / 8`, `lcd_clkm_div_num = 8`) на GPIO 41 через аппаратный блок `LCD_CAM` (i80 8-битная параллельная шина).
- **Частота кадров (FPS):** кварцевые **77.00 FPS** (период кадра 12.99 мс), абсолютное отсутствие мерцания, полос и разрывов на камеру при любой экспозиции.
- **Глубина цвета:** **14-бит S-PWM на канал (16 384 градации яркости)** со встроенной таблицей **Gamma 2.4**.
- **Оптическая яркость (Silicon Turbo):** физическое растяжение S-PWM импульса на все окно 200 мкс строки (`REG04 = 0x04FF`, `REG06 = 0x0640`, аналоговый гейн `REG08 = 0x08FF` 200%).
- **Нагрузка на CPU:** **0% на вывод кадров**. Кольцевой циклический буфер GDMA работает полностью автономно в оперативной памяти.

### 2. Веб-студия (Wi-Fi SoftAP + Web Studio)
- **Точка доступа:** SSID `matrix 128x128`, пароль `123456788`.
- **Адрес панели:** `http://192.168.4.1` (Single Page Application, упакованное в GZIP).
- **Аппаратный ползунок яркости экрана:** Master Brightness (5% – 100%) через WebSocket opcode `0x06` с мгновенным пересчетом гаммы и сохранением в LittleFS (`/boot_bright.txt`).
- **Протокол передачи:** WebSocket двоичный поток 24-bit RGB888 (Type `0x05`, 49 155 байт).
- **Режимы отображения:**
  - **`🌟 Истинный цвет (TrueColor 14-бит S-PWM)`** — максимальная фото-детализация без зернистости и постеризации.
  - **`🎨 Floyd-Steinberg HD`** — диффузное сглаживание полутонов с настройкой силы и зерна.
  - **`📐 Bayer 2×2 / 4×4`** — упорядоченный микро-дизеринг.
  - **`⚡ Порог 8 цветов`** — чистый ретро пиксель-арт.
- **Инструменты тоновой коррекции:**
  - **Яркость (Brightness):** от `-100%` до `+100%`.
  - **Контраст (Contrast):** от `-100%` до `+100%`.
  - **Гамма / Экспозиция (Gamma / Midtones):** от `0.40` до `2.60` (вытягивает тени без пересвета светлых зон).
  - **Насыщенность (Saturation):** от `-100%` (идеальное ч/б фото) до `+100%`.
  - **Четкость / Резкость (Sharpness / Clarity):** 0–100% (фильтр нерезкого маскирования для выделения мелких деталей на матрице 128×128).
  - **Уровень черного (Black Level):** отсечение шума и глубокий черный цвет светодиодов.
  - **Баланс белого (Color Temp):** теплый ↔ холодный спектр.
  - **🔴 Живой предпросмотр (Real-time Live Sync):** отображение изменений на физическом экране во время движения слайдеров (задержка 120 мс).
- **Энергонезависимость (LittleFS):**
  - Кнопка **«💾 Сохранить во Flash»** сохраняет полный 14-битный буфер (96 КБ) во Flash-память контроллера (`/boot_gray.bin`). Картинка восстанавливается сразу при включении питания.

## 🔌 Распиновка подключения (ESP32-S3 Super Mini)

| Сигнал матрицы | Пин ESP32-S3 | Назначение / Примечание |
| :--- | :--- | :--- |
| **R1** | `GPIO 4` | Верхняя половина (Красный 1) |
| **G1** | `GPIO 5` | Верхняя половина (Зеленый 1) |
| **B1** | `GPIO 6` | Верхняя половина (Синий 1) |
| **R2** | `GPIO 7` | Нижняя половина (Красный 2) |
| **G2** | `GPIO 15` | Нижняя половина (Зеленый 2) |
| **B2** | `GPIO 16` | Нижняя половина (Синий 2) |
| **CLK (DCLK)** | `GPIO 41` | Тактовая частота 20.21 МГц (LCD_PCLK) |
| **LAT (LE)** | `GPIO 40` | Защелка данных DP3356 / VSYNC |
| **OE** | `GPIO 2` | Разрешение выхода / переключение строк |
| **A (DCK)** | `GPIO 18` | Тактирование сдвига строк DP32020A / Бланкирование |
| **B (BK)** | `GPIO 8` | Постоянный LOW |
| **C (DIN)** | `GPIO 3` | Маркер начала кадра DP32020A (Строка 0) |
| **LED** | `GPIO 47` | Встроенный синий светодиод платы |

## 🛠️ Сборка и прошивка (Arduino CLI)

### 1. Параметры платы
- **Board:** `esp32:esp32:esp32s3`
- **USB CDC On Boot:** `Enabled` (`CDCOnBoot=cdc`)
- **Flash Mode:** **`DIO 80MHz`** (`FlashMode=dio`) — *Никогда не использовать QIO!*
- **PSRAM:** `Enabled` (`PSRAM=enabled`)
- **Flash Size:** `4MB` (`FlashSize=4M`)

### 2. Команды сборки и загрузки
```powershell
# Компиляция
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=dio,PSRAM=enabled,FlashSize=4M .

# Загрузка
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload -p COM11 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=dio,PSRAM=enabled,FlashSize=4M .
```

---

# 🇬🇧 English Version

Firmware for dual-panel **Qiangli Q2.5 Pro** ($128 \times 128$) LED matrix driven by **DP3356** (14-bit S-PWM) column drivers and **DP32020A** P-MOSFET row decoders, controlled by the **ESP32-S3 Super Mini**.

Features an autonomous hardware direct-memory-access engine (**LCD_CAM + GDMA**) clocked at **20.21 MHz** (rock-solid **77.00 FPS / 77 Hz** at 0% CPU rendering load), physical S-PWM window unrolling (`REG04 = 0x04FF` + `REG06 = 0x0640` GCLK x1), and an integrated wireless **Wi-Fi Web Studio** with hardware master screen brightness slider, gamma, contrast, sharpness, and 24-bit TrueColor image processing.

## 🌟 Key Features

### 1. Hardware Display Engine (LCD_CAM + GDMA 77 Hz Turbo)
- **Pixel Clock (PCLK / DCLK):** Exactly **20.21 MHz** (`PLL160M / 8`, `lcd_clkm_div_num = 8`) on GPIO 41 via hardware `LCD_CAM` (i80 8-bit parallel bus).
- **Refresh Rate (FPS):** Quartz-locked **77.00 FPS** (12.99 ms frame period), completely eliminating flicker and scanline artifacts on cameras under any exposure setting.
- **Color Depth:** **14-bit S-PWM per channel (16,384 grayscale levels)** with built-in **Gamma 2.4** calibration.
- **Optical Brightness (Silicon Turbo):** Stretches the S-PWM photon burst across the full $200.0\ \mu\text{s}$ row illumination window (`REG04 = 0x04FF`, `REG06 = 0x0640`, analog force current `REG08 = 0x08FF` 200%).
- **CPU Rendering Load:** **0%**. Autonomous circular GDMA descriptor ring operates entirely in RAM.

### 2. Web Studio (Wi-Fi SoftAP + Web Studio)
- **Access Point:** SSID `matrix 128x128`, Password `123456788`.
- **Web Interface URL:** `http://192.168.4.1/` (Single Page Application, compressed in GZIP).
- **Master Screen Brightness Slider:** Hardware Brightness (5% – 100%) via WebSocket opcode `0x06` with instantaneous gamma LUT recomputation and LittleFS persistence (`/boot_bright.txt`).
- **Binary Protocol:** WebSocket binary 24-bit RGB888 stream (Type `0x05`, 49,155 bytes).
- **Display Modes:**
  - **`🌟 TrueColor (14-bit S-PWM)`** — Maximum photo realism with smooth tonal ramps and zero banding.
  - **`🎨 Floyd-Steinberg HD`** — Error diffusion dithering with adjustable grain and intensity.
  - **`📐 Bayer 2×2 / 4×4`** — Ordered micro-dithering.
  - **`⚡ 8-Color Threshold`** — Clean retro pixel art.
- **Tone Correction Tools:**
  - **Brightness:** `-100%` to `+100%`.
  - **Contrast:** `-100%` to `+100%`.
  - **Gamma / Midtones:** `0.40` to `2.60` (extracts shadow detail without blowing out highlights).
  - **Saturation:** `-100%` (monochrome) to `+100%`.
  - **Sharpness / Clarity:** 0–100% ($3 \times 3$ unsharp mask to accentuate crisp details on $128 \times 128$).
  - **Black Level:** Noise cutoff for deep LED blacks.
  - **White Balance / Color Temp:** Warm ↔ Cool spectrum adjustment.
  - **🔴 Live Sync Preview:** Real-time matrix updating as sliders are moved (120 ms latency).
- **Non-Volatile Storage (LittleFS):**
  - **«💾 Save to Flash»** button permanently writes the 96 KB grayscale buffer to Flash (`/boot_gray.bin`). Restored automatically upon reboot.

## 🔌 Hardware Pinout (ESP32-S3 Super Mini)

| Matrix Signal | ESP32-S3 Pin | Function / Notes |
| :--- | :--- | :--- |
| **R1** | `GPIO 4` | Top half Red |
| **G1** | `GPIO 5` | Top half Green |
| **B1** | `GPIO 6` | Top half Blue |
| **R2** | `GPIO 7` | Bottom half Red |
| **G2** | `GPIO 15` | Bottom half Green |
| **B2** | `GPIO 16` | Bottom half Blue |
| **CLK (DCLK)** | `GPIO 41` | 20.21 MHz Pixel Clock (LCD_PCLK) |
| **LAT (LE)** | `GPIO 40` | DP3356 Latch / VSYNC |
| **OE** | `GPIO 2` | Output Enable / Row Switch Trigger |
| **A (DCK)** | `GPIO 18` | DP32020A Row Clock / Blanking |
| **B (BK)** | `GPIO 8` | Config Clock (Tied LOW) |
| **C (DIN)** | `GPIO 3` | DP32020A Row 0 Start Marker |
| **LED** | `GPIO 47` | Onboard blue status LED |

## 🛠️ Build and Flash (Arduino CLI)

### 1. Board Options
- **Board:** `esp32:esp32:esp32s3`
- **USB CDC On Boot:** `Enabled` (`CDCOnBoot=cdc`)
- **Flash Mode:** **`DIO 80MHz`** (`FlashMode=dio`) — *Never use QIO!*
- **PSRAM:** `Enabled` (`PSRAM=enabled`)
- **Flash Size:** `4MB` (`FlashSize=4M`)

### 2. Compile and Upload Commands
```powershell
# Compile
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" compile --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=dio,PSRAM=enabled,FlashSize=4M .

# Upload
& "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" upload -p COM11 --fqbn esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashMode=dio,PSRAM=enabled,FlashSize=4M .
```
