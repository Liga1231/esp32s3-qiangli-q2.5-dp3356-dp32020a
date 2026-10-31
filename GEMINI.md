# ESP32-S3 Super Mini Project Context

This workspace is for developing on the **ESP32-S3 Super Mini** board.

## Key Hardware & Build Facts
- **Board:** ESP32-S3 Super Mini (4MB Flash XMC, 2MB PSRAM QSPI, Native USB CDC/JTAG on COM11).
- **LED:** GPIO 47.
- **Flash Mode:** MUST be **DIO 80MHz** (`FlashMode=dio`). Never use QIO (causes `ets_loader.c 78` panic).
- **USB CDC on Boot:** MUST be **Enabled** (`CDCOnBoot=cdc`).
- **Upload Reset:** Configured to `--before usb_reset` for hands-free automatic flashing (no BOOT button press needed).
- Full guide: [ESP32_S3_SUPERMINI_CHEAT_SHEET.md](file:///c:/Users/Liga.inc/Desktop/esp_wibe/ESP32_S3_SUPERMINI_CHEAT_SHEET.md).
