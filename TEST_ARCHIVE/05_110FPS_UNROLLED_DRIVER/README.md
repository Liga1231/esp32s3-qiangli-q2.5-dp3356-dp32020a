# Test 05: 110 FPS Unrolled Driver & High-Frequency PWM
- **FPS**: 110 FPS (nearly triple from the previous 42 FPS)
- **PWM Configuration**: Reg 0x04 tuned to 0x3F (doubling hardware PWM sub-period frequency).
- **Driver Pipeline**: Direct bit-plane hardware unrolling, no loop branches, instant APB writes.
- **Coverage**: 100% active diode coverage, zero horizontal dashed cuts, zero vertical dark lines.
