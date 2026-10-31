# IDEAL FIRMWARE — Qiangli Q2.5 Pro (128x64) + ESP32-S3

**Verified Status:** User Confirmed: *"ОООООООООООО. ща вот ЯРКО. замиши эту прошивку как ИДЕАЛ"*  
**Framerate:** 99.4 FPS (Frame: 10 065 µs)  
**PWM Duration:** 32.96 µs / line (256 GCLK pulses)  
**RGB Shift:** 274.95 µs / line  
**Current Gain:** 200% (`0x08FF`)  
**PLL Mode:** `0x0642` (Bits [7:6] = 01: Internal Hardware PLL active)  
**Registers:** 13 Clean Registers transmitted one-shot during initial 13 frames  
**Visual Artifacts:** ZERO flicker, ZERO horizontal running beat bar, ZERO jumping rows on smiley  

## Key Registers
```cpp
const uint16_t DP3356_CONFIG_REGS[13] = {
    0x021F, // 32 scan lines (1/32 scan)
    0x033F, // 64 groups
    0x047F, // Max active PWM length
    0x0504,
    0x0642, // Bits [7:6] = 01 (INTERNAL HARDWARE PLL ACTIVE)
    0x0700,
    0x08FF, // MAXIMUM 200% CURRENT GAIN (255 / 255)
    0x0960,
    0x0ABE,
    0x0B8B, // Stable Knee Voltage & Anti-Coupling
    0x0CD8, // High-Current Range Multiplier
    0x0D12,
    0x1180  // Internal boost / high-efficiency PWM enable
};
```
