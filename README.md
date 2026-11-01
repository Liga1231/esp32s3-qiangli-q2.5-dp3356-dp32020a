# ESP32-S3 Qiangli Q2.5 Pro LED Matrix Driver (DP3356 + DP32020A)

[![Release](https://img.shields.io/badge/Release-v3.0.0%20TrueColor%2077Hz-blueviolet.svg)]()
[![ESP32-S3](https://img.shields.io/badge/SoC-ESP32--S3%20Super%20Mini-blue.svg)](https://www.espressif.com/)
[![Panel](https://img.shields.io/badge/Panel-Qiangli%20Q2.5%20Pro%20(128x128)-green.svg)](https://www.qiangli.com/)
[![Driver](https://img.shields.io/badge/Driver-DP3356%2014--bit%20S--PWM-orange.svg)](https://www.depuw.com/)
[![Row Switch](https://img.shields.io/badge/Row%20Switch-DP32020A%20PMOS-red.svg)](https://www.depuw.com/)
[![FPS](https://img.shields.io/badge/Refresh-77.00%20FPS%20Zero--Flicker-brightgreen.svg)]()
[![AI-Developed](https://img.shields.io/badge/Code%20by-100%25%20Autonomous%20AI-purple.svg)]()

<p align="center">
  <img src="docs/panel_preview.jpg" width="800" alt="Qiangli Q2.5 Pro running on ESP32-S3" />
  <br>
  <em>Live capture of the Dual-Panel Qiangli Q2.5 Pro (128x128) running at 77.00 FPS with 24-bit TrueColor on ESP32-S3 Super Mini</em>
</p>

---

## 🌐 Language Navigation / Выбор языка
* 📖 **[Полное руководство по архитектуре и протоколу / Complete Hardware & Protocol Guide (HARDWARE_ARCHITECTURE_AND_PROTOCOL_GUIDE.md)](HARDWARE_ARCHITECTURE_AND_PROTOCOL_GUIDE.md)** — *Эталонный гайд для разработчиков и AI-агентов*
* [🇷🇺 **Русская версия (Russian Version)**](#-русская-версия)
  * [📡 Быстрое подключение к экрану / Wi-Fi](#-быстрое-подключение-к-экрану--wi-fi-credentials)
  * [🤖 Автономная разработка ИИ](#-100-автономная-разработка-искусственным-интеллектом-без-ручного-вмешательства)
  * [🌟 Ключевые возможности v3.0 TrueColor](#-ключевые-возможности-релиза-v30-truecolor-studio)
  * [📁 Обзор всех прошивок](#-обзор-всех-прошивок-в-репозитории)
  * [🛠️ Изменение количества панелей (1, 2, 4 панели)](#️-как-изменить-количество-панелей-1-2-4-панели)
  * [🔌 Распиновка подключения](#-распиновка-подключения-hub75--esp32-s3-super-mini)
  * [⚙️ Сборка и прошивка](#️-настройки-компиляции-и-прошивки)
* [🇬🇧 **English Version**](#-english-version)
  * [📡 Quick Wi-Fi Connection & Credentials](#-quick-wi-fi-connection--credentials)
  * [🤖 Autonomous AI Development](#-100-autonomous-ai-development-zero-human-code-touched)
  * [🌟 Key Features of Release v3.0 TrueColor](#-key-features-of-release-v30-truecolor-studio)
  * [📁 Repository Firmware Overview](#-repository-firmware-overview)
  * [🛠️ Configuring Screen Count (1, 2, 4 Panels)](#️-how-to-change-the-number-of-panels-1-2-4-panels)
  * [🔌 Wiring & HUB75 Pinout](#-wiring--hub75-pinout-hub75--esp32-s3-super-mini)
  * [⚙️ Build and Flash Settings](#️-build-and-flash-settings)

---

# 🇷🇺 Русская версия

## 📡 Быстрое подключение к экрану / Wi-Fi Credentials

При включении платы автоматически поднимается локальная Wi-Fi сеть:
* **Имя сети (SSID):** `matrix 128x128`
* **Пароль:** `123456788`
* **Адрес веб-студии:** **`http://192.168.4.1/`** (открывается в любом браузере на телефоне, планшете или ПК)
* **Автосохранение:** Кнопка **«💾 Сохранить»** в интерфейсе навсегда записывает рисунок и текущий уровень яркости экрана в энергонезависимую Flash-память LittleFS.

---

### 🤖 100% Автономная разработка искусственным интеллектом (Без ручного вмешательства)
> [!IMPORTANT]
> **Каждая строчка исходного кода, реверс-инжиниринг протокола Depuwei, математический расчёт таймингов и документация в данном репозитории были исследованы, написаны и отлажены на 100% полностью автономно искусственным интеллектом (Google DeepMind Antigravity / Gemini / ChatGPT Tandem Architecture).**
> 
> **Человек не прикасался к коду и не правил вручную ни одной строчки.** Роль человека-оператора сводилась исключительно к физическому наблюдению

---

## 🌟 Ключевые возможности релиза v3.0 TrueColor Studio

1. **Разгон частоты обновления до 77 Гц (77.00 FPS):**
   * Шина данных DCLK разогнана до **20.21 МГц** (`PLL160M / 8`, аппаратный блок `LCD_CAM`).
   * Буфер свечения PMOS-строк откалиброван под $200.0\ \mu\text{s}$ (`DWELL_BYTES = 4000`).
   * **Результат:** Любое мерцание боковым зрением полностью устранено. Картинка воспринимается глазом как монолитная матрица.

2. **Максимальная светоотдача кремния DP3356 (`REG04=0x04FF` + `REG06=0x0640`):**
   * В ходе глубоких физических экспериментов раскрыто поведение чипа: пачка ШИМ завершалась раньше времени, а умножитель PLL сжимал её при разгоне шины.
   * Связка `REG04=0x04FF` (удвоение окна активного ШИМ со 127 до 255) и `REG06=0x0640` (множитель GCLK $\times 1$ вместо $\times 3$) растянула световую пачку на всё окно строки, обеспечив максимальную оптическую мощность светодиодов.
   * Токовый ЦАП зафиксирован на аппаратном пределе $200\%$ (`REG08 = 0x08FF`).

3. **☀️ Аппаратная регулировка яркости экрана в веб-интерфейсе:**
   * В шапку Web Studio добавлен глобальный мастер-слайдер **«Яркость экрана (5% - 100%)»**.
   * При перетаскивании ползунка контроллер на лету масштабирует 14-битную гамма-таблицу через бинарный WebSocket-пакет `0x06` — экран плавно затухает или разгорается в реальном времени.
   * Настройка сохраняется в LittleFS (`/boot_bright.txt`) и восстанавливается при старте.

4. **24-битная TrueColor передача цвета:**
   * Поддержка несжатых кадров RGB888 (пакет `0x05`, 49 155 байт).
   * 16.7 миллионов отображаемых цветов без ступенек градиентов благодаря 14-битному аппаратному S-PWM ($16\,384$ градации серого на канал).
   * Встроенные фильтры: Гамма ($0.40 - 2.60$), Контраст, Насыщенность, Резкость (Unsharp Mask $3 \times 3$), Точка черного, Баланс белого и предпросмотр Live Sync.

---

## 📁 Обзор всех прошивок в репозитории

В репозитории представлены 3 готовые прошивки под разные конфигурации и задачи:

```text
esp_wibe/
├── matrix_128x128_TrueColor_Brightness_Studio/  <-- [ФЛАГМАН v3.0] Основная боевая прошивка (77 Гц)
├── matrix_128x128_WiFi_GDMA/                    <-- [СТУДИЙНЫЙ РЕЛИЗ v2.5] 50 FPS с сетевой синхронизацией
└── matrix_128x64_single/                        <-- [ОДИНОЧНАЯ v1.0] Для 1 панели (128x64, 99.4 FPS)
```

### 1. `matrix_128x128_TrueColor_Brightness_Studio` (Флагман / Основной релиз v3.0)
* **Разрешение:** $128 \times 128$ (2 панели каскадом, 16 384 пикселей, 49 152 LED-кристалла).
* **Частота развёртки:** **77.00 FPS / 77 Гц** (DCLK 20.21 МГц, нулевое мерцание).
* **Движок:** Аппаратный LCD_CAM + GDMA на чипе ESP32-S3 (0% нагрузки на CPU).
* **Цветность:** 24-бит TrueColor RGB888 (16.7M цветов) + 4 алгоритма дизеринга (Floyd-Steinberg HD, Bayer 2x2, Bayer 4x4, 8-Color).
* **Интерфейс:** Встроенная Web Studio, аппаратный ползунок «☀️ Яркость экрана» (5% – 100%), рисование, текст с автопереносом, фото-конвертер.
* **Память:** LittleFS Flash-хранилище (`/boot_gray.bin`, `/boot_bright.txt`, `/boot_mode.txt`).
* **Для кого:** Для повседневного использования как законченное готовое устройство с максимальной яркостью и частотой.

### 2. `matrix_128x128_WiFi_GDMA` (Базовый релиз v2.5)
* **Разрешение:** $128 \times 128$ (2 панели).
* **Частота развёртки:** **50.00 FPS** (DCLK 10.00 МГц).
* **Особенность:** Частота кадра математически выверена под 100 Гц пульсации сетевых блоков питания ($2 \times 50\text{ Гц}$). Разностная частота биений $F_{\text{beat}} = 0.00\text{ Гц}$ — полное отсутствие бегущих темных полос на камерах даже с простыми импульсными БП.
* **Для кого:** Для профессиональной видеосъёмки, студийных съёмок и вещания.

### 3. `matrix_128x64_single` (Одиночная панель v1.0)
* **Разрешение:** $128 \times 64$ (1 панель).
* **Частота развёртки:** **99.4 FPS**.
* **Особенность:** Автономный скетч без Wi-Fi стека со встроенной калибровочной заставкой-смайликом.
* **Для кого:** Для быстрой проверки работоспособности одиночной панели при распаковке и тестировании кабелей.

---

## 🛠️ Как изменить количество панелей (1, 2, 4 панели)

Конструкция легко масштабируется под любое количество модулей:

### Вариант А: Одиночная панель 128×64 (1 модуль)
1. **Физика:** 8 чипов DP3356 в каскаде на строку.
2. **Правки в коде скетча:**
   ```cpp
   #define MATRIX_WIDTH      128
   #define MATRIX_HEIGHT     64
   #define CHAINED_CHIPS     8      // 8 чипов на 1 панели (вместо 16)
   ```
3. **Буфер DMA:** На 8 чипов требуется 2048 байт сдвига (вместо 4096 байт):
   ```cpp
   static uint8_t s_shift_buf[ROWS][2048]; // 32 строки x 2048 байт
   ```
   В кольце дескрипторов `s_desc_shift_a` (2048 байт) сразу указывает на `s_desc_suffix`, а дескриптор `s_desc_shift_b` исключается.

---

### Вариант Б: Две панели 128×128 (Штатная конфигурация)
* 2 панели $128 \times 64$ установлены вертикально (Панель 1 — сверху, Панель 2 — снизу).
* Шлейф от ESP32 входит во **вход (INPUT) Панели 1**. Выход Панели 1 соединяется со **входом Панели 2**.
* В коде:
   ```cpp
   #define MATRIX_WIDTH      128
   #define MATRIX_HEIGHT     128
   #define CHAINED_CHIPS     16     // 8 чипов Панели 2 + 8 чипов Панели 1
   ```
   Сдвиг: $16 \text{ чипов} \times 16 \text{ каналов} \times 16 \text{ бит} = 4096\text{ байт}$ на строку (2 дескриптора по 2048 байт).

---

### Вариант В: Четыре панели 256×128 (Широкий экран)
* 2 верхние панели соединены горизонтально, 2 нижние — под ними.
* При последовательном соединении шлейфом `CHAINED_CHIPS = 32`:
   ```cpp
   #define MATRIX_WIDTH      256
   #define MATRIX_HEIGHT     128
   #define CHAINED_CHIPS     32
   ```
   Буфер сдвига строки: $32 \times 16 \times 16 = 8192\text{ байта}$ (4 дескриптора по 2048 байт). Для сохранения высокой частоты кадров шина DCLK работает на 20–25 МГц.

---

## 🔌 Распиновка подключения (HUB75 ➔ ESP32-S3 Super Mini)

| Пин HUB75 | Сигнал | Назначение | Пин ESP32-S3 | Примечание |
| :---: | :---: | :--- | :---: | :--- |
| **Pin 1** | **R1** | Красный (верхняя половина) | **GPIO 4** | Линия данных |
| **Pin 2** | **G1** | Зелёный (верхняя половина) | **GPIO 5** | Линия данных |
| **Pin 3** | **B1** | Синий (верхняя половина) | **GPIO 6** | Линия данных |
| **Pin 4** | **GND** | Общий провод (земля) | **GND** | Общая земля с БП |
| **Pin 5** | **R2** | Красный (нижняя половина) | **GPIO 7** | Линия данных |
| **Pin 6** | **G2** | Зелёный (нижняя половина) | **GPIO 15** | Линия данных |
| **Pin 7** | **B2** | Синий (нижняя половина) | **GPIO 16** | Линия данных |
| **Pin 8** | **GND** | Общий провод (земля) | **GND** | Общая земля с БП |
| **Pin 9** | **A** | DP32020A: Тактирование строки и гашение (**DCK**) | **GPIO 18** | Гашение и шаг PMOS |
| **Pin 10** | **B** | DP32020A: Конфигурационный такт (**BK/RCK**) | **GPIO 8** | Постоянный уровень LOW |
| **Pin 11** | **C** | DP32020A: Данные строчного регистра (**DIN**) | **GPIO 3** | Маркер строки 0 |
| **Pin 12** | **GND** | Общий провод (земля) | **GND** | Общая земля с БП |
| **Pin 13** | **CLK** | DP3356: Тактирование сдвига данных (**DCLK**) | **GPIO 41** | 20.21 МГц LCD_CAM |
| **Pin 14** | **LAT** | DP3356: Защёлка команд и данных (**LE**) | **GPIO 40** | Latch / VSYNC |
| **Pin 15** | **OE** | DP3356: Сигнал смены строки (**ROW**) | **GPIO 2** | Запуск S-PWM |
| **Pin 16** | **GND** | Общий провод (земля) | **GND** | Общая земля с БП |

> ⚠️ **ВАЖНО:** Вывод **GPIO 42 (MTMS)** на ESP32-S3 является системным выводом аппаратного режима загрузки (Strapping Pin). Он **ни в коем случае не должен подключаться к матрице**, иначе контроллер зависнет на старте в режиме JTAG.

---

## ⚙️ Настройки компиляции и прошивки

### Параметры платы в Arduino IDE:
* **Board:** `ESP32S3 Dev Module`
* **USB CDC On Boot:** **`Enabled`** *(Критично для работы виртуального COM-порта)*
* **Flash Mode:** **`DIO 80MHz`** *(Критично! В режиме QIO чип упадет в panic `ets_loader.c 78`)*
* **Flash Size:** `4MB (32Mb)`
* **Partition Scheme:** `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS/LittleFS)`
* **PSRAM:** **`QSPI PSRAM`** *(Обязательно для выделения кадровых буферов TrueColor)*
* **CPU Frequency:** `240MHz`

### Быстрая прошивка через скрипт `compile_and_flash.bat`:
В корне репозитория находится интерактивный батник:
```cmd
compile_and_flash.bat
```
Он позволяет в один клик выбрать нужную прошивку, скомпилировать её с правильными флагами PSRAM/DIO и прошить на плату.

---

# 🇬🇧 English Version

## 📡 Quick Wi-Fi Connection & Credentials

When the ESP32-S3 boots up, it automatically creates a dedicated Wi-Fi access point:
* **Network Name (SSID):** `matrix 128x128`
* **Password:** `123456788`
* **Web Studio Address:** **`http://192.168.4.1/`** (open in any web browser on iOS, Android, tablet, or PC)
* **Persistence:** The **«💾 Save»** button in the Web Studio permanently writes the artwork and master brightness level to non-volatile LittleFS Flash memory.

---

### 🤖 100% Autonomous AI Development (Zero Human Code Touched)
> [!IMPORTANT]
> **Every single line of source code, hardware timing calculation, register handshake sequence, and documentation in this repository was researched, written, and debugged 100% autonomously by AI (Google DeepMind Antigravity / Gemini / ChatGPT Tandem Architecture).**
> 
> **No human touched or manually edited any code or register bits.** The human user strictly functioned as the physical-world observer

---

## 🌟 Key Features of Release v3.0 TrueColor Studio

1. **Turbo Refresh Rate of 77 Hz (77.00 FPS):**
   * DCLK data bus overclocked to **20.21 MHz** (`PLL160M / 8`, hardware `LCD_CAM` peripheral).
   * Row PMOS dwell lighting window calibrated to $200.0\ \mu\text{s}$ (`DWELL_BYTES = 4000`).
   * **Result:** Peripheral eye flicker is 100% eliminated. The display appears as a rock-solid, monolithic OLED-like image.

2. **Maximum Silicon Optical Output (`REG04=0x04FF` + `REG06=0x0640`):**
   * In-depth physical hardware experimentation revealed driver physics: the S-PWM burst was concluding prematurely, and the internal PLL GCLK multiplier compressed it when DCLK speed was increased.
   * Pairing `REG04=0x04FF` (doubling active PWM counter window from 127 to 255) with `REG06=0x0640` (slowing internal GCLK PLL multiplier to $\times 1$ instead of $\times 3$) stretched the S-PWM photon burst across the full $200.0\ \mu\text{s}$ line window, achieving peak physical LED brightness.
   * Analog current DAC locked to maximum 200% force current (`REG08 = 0x08FF`).

3. **☀️ Hardware Screen Master Brightness Slider:**
   * Global master slider **«Screen Brightness (5% - 100%)»** integrated into the Web Studio header.
   * Moving the slider dynamically re-scales the 14-bit gamma lookup table via binary WebSocket opcode `0x06` — display brightness smoothly dims or brightens in real-time.
   * Preserved across reboots in LittleFS (`/boot_bright.txt`).

4. **24-bit TrueColor High-Fidelity Rendering:**
   * Uncompressed RGB888 streaming support (WebSocket packet `0x05`, 49,155 bytes).
   * 16.7 million visible colors with continuous tonal ramps driven by 14-bit hardware S-PWM ($16\,384$ shades per channel).
   * Real-time image processing pipeline: Gamma ($0.40 - 2.60$), Contrast, Saturation, Sharpness (Unsharp Mask $3 \times 3$), Black Level clipping, Color Temperature, and Live Sync preview.

---

## 📁 Repository Firmware Overview

The repository provides 3 distinct, production-ready firmwares:

```text
esp_wibe/
├── matrix_128x128_TrueColor_Brightness_Studio/  <-- [FLAGSHIP v3.0] Primary release (77 Hz TrueColor)
├── matrix_128x128_WiFi_GDMA/                    <-- [STUDIO RELEASE v2.5] 50 FPS camera-locked
└── matrix_128x64_single/                        <-- [STANDALONE v1.0] Single panel (128x64, 99.4 FPS)
```

### 1. `matrix_128x128_TrueColor_Brightness_Studio` (Flagship v3.0)
* **Resolution:** $128 \times 128$ (2 cascaded panels, 16,384 pixels, 49,152 LED emitters).
* **Refresh Rate:** **77.00 FPS / 77 Hz** (20.21 MHz DCLK, zero flicker).
* **Engine:** Hardware LCD_CAM + GDMA on ESP32-S3 (0% CPU load for rendering).
* **Color Depth:** 24-bit TrueColor RGB888 (16.7M colors) + 4 dithering modes (Floyd-Steinberg HD, Bayer 2x2, Bayer 4x4, 8-Color retro).
* **Web UI:** Embedded responsive Web Studio with hardware brightness slider, drawing canvas, multi-line text engine, and photo converter.
* **Storage:** LittleFS Flash persistence (`/boot_gray.bin`, `/boot_bright.txt`, `/boot_mode.txt`).
* **Target Audience:** Daily standalone use as a finished consumer-grade smart matrix display.

### 2. `matrix_128x128_WiFi_GDMA` (Studio Release v2.5)
* **Resolution:** $128 \times 128$ (2 panels).
* **Refresh Rate:** **50.00 FPS** (10.00 MHz DCLK).
* **Specialty:** Frame rate mathematically locked to 100 Hz ripple of 50 Hz AC mains ($2 \times 50\text{ Hz}$). Beat frequency $F_{\text{beat}} = 0.00\text{ Hz}$ guarantees zero rolling dark shutter bands on video cameras even when powered by unshielded switching power supplies.
* **Target Audience:** Studio video shoots, broadcasting, and camera benchmarking.

### 3. `matrix_128x64_single` (Single Panel Standalone v1.0)
* **Resolution:** $128 \times 64$ (1 panel).
* **Refresh Rate:** **99.4 FPS**.
* **Specialty:** Lightweight standalone sketch without Wi-Fi stack featuring a built-in calibration splash art.
* **Target Audience:** Rapid hardware verification when unpacking new panels or testing cables.

---

## 🛠️ How to Change the Number of Panels (1, 2, 4 Panels)

The cascaded DP3356 driver pipeline scales flexibly:

### Option A: Single Panel 128×64 (1 Module)
1. **Physics:** 8 cascaded DP3356 ICs per row line.
2. **Code adjustments:**
   ```cpp
   #define MATRIX_WIDTH      128
   #define MATRIX_HEIGHT     64
   #define CHAINED_CHIPS     8      // 8 chips per panel line (instead of 16)
   ```
3. **DMA Buffer:** 8 chips require 2048 shift bytes (instead of 4096):
   ```cpp
   static uint8_t s_shift_buf[ROWS][2048]; // 32 rows x 2048 bytes
   ```
   In the GDMA descriptor chain, `s_desc_shift_a` links directly to `s_desc_suffix`, omitting `s_desc_shift_b`.

---

### Option B: Dual Panels 128×128 (Default Configuration)
* 2 panels ($128 \times 64$ each) arranged vertically (Panel 1 on top, Panel 2 on bottom).
* Ribbon cable from ESP32 connects to **INPUT of Panel 1**. Output of Panel 1 connects to **INPUT of Panel 2**.
* In code:
   ```cpp
   #define MATRIX_WIDTH      128
   #define MATRIX_HEIGHT     128
   #define CHAINED_CHIPS     16     // 8 chips Panel 2 + 8 chips Panel 1
   ```
   Row shift payload: $16 \text{ chips} \times 16 \text{ channels} \times 16 \text{ bits} = 4096\text{ bytes}$ per row (2 GDMA descriptors of 2048 bytes each).

---

### Option C: Quad Panels 256×128 (Wide Display Wall)
* 2 top panels arranged horizontally, 2 bottom panels arranged beneath them.
* In daisy-chain topology `CHAINED_CHIPS = 32`:
   ```cpp
   #define MATRIX_WIDTH      256
   #define MATRIX_HEIGHT     128
   #define CHAINED_CHIPS     32
   ```
   Row shift payload: $32 \times 16 \times 16 = 8192\text{ bytes}$ (4 descriptors of 2048 bytes). DCLK should run at 20–25 MHz to maintain high refresh rate.
   **Power:** Quad panels require a **5V 40A–60A power supply** with dedicated 2.5 mm² power feeds to each panel.

---

## 🔌 Wiring & HUB75 Pinout (HUB75 ➔ ESP32-S3 Super Mini)

| HUB75 Pin | Signal | Description | ESP32-S3 Pin | Notes |
| :---: | :---: | :--- | :---: | :--- |
| **Pin 1** | **R1** | Red data (Top half) | **GPIO 4** | Parallel data line |
| **Pin 2** | **G1** | Green data (Top half) | **GPIO 5** | Parallel data line |
| **Pin 3** | **B1** | Blue data (Top half) | **GPIO 6** | Parallel data line |
| **Pin 4** | **GND** | Ground | **GND** | Common ground with PSU |
| **Pin 5** | **R2** | Red data (Bottom half) | **GPIO 7** | Parallel data line |
| **Pin 6** | **G2** | Green data (Bottom half) | **GPIO 15** | Parallel data line |
| **Pin 7** | **B2** | Blue data (Bottom half) | **GPIO 16** | Parallel data line |
| **Pin 8** | **GND** | Ground | **GND** | Common ground with PSU |
| **Pin 9** | **A** | DP32020A: Row Clock & Blanking (**DCK**) | **GPIO 18** | Blanking & PMOS shift |
| **Pin 10** | **B** | DP32020A: Config Clock (**BK/RCK**) | **GPIO 8** | Tied permanently LOW |
| **Pin 11** | **C** | DP32020A: Row Serial Data (**DIN**) | **GPIO 3** | Row 0 frame marker |
| **Pin 12** | **GND** | Ground | **GND** | Common ground with PSU |
| **Pin 13** | **CLK** | DP3356: Shift Clock (**DCLK**) | **GPIO 41** | 20.21 MHz LCD_CAM PCLK |
| **Pin 14** | **LAT** | DP3356: Latch Enable (**LE**) | **GPIO 40** | Latch / VSYNC pulse |
| **Pin 15** | **OE** | DP3356: Row Switch Trigger (**ROW**) | **GPIO 2** | Triggers S-PWM cycle |
| **Pin 16** | **GND** | Ground | **GND** | Common ground with PSU |

> ⚠️ **CRITICAL WARNING:** **GPIO 42 (MTMS)** on ESP32-S3 is an SoC hardware strapping pin. It **must NEVER be connected to any HUB75 pin**, or the board will hang on power-on in JTAG mode.

---

## ⚙️ Build and Flash Settings

### Arduino IDE Board Settings:
* **Board:** `ESP32S3 Dev Module`
* **USB CDC On Boot:** **`Enabled`** *(Mandatory for native USB serial connectivity)*
* **Flash Mode:** **`DIO 80MHz`** *(Mandatory! QIO causes `ets_loader.c 78` panic)*
* **Flash Size:** `4MB (32Mb)`
* **Partition Scheme:** `Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS/LittleFS)`
* **PSRAM:** **`QSPI PSRAM`** *(Mandatory for TrueColor double-buffering)*
* **CPU Frequency:** `240MHz`

### 1-Click Flashing via `compile_and_flash.bat`:
Run the batch helper in the repository root:
```cmd
compile_and_flash.bat
```
It prompts you to choose the target firmware, compiles it with the exact DIO/PSRAM/CDC flags, and uploads directly to the connected ESP32-S3 on COM11.

---

## 📄 Additional Documentation
* [FULL_PROJECT_REPORT_77HZ_TRUECOLOR.md](file:///c:/Users/Liga.inc/Desktop/esp_wibe/matrix_128x128_TrueColor_Brightness_Studio/FULL_PROJECT_REPORT_77HZ_TRUECOLOR.md) — Comprehensive technical report on DP3356 register physics, Kimi trials, and 77 Hz tuning.
* [ESP32_S3_SUPERMINI_CHEAT_SHEET.md](file:///c:/Users/Liga.inc/Desktop/esp_wibe/ESP32_S3_SUPERMINI_CHEAT_SHEET.md) — ESP32-S3 Super Mini hardware cheat sheet (boot panic fixes, pinout, upload resets).
* [matrix_128x128_TrueColor_Brightness_Studio/README.md](file:///c:/Users/Liga.inc/Desktop/esp_wibe/matrix_128x128_TrueColor_Brightness_Studio/README.md) — TrueColor Studio firmware details.

---

## License
Distributed under the **MIT License**.
Developed 100% autonomously by Google DeepMind Antigravity AI in tandem with human observation.
