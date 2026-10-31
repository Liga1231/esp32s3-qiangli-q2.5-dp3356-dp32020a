# ESP32-S3 Qiangli Q2.5 Pro LED Matrix Driver (DP3356 / DP3265S + DP32020A)

[![ESP32-S3](https://img.shields.io/badge/SoC-ESP32--S3%20Super%20Mini-blue.svg)](https://www.espressif.com/)
[![Panel](https://img.shields.io/badge/Panel-Qiangli%20Q2.5%20Pro%20(128x64)-green.svg)](https://www.qiangli.com/)
[![Driver](https://img.shields.io/badge/Driver-DP3356%20%7C%20DP3265S-orange.svg)](https://www.depuw.com/)
[![Row Switch](https://img.shields.io/badge/Row%20Switch-DP32020A%20PMOS-red.svg)](https://www.depuw.com/)
[![FPS](https://img.shields.io/badge/Refresh-99.4%20FPS%20Rock--Solid-brightgreen.svg)]()

<p align="center">
  <img src="docs/panel_preview.jpg" width="800" alt="Qiangli Q2.5 Pro running on ESP32-S3" />
  <br>
  <em>Live capture of the Qiangli Q2.5 Pro (128x64) running rock-solid on ESP32-S3 Super Mini</em>
</p>

> **English & Русский**  
> An ultra-stable, flicker-free, artifact-free driver firmware for **Qiangli Q2.5 Pro (Q2.5 S20B32 V1.1)** 128×64 1/32 scan LED panels powered by **Depuwei DP3356 / DP3265S** column drivers and **DP32020A** P-MOSFET row drivers on **ESP32-S3**.

---

## English Version

### 1. Overview & Key Hardware Specifications
* **LED Matrix Panel:** Qiangli Q2.5 Pro (`Q2.5 S20B32 V1.1`), 128×64 resolution, 1/32 scan.
* **Column Drivers (48 ICs):** Depuwei `DP3356` (functional equivalent to `DP3265S` / `DP3364S`), 16-channel constant-current S-PWM (Scrambled PWM) drivers with integrated PLL.
* **Row Drivers (4 ICs cascaded):** Depuwei `DP32020A` 8-channel integrated power P-MOSFET switches ($R_{DS(on)} = 110\text{ m}\Omega$, peak current $-3.5\text{ A}$) with built-in anti-ghosting circuitry.
* **Controller:** ESP32-S3 Super Mini (Dual-core 240 MHz Xtensa LX7, 4MB Flash DIO 80MHz, Native USB CDC on COM port).

---

### 2. Major Engineering Discoveries & Fixes

1. **Silicon Register Rejection & "One Register Per Frame" Rule (Depuwei Section 11.4):**
   * *Problem:* Standard initialization attempts to push all 13 configuration registers sequentially in `setup()`. The DP3356 internal state machine rejects commands 2..13, causing the driver to run in default Power-On Reset (POR) safe mode.
   * *Solution:* Registers must be updated **strictly one address per frame** across 13 frames.
2. **Double VSYNC Protocol Bug:**
   * *Problem:* In early code, `write_dp3356_reg()` generated VSYNC (`LAT=3`), followed immediately by a second `dp3364_vsync()`, resetting the configuration latch before row display data could be read.
   * *Solution:* Register configuration write replaces `dp3364_vsync()` in the frame header.
3. **Elimination of the 7.62 Hz Beat Flicker (One-Shot Init):**
   * *Problem:* Continuous round-robin updating of all 13 registers every frame causes phase glitches when rewriting PLL (`0x06`) and PWM mode (`0x0C`), producing a visible 7.62 Hz strobe flicker ($99.0\text{ FPS} / 13$).
   * *Solution:* **One-Shot Initialization**: all 13 registers are transmitted once during the first 13 frames (~130 ms). From frame 14 onwards, the controller holds a clean, lightweight VSYNC pulse (`LAT=3`).
4. **DCK Hardware Blanking Law ($T_{\text{shadow}} \ge 500\text{ ns}$):**
   * *Problem:* Dropping DCK to LOW during serial RGB data shifting turns on the P-MOSFET row keys while high-speed CLK is clocking, causing a 50% ghost echo above active pixels.
   * *Solution:* Per DP32020A Section 9, `DCK = HIGH` physically turns OFF all row PMOS switches. DCK is held HIGH during the entire serial shift, guaranteeing 0 mA dark blanking.
5. **ROW Clock Pulse on HUB75 Pin 15 (OE):**
   * On DP3356 S-PWM panels, pin 15 is re-purposed as `ROW`:
     - Line 0 requires pulse width $W12$ (12 DCLK cycles).
     - Lines 1..31 require pulse width $W4$ (4 DCLK cycles).

---

### 3. HUB75 Pinout for ESP32-S3 Super Mini

| HUB75 Pin | Signal Name | ESP32-S3 GPIO | Description |
|---|---|---|---|
| 1 | `R1` | **GPIO 4** | Top Half Red data |
| 2 | `G1` | **GPIO 5** | Top Half Green data |
| 3 | `B1` | **GPIO 6** | Top Half Blue data |
| 5 | `R2` | **GPIO 7** | Bottom Half Red data |
| 6 | `G2` | **GPIO 15** | Bottom Half Green data |
| 7 | `B2` | **GPIO 16** | Bottom Half Blue data |
| 9 | `A` (`DCK`) | **GPIO 18** | DP32020A Shift Clock & Hardware Blanking (HIGH = Blank) |
| 10 | `B` (`RCK`) | **GPIO 8** | DP32020A VR_UP Ghost Threshold (Held LOW) |
| 11 | `C` (`DIN`) | **GPIO 3** | DP32020A Row Serial Data (1 for Line 0, 0 for Lines 1..31) |
| 13 | `CLK` (`DCLK`) | **GPIO 41** | S-PWM Data / Configuration Clock |
| 14 | `LAT` (`LE`) | **GPIO 40** | Command Strobe (1=Data, 3=VSYNC, 5=WR_CFG, 14=PRE_ACT) |
| 15 | `OE` (`ROW`) | **GPIO 2** | S-PWM Row Counter Strobe ($W12$ on row 0, $W4$ on rows 1..31) |
| 4, 8, 12, 16 | `GND` | **GND** | Common Ground (MUST BE CONNECTED) |

> [!WARNING]
> Never drive **GPIO 42** HIGH on ESP32-S3 (tied to internal MTMS strapping / bootstrap).

---

### 4. Compilation & Flashing Instructions

#### Arduino CLI
```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashMode=dio,CDCOnBoot=cdc,PSRAM=enabled esp_wibe.ino -u -p COM11
```
*(Replace `COM11` with your device's native COM port).*

#### Arduino IDE 2.x Board Settings:
* **Board:** `ESP32S3 Dev Module`
* **Flash Mode:** `DIO 80MHz` (**NEVER USE QIO** — causes `ets_loader.c 78` boot panic!)
* **USB CDC On Boot:** `Enabled`
* **PSRAM:** `QSPI PSRAM`

---

## Русская версия (Russian Version)

### 1. Архитектура и аппаратные особенности
* **Матрица:** Qiangli Q2.5 Pro (`Q2.5 S20B32 V1.1`), разрешение 128×64, шаг сканирования 1/32.
* **Драйверы колонок (48 микросхем):** Depuwei `DP3356` (полный функциональный аналог `DP3265S` / `DP3364S`), 16-канальные драйверы постоянного тока с архитектурой S-PWM и встроенным умножителем частоты PLL.
* **Дешифраторы строк (4 каскадированных микросхемы):** Depuwei `DP32020A` со встроенными силовыми P-MOSFET ключами ($R_{DS(on)} = 110\text{ мОм}$, импульсный ток до $-3.5\text{ А}$) и активным подавлением паразитных емкостей.
* **Контроллер:** ESP32-S3 Super Mini (240 МГц, 4MB Flash DIO 80MHz, Native USB CDC).

---

### 2. Главные инженерные открытия и исправления проекта

1. **Закон «1 регистр на 1 кадр» (Depuwei DP3265S, раздел 11.4):**  
   Пачечная отправка всех 13 конфигурационных регистров в `setup()` блокируется кремнием: микросхема принимает только первый регистр, а остальные игнорирует. Регистры обязаны передаваться строго по одному за один VSYNC кадра.
2. **Устранение бага Double VSYNC:**  
   Функция `write_dp3356_reg()` аппаратно включает в себя строб `VSYNC` (`LAT=3`). Добавление внешнего `dp3364_vsync()` сбрасывало защёлку загруженного регистра. В прошивке отправка регистра заменяет собой одиночный VSYNC в заголовке кадра.
3. **Устранение мерцания 7.62 Гц (Алгоритм One-Shot):**  
   Непрерывная перезапись 13 регистров на каждом кадре вызывала паразитный микросдвиг фазы PLL (`0x06`) и сброс ШИМ-счётчиков (`0x0C`) на частоте биений $F_{\text{beat}} = 99\text{ FPS} / 13 = 7.62\text{ Гц}$. Прошивка переведена на **One-Shot инициализацию**: все 13 регистров загружаются один раз за первые 13 кадров (~130 мс), после чего контроллер выдает чистый легкий VSYNC.
4. **Закон аппаратного гашения DCK ($T_{\text{shadow}} \ge 500\text{ нс}$):**  
   По разделу 9 даташита DP32020A высокий уровень `DCK = HIGH` принудительно отключает все ключи P-MOSFET. Удержание DCK в HIGH на всё время программного сдвига данных гарантирует нулевой ток через диоды и полностью устраняет 50% фантомное двоение (эхо).
5. **Сигнал ROW на HUB75 Pin 15 (OE):**  
   Вывод 15 разъёма переназначен на тактирование внутреннего указателя строк DP3356:
   - Строка 0: строб $W12$ (12 тактов DCLK).
   - Строки 1..31: строб $W4$ (4 такта DCLK).

---

### 3. Замеры профилировщика стабильного базиса (Test 31B)

```text
[PROFILE] Frame: 10065.1 us | FPS: 99.4 | RGB/line: 274.95 us | RowChange: 1.61 us | PWM/line: 32.96 us | Sync: 1.58 us
```
* **Частота кадров:** стабильные **99.4 FPS** (джиттер $< 0.5\ \mu\text{s}$).
* **Качество изображения:** полное отсутствие бегущих полос на 32-й строке, отсутствие паразитного эха, чистота на камеру смартфона.
* **Напряжение шины питания:** 5.03 В без просадок, отсутствие нагрева драйверов и стабилизатора LM2596.

---

### 4. Дорожная карта Фазы 2 (LCD_CAM GDMA)

Текущая скважность свечения светодиодов при программном ногодрыге составляет:
$$\text{Duty Cycle} = \frac{32.96\ \mu\text{s}}{310.99\ \mu\text{s}} \approx 10.6\%$$
В Фазе 2 сдвиг RGB будет переведён с CPU на аппаратный параллельный интерфейс ESP32-S3 **`LCD_CAM` через GDMA (20 МГц)**:
* Время сдвига строки сократится с $275\ \mu\text{s}$ до **$6.4\ \mu\text{s}$**.
* Скважность свечения взлетит с $10.6\%$ до **$>80\%$**.
* Светимость матрицы вырастет в 7 раз до максимального уровня контроллеров NovaStar при холодном железе.

---

## License & Attribution
Distributed under the **MIT License**.  
Reverse-engineered, debugged, and verified through empirical hardware analysis on **ESP32-S3 Super Mini** + **Qiangli Q2.5 Pro**.
