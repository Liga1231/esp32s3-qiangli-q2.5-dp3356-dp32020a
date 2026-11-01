# AGENT HANDOFF: ESP32-S3 Matrix 128×128 + Wi-Fi Web Studio + LCD_CAM/GDMA

**Дата:** 1 ноября 2026 г.  
**Статус:** объединённая прошивка успешно компилируется; аппаратное тестирование этой конкретной версии ещё не выполнялось.  
**Платформа:** ESP32-S3 Super Mini, `ESP32-S3FH4R2`, 4 MB Flash, 2 MB QSPI PSRAM.  
**Матрица:** две Qiangli Q2.5 Pro, суммарно 128×128, 1/32 scan.  
**Драйверы:** DP3356 + DP32020A.  
**Arduino Core:** ESP32 Arduino 2.0.17.

---

## 1. Главный результат

Создана отдельная прошивка:

```text
matrix_128x128_WiFi_GDMA/matrix_128x128_WiFi_GDMA.ino
```

Она объединяет:

- физически подтверждённый пользователем LCD_CAM + GDMA-движок из `lcd_cam_dma_engine.ino`;
- существующий Web Studio с SoftAP и WebSocket из `matrix_128x128_WiFi.ino`;
- восстановление сохранённого кадра из LittleFS;
- обновление изображения без возврата к программному bit-banging.

**Исходные файлы не изменены.** Production `matrix_128x128_WiFi/matrix_128x128_WiFi.ino` остаётся нетронутым.

Рядом лежат необходимые ресурсы:

```text
web_page_data.h
matrix128x128_data.h
```

При изменении оригинальных ресурсов их нужно вручную синхронизировать с этой папкой.

---

## 2. Сетевые параметры и доступ

Параметры сохранены от исходной Wi-Fi прошивки:

```text
Режим:    SoftAP
SSID:     matrix 128x128
Пароль:   123456788
IP:       192.168.4.1
Web:      http://192.168.4.1/
WebSocket:/ws
```

TX power: `WIFI_POWER_8_5dBm`.

Библиотеки, использованные при успешной сборке:

- AsyncTCP 1.0.3;
- ESP Async WebServer 3.7.0;
- WiFi / FS / LittleFS из ESP32 Arduino Core 2.0.17.

---

## 3. Архитектура обновления изображения

```text
Браузер
   │ WebSocket
   ▼
Приём пикселя или полного кадра
   │
   ├──► active_frame — совместимый формат сохранения, 32 768 байт, PSRAM
   │
   └──► grayFrame — 14-bit RGB с gamma 2.4, 98 304 байта, PSRAM
              │
              ▼
        s_frame_dirty = true
              │
              ▼
        loop(): pack_dma_stream()
              │
              ▼
        s_shift_buf — 128 КБ внутренней SRAM
              │
              ▼
        LCD_CAM + GDMA circular ring → матрица, 50 FPS
```

Развёртка не останавливается при приёме сетевых данных. WebSocket обновляет кадровые буферы, а основной цикл упаковывает новый DMA-поток.

### Важное свойство

Во время `pack_dma_stream()` GDMA продолжает читать `s_shift_buf`. Это предотвращает остановку refresh и чёрные вспышки, но во время обновления возможен один смешанный кадр/tearing. Полный второй DMA-буфер в internal SRAM не создан, поскольку он потребовал бы ещё 128 КБ и мог не оставить места Wi-Fi.

---

## 4. Совместимый WebSocket-протокол

### 4.1. Один пиксель

```text
[0x01, x, y, r, g, b]
```

Длина: 6 байт.

Действия:

- обновляется `active_frame` по правилу яркости `>127`;
- в `grayFrame` записываются RGB888 → gamma 14-bit;
- устанавливается `s_frame_dirty`.

### 4.2. Полный кадр

```text
[0x02, 128, 128, ...16384 байта пикселей...]
```

Общая длина: 16 387 байт.

Каждый байт пикселя:

```text
bit 0 = Red
bit 1 = Green
bit 2 = Blue
```

Поддерживаются фрагментированные WebSocket-сообщения. Проверяются:

- общая длина;
- последовательность `info->index`;
- границы приёмного буфера;
- тип `0x02`;
- ширина и высота 128×128.

После полного приёма увеличивается счётчик `RX` в телеметрии.

### 4.3. Сохранение

```text
[0x03, modeId]
```

`modeId`: 1 = draw, 2 = text, 3 = image.

Файлы LittleFS:

```text
/boot_frame.bin  — 32 768 байт, raw active_frame, старый совместимый формат
/boot_mode.txt   — draw / text / image
```

Ответы:

```text
SAVED_OK
SAVE_ERR
BOOT_MODE:<mode>
```

### Ограничение цвета

Существующий Web Studio квантует полные кадры до 3 бит на пиксель, то есть 8 базовых цветов с Bayer dithering. 14-bit DMA-движок не превращает такой поток в RGB888. Для полноцветного видео потребуется отдельный новый протокол и изменение браузерного кода; сейчас это не добавлялось.

---

## 5. Загрузка сохранённого кадра

При старте:

1. Явно выделяются PSRAM-буферы.
2. Монтируется LittleFS.
3. Если `/boot_frame.bin` имеет ровно 32 768 байт, он загружается в `active_frame`.
4. Иначе используется `MATRIX128_ARTWORK`.
5. `sync_gray_from_active_frame()` преобразует старый формат в 14-bit `grayFrame`.
6. Строится DMA-поток.
7. Запускаются DP3356, DP32020A, LCD_CAM/GDMA.
8. После запуска движка стартуют SoftAP и HTTP/WebSocket server.

---

## 6. Критическая проблема памяти в Core 2.0.17

Первая сборка завершилась ошибкой:

```text
.dram0.bss will not fit in region dram0_0_seg
region overflowed by 32744 bytes
```

### Причина

В установленной Core 2.0.17 `EXT_RAM_ATTR` зависит от `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY`. В этой конфигурации макрос фактически пустой. Поэтому массивы с `EXT_RAM_ATTR` всё равно попадали в `.dram0.bss`.

Это видно в map-файле: `grayFrame`, `active_frame` и WebSocket receive buffer находились во внутренней DRAM.

### Решение

Для PSRAM используется явное выделение:

```cpp
grayFrame = (...)ps_malloc(98 304);
active_frame = (...)ps_malloc(32 768);
s_frame_recv_buf = (...)ps_malloc(16 387);
```

Если выделение не удалось:

```text
[FATAL] PSRAM allocation failed. Enable QSPI PSRAM.
```

**Не возвращать эти буферы в статическую DRAM.** `s_shift_buf`, DMA descriptors и другие буферы, читаемые GDMA, должны оставаться во внутренней SRAM.

---

## 7. Настройки рабочего LCD_CAM/GDMA, которые нельзя случайно изменить

Базовый `lcd_cam_dma_engine.ino` был признан пользователем рабочим на реальной матрице. В объединённой прошивке сохранены его ключевые настройки:

```cpp
LCD_CAM.lcd_clock.lcd_clk_sel = 3;       // PLL160M в Core 2.0.17
LCD_CAM.lcd_clock.lcd_clkm_div_num = 16; // 10 MHz
LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;
LCD_CAM.lcd_clock.lcd_ck_idle_edge = 1;
LCD_CAM.lcd_misc.lcd_bk_en = 1;
```

Для GPIO:

1. `gpio_set_direction()` вызывается **до** `esp_rom_gpio_connect_out_signal()`;
2. вызывается `esp_rom_gpio_pad_select_gpio()`;
3. после маршрутизации устанавливается `GPIO.func_out_sel_cfg[pin].oen_sel = 1`.

Комментарий рабочего кода: обратный порядок может сбросить `func_out_sel_cfg` в `SIG_GPIO_OUT_IDX`, из-за чего внешние сигналы исчезают.

Pinout:

```text
R1=4, G1=5, B1=6
R2=7, G2=15, B2=16
CLK=41, LAT=40, OE=2
DCK=18, BK/RCK=8, DIN=3
Onboard LED=47
Native USB CDC=19/20, не изменять
```

---

## 8. Настройки Arduino IDE

```text
Board:             ESP32S3 Dev Module
CPU Frequency:     240 MHz
PSRAM:             QSPI PSRAM / Enabled
Flash Mode:        DIO
Flash Frequency:   80 MHz, если доступно в меню
USB CDC On Boot:   Enabled
Port:              COM11 у текущего пользователя
```

FQBN, использованный для проверки:

```text
esp32:esp32:esp32s3:PSRAM=enabled,FlashMode=dio,CDCOnBoot=cdc
```

---

## 9. Результат компиляции

```text
Sketch uses 839825 bytes (64%) of program storage space.
Maximum is 1310720 bytes.

Global variables use 180032 bytes (54%) of dynamic memory,
leaving 147648 bytes for local variables.
Maximum is 327680 bytes.
```

**Прошивка платы и проверка браузера с реальной матрицей не выполнялись агентом.** Пользователю нужно провести аппаратный тест после загрузки.

---

## 10. Ожидаемая телеметрия

```text
[CLK SELF-TEST] 1000/1000 pulses
[WIFI] SoftAP IP: 192.168.4.1
[SETUP] LCD_CAM + GDMA + Wi-Fi Engine Started!
[TELEMETRY] ... FPS: 50.00 | PIN_CLK: 10.00 MHz ... | RX: 0 | WS: 0
```

При подключении браузера `WS` должен увеличиться. После каждого полного принятого кадра `RX` должен увеличиваться.

`Frames` — счётчик DMA-циклов, а не количество кадров, полученных из браузера.

---

## 11. Аппаратный чек-лист

1. Прошить именно `matrix_128x128_WiFi_GDMA.ino` с QSPI PSRAM.
2. Убедиться, что при отключённом Wi-Fi клиенте матрица показывает сохранённое или стартовое изображение.
3. Проверить 50 FPS и 10 MHz в Serial Monitor.
4. Подключиться к `matrix 128x128`, открыть `192.168.4.1`.
5. Отправить один пиксель, затем полный кадр draw/text/image.
6. Проверить, что изображение меняется, `RX` растёт, FPS остаётся около 50.
7. Выполнить сохранение и дождаться `SAVED_OK`.
8. Перезагрузить ESP32 и проверить восстановление кадра и режима.
9. Проверить серию кадров под нагрузкой: отсутствие зависаний AsyncTCP, стабильность refresh и отсутствие PSRAM errors.

---

## 12. Диагностика возможных проблем

### `[FATAL] PSRAM allocation failed`

- Включить QSPI PSRAM в Arduino IDE.
- Проверить, что выбран ESP32-S3FH4R2 с рабочей PSRAM.
- Не заменять `ps_malloc` на статические массивы.

### Матрица работает, сайт не открывается

- Проверить подключение именно к SoftAP ESP32.
- Проверить `192.168.4.1`, Serial `[WIFI]` и число `WS`.
- Убедиться, что используются AsyncTCP 1.0.3 и ESP Async WebServer 3.7.0.

### Сайт работает, матрица не меняется

- Проверить `WS` и `RX`.
- Для одиночных пикселей `RX` не растёт — это счётчик только полных кадров.
- Проверить `s_frame_dirty` и вызов `pack_dma_stream()` в `loop()`.
- Если FPS около 75, проверить `lcd_clk_sel`: должно быть 3.

### Изображение иногда смешивается на один кадр

Это возможный tearing из-за обновления единственного DMA shift buffer. Не останавливать GDMA как первое решение: остановка может дать мигание. Для полного устранения нужна продуманная схема атомарной подмены/чередования буферов с учётом internal SRAM.

### Сохранённый кадр не загружается

- Проверить размер `/boot_frame.bin`: ровно 32 768 байт.
- Проверить монтирование LittleFS и ответ `SAVED_OK`.
- Не менять layout `active_frame`, пока требуется совместимость со старыми сохранениями.

---

## 13. Правила для следующего агента

1. Не изменять production `matrix_128x128_WiFi.ino` без отдельного запроса.
2. Не прошивать плату без разрешения пользователя.
3. Не менять рабочий порядок GPIO matrix routing и параметры LCD_CAM без необходимости.
4. Не перемещать DMA buffers/descriptors в PSRAM.
5. Не использовать `EXT_RAM_ATTR` в этой Core как доказательство размещения в PSRAM; проверять map или использовать `ps_malloc`.
6. Сохранять существующий WebSocket-протокол, если не обновляется и браузерный клиент.
7. Разделять результаты компиляции и результаты реальных аппаратных измерений.
