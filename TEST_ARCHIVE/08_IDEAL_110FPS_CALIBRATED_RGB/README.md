# Test 08: Ideal 110 FPS Flicker-Free Driver (Calibrated 3 Zones)

## User Confirmation
"то что сейчас загружено идеально. доделай что хотел, дальше я подскажу что надо исправить"

## Results
- **FPS:** 110 FPS locked (Serial reporting `Panel FPS: 110`).
- **Flicker:** Virtually eliminated due to high frame refresh rate and 64-pulse PWM bursts.
- **Signal Integrity:** 4-NOP pulse delay completely eliminated tears / horizontal glitched lines.
- **Color Zones (Physical):**
  - Left Zone (cols 0..42): Pure BLUE 100%
  - Center Zone (cols 43..85): Pure RED 94%
  - Right Zone (cols 86..127): Pure GREEN 100%
- **Physical Pin Mapping:**
  - PIN_R1 = GPIO 5 (Top Half) / PIN_R2 = GPIO 15 (Bottom Half) -> Physically drives BLUE zone
  - PIN_G1 = GPIO 4 (Top Half) / PIN_G2 = GPIO 7 (Bottom Half) -> Physically drives RED zone
  - PIN_B1 = GPIO 6 (Top Half) / PIN_B2 = GPIO 16 (Bottom Half) -> Physically drives GREEN zone
