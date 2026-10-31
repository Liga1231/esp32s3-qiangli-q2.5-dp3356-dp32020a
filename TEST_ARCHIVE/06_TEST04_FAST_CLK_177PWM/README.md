# Test 06: Test 04 Baseline + Fast CLK (2 nops) + Full 177 PWM Clocks
- **Date/Time**: 2026-10-31 00:55
- **Baseline**: Exact Test 04 code structure (Blue Left, Red Center, Green Right).
- **Changes applied**:
  1. clock_pulse reduced from 16 nops down to 2 nops (raising DCLK to ~15-20 MHz).
  2. PWM timing burst increased from 23 pulses to full 177 GCLK pulses (increasing row on-time).
- **Camera Evaluation**:
  - Left: BLUE 87%
  - Center: RED 92%
  - Right: GREEN 100%
  - Brightness: 54.5 / 255
