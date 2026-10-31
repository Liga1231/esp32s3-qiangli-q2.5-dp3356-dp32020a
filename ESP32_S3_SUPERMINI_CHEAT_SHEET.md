# Шпаргалка: ESP32-S3 Super Mini (Характеристики, Баги и Решения)

> **Назначение:** Полный справочник по настройке, типичным проблемам, режимам прошивки и аппаратным особенностям платы **ESP32-S3 Super Mini**.

---

## 1. Аппаратные характеристики платы
* **Чип:** ESP32-S3 (QFN56, ревизия v0.2, 240 МГц, 2 ядра Xtensa LX7).
* **Flash-память:** 4 МБ встроенная (XMC).
* **PSRAM:** 2 МБ встроенная (AP_3v3, QSPI).
* **USB-интерфейс:** Native USB-Serial / JTAG встроен напрямую в чип (`VID: 303A`, `PID: 1001`). Внешнего чипа UART (CH340/CP2102) на плате **нет**.
* **Встроенный светодиод:** `GPIO 47` (синий, активный HIGH).
* **Кнопки:**
  * `B` / `0` — **BOOT** (`GPIO 0`).
  * `R` — **RESET** (`CHIP_PU` / `EN`).

---

## 2. Главные грабли и как их решать

### Проблема №1: «Пилик-пилик» Windows (вечный цикл переподключения USB)
* **Симптом:** Каждые 1.5–3 секунды Windows издает звук подключения и сразу отключения USB.
* **Причина:** Если во Flash-памяти нет валидной прошивки (затёрта или сбиты настройки), ROM-загрузчик выдает:
  ```text
  rst:0x7 (TG0WDT_SYS_RST), boot:0x2b (SPI_FAST_FLASH_BOOT)
  invalid header: 0xffffffff
  ```
  Срабатывает сторожевой таймер (`TG0WDT`), процессор перезагружается. Так как USB-контроллер находится внутри кристалла, при аппаратном ресете физически рвется USB-сессия.
* **Экстренная остановка цикла:**
  1. Зажать кнопку **BOOT** (`0`).
  2. Не отпуская `BOOT`, кратковременно нажать **RESET** (`R`).
  3. Отпустить **BOOT**.
  4. Чип переходит в режим загрузчика (`Download Mode`), звуки прекращаются, COM-порт висит стабильно.

---

### Проблема №2: Зависание после прошивки (`ets_loader.c 78`)
* **Причина:** Неверно выставлен режим Flash-памяти. Встроенная в чип память 4MB XMC требует режим **`DIO`**.
* **Решение:**
  * **Flash Mode:** всегда ставить **`DIO 80MHz`** (при выборе `QIO` плата не запустится и упадет в ошибку загрузчика).

---

### Проблема №3: Прошивка требует каждый раз зажимать кнопку BOOT
* **Причина:** В Arduino IDE по умолчанию в `platform.txt` для сброса прописан флаг `--before default_reset`. Этот метод дёргает линии DTR/RTS через внешний UART-чип, которого на Super Mini нет.
* **Решение:**
  1. В `platform.txt` ядра ESP32 использовать флаг сброса:
     `--before usb_reset`
  2. В настройках скетча обязательно компилировать с:
     **`USB CDC On Boot: Enabled`** (в коде инициализируется виртуальный COM-порт, отслеживающий сигнал прошивки).
  *(На текущем компьютере файлы конфигурации уже пропатчены: теперь прошивка идёт на 100% автоматически).*

---

### Проблема №4: В Library Manager в Arduino IDE ничего не ищется
1. Проверить фильтр **Type** (Тип) под поисковой строкой — он должен быть **`All`**, а не `Installed`.
2. Если зависла индексация базы: `Ctrl + Shift + P` → `Reload Window`.
3. Установка напрямую через CLI:
   ```powershell
   & "$env:LOCALAPPDATA\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe" lib install "ИмяБиблиотеки"
   ```

---

## 3. Точный эталон настроек в Arduino IDE
Для меню **Инструменты (Tools)**:

| Параметр | Значение | Примечание |
| :--- | :--- | :--- |
| **Board (Плата)** | `ESP32S3 Dev Module` | Базовый профиль |
| **Port (Порт)** | `COM11` (или текущий) | Устройство `USB JTAG/serial debug unit` |
| **USB CDC On Boot** | **`Enabled`** | **Критично!** Чтобы работал Serial Monitor |
| **Flash Mode** | **`DIO 80MHz`** | **Критично!** В QIO плата упадет в panic |
| **Flash Size** | **`4MB (32Mb)`** | Размер встроенной флешки |
| **PSRAM** | **`QSPI PSRAM`** | У чипа 2 МБ встроенной QSPI памяти |
| **Upload Mode** | `UART0 / Hardware CDC` | Стандартный режим |
| **USB DFU On Boot** | `Disabled` | Отключено |
| **CPU Frequency** | `240MHz (WiFi)` | Штатная частота |

---

## 4. Быстрый тестовый скетч (Blink + Serial)
```cpp
// Тестовый скетч для проверки ESP32-S3 Super Mini
#define LED_PIN 47

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_PIN, HIGH);
  delay(500);
  digitalWrite(LED_PIN, LOW);
  delay(500);
  Serial.println("ESP32-S3 SuperMini OK!");
}
```

---

## 5. Прошивка через консоль (esptool) вручную
Если когда-либо плата зависнет наглухо, одна команда всегда оживит её:
```powershell
python -m esptool --chip esp32s3 --port COM11 --before usb-reset --after hard-reset write-flash -z --flash-mode dio --flash-freq 80m --flash-size 4MB 0x0 bootloader.bin 0x8000 partitions.bin 0xe000 boot_app0.bin 0x10000 firmware.bin
```
