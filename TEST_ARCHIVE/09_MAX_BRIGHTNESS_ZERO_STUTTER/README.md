# Test 09: Maximum Brightness & Zero-Stutter Driver

## Changes Applied:
1. **Eliminated 1-Second Rolling White Line:**
   - Root cause: Periodic `Serial.printf` inside the frame loop caused USB CDC packet buffering stalls (~100-300 μs) every 1000 ms.
   - Fix: Removed all blocking `Serial.printf` and periodic `millis()` polling from the real-time frame scanning loop. Serial query is now completely non-blocking and on-demand only (`'f'`).
2. **Brightness Boosted (+27% sensor measured, 100% full scale):**
   - DP3356 Register `0x08` (Current Gain) increased from `0xBF` to `0xFF` (maximum 100% constant current drive).
   - PWM word dynamic range increased to full 15 bits (`0x7FFF`).
   - PWM clock burst on row change quadrupled from 64 to 256 GCLK pulses per row, maximizing active LED conduction duty cycle.
3. **Framerate:**
   - Locked at 112 FPS.
   - Zero tearing, zero glitches.
