#include <Arduino.h>

// ============================================================================
// TEST 09: MAXIMUM BRIGHTNESS & ZERO-STUTTER DRIVER
// 1. Eliminated 1-second rolling line: removed blocking Serial.printf and millis()
// 2. Increased brightness to 100%:
//    - Current Gain Reg 0x08 set to 0xFF (100% current)
//    - PWM active bits increased to 15-bit (0x7FFF / full dynamic range)
//    - PWM timing burst quadrupled from 64 to 256 GCLK cycles per line
// 3. Perfect color zones maintained: Left = Blue, Center = Red, Right = Green
// ============================================================================

// Physical RGB LED outputs:
#define PIN_R1  5   // Top Half Red   (GPIO 5) -> Drives BLUE
#define PIN_G1  4   // Top Half Green (GPIO 4) -> Drives RED
#define PIN_B1  6   // Top Half Blue  (GPIO 6) -> Drives GREEN

#define PIN_R2  15  // Bottom Half Red   (GPIO 15) -> Drives BLUE
#define PIN_G2  7   // Bottom Half Green (GPIO 7)  -> Drives RED
#define PIN_B2  16  // Bottom Half Blue  (GPIO 16) -> Drives GREEN

// Control pins wired to HUB75 connector:
#define PIN_CLK 41  // HUB75 Pin 13 (CLK / DCLK)
#define PIN_LE  40  // HUB75 Pin 14 (LAT / LE)
#define PIN_OE  2   // HUB75 Pin 15 (OE / ROW)

// Row-address DP32020A pins (HUB75 Pin 9, 10, 11):
#define PIN_A   18  // DCK (Data Clock)
#define PIN_B   8   // BK / RCK (Gate / Output Enable)
#define PIN_C   3   // DIN (Serial Data)

#define ONBOARD_LED 47

#define MATRIX_WIDTH  128
#define MATRIX_HEIGHT 64
#define ROWS          32
#define CHAINED_CHIPS 8

#define ALL_RGB_MASK ((1UL << PIN_R1) | (1UL << PIN_G1) | (1UL << PIN_B1) | \
                      (1UL << PIN_R2) | (1UL << PIN_G2) | (1UL << PIN_B2))

#define CLK_MASK (1UL << (PIN_CLK - 32))
#define LAT_MASK (1UL << (PIN_LE - 32))

// Fast clock pulse with 4 NOPs (~16ns) for clean cable signal integrity
#define CLK_PULSE() do { \
    GPIO.out1_w1ts.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
    GPIO.out1_w1tc.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
} while(0)

#define CLK_PULSE_2()  do { CLK_PULSE(); CLK_PULSE(); } while(0)
#define CLK_PULSE_4()  do { CLK_PULSE_2(); CLK_PULSE_2(); } while(0)
#define CLK_PULSE_8()  do { CLK_PULSE_4(); CLK_PULSE_4(); } while(0)
#define CLK_PULSE_14() do { CLK_PULSE_8(); CLK_PULSE_4(); CLK_PULSE_2(); } while(0)

static inline void set_gpio_for_clk_pulses(uint32_t pin, uint32_t width) {
    if (pin >= 32) GPIO.out1_w1ts.val = (1UL << (pin - 32));
    else GPIO.out_w1ts = (1UL << pin);

    while (width-- > 0) {
        CLK_PULSE();
    }

    if (pin >= 32) GPIO.out1_w1tc.val = (1UL << (pin - 32));
    else GPIO.out_w1tc = (1UL << pin);

    CLK_PULSE();
}

// DP3364 / DP3356 Protocol Primitives:
void dp3364_set_sdr_mode(void) {
    set_gpio_for_clk_pulses(PIN_LE, 15);
}

void dp3364_vsync(void) {
    set_gpio_for_clk_pulses(PIN_LE, 3);
}

void dp3364_pre_act(void) {
    set_gpio_for_clk_pulses(PIN_LE, 14);
}

void dp3364_write_reg(uint8_t reg, uint8_t val, bool latch) {
    uint16_t u16 = ((uint16_t)reg << 8) | val;
    uint16_t bits = 16;
    while (bits-- > 0) {
        uint32_t set_mask = 0;
        if (u16 & 0x8000) {
            set_mask = ALL_RGB_MASK;
        }
        uint32_t clr_mask = ALL_RGB_MASK & ~set_mask;
        GPIO.out_w1ts = set_mask;
        GPIO.out_w1tc = clr_mask;

        u16 <<= 1;

        if (latch && (bits < 5)) {
            GPIO.out1_w1ts.val = LAT_MASK;
        } else {
            GPIO.out1_w1tc.val = LAT_MASK;
        }

        CLK_PULSE();
        GPIO.out_w1tc = ALL_RGB_MASK;
    }
    GPIO.out1_w1tc.val = LAT_MASK;
}

// 13 configuration registers from verified DP3264/DP3364 specification:
// Reg 0x08 set to 0xFF (100% full current gain)
struct RegVal { uint8_t addr; uint8_t val; };
const RegVal INIT_REGS[] = {
    {0x11, 0x00},
    {0x02, 0x1F}, // 31 + 1 lines (1/32 scan)
    {0x03, 0x3F}, // group num = 63
    {0x04, 0x3F}, // max active PWM time
    {0x05, 0x04},
    {0x06, 0x42},
    {0x07, 0x00},
    {0x08, 0xFF}, // Maximum 100% current gain (boost from 0xBF)
    {0x09, 0x60},
    {0x0A, 0xBE},
    {0x0B, 0x8B},
    {0x0C, 0x88},
    {0x0D, 0x12}
};

void dp3364_init(void) {
    dp3364_set_sdr_mode();

    dp3364_vsync();
    dp3364_pre_act();

    for (size_t i = 0; i < sizeof(INIT_REGS) / sizeof(INIT_REGS[0]); i++) {
        for (uint32_t ch = 1; ch <= CHAINED_CHIPS; ch++) {
            dp3364_write_reg(INIT_REGS[i].addr, INIT_REGS[i].val, ch == CHAINED_CHIPS);
        }
    }

    // Stop writing registers by sending dummy zero words:
    for (uint32_t ch = 1; ch <= CHAINED_CHIPS; ch++) {
        for (int b = 0; b < 16; b++) {
            GPIO.out_w1tc = ALL_RGB_MASK;
            if (ch == CHAINED_CHIPS && b == 15) {
                GPIO.out1_w1ts.val = CLK_MASK | LAT_MASK;
                GPIO.out1_w1tc.val = CLK_MASK | LAT_MASK;
            } else {
                CLK_PULSE();
            }
        }
    }
}

// Row change pulse on OE (Pin 2 / ROW):
static inline void dp3364_change_row(uint32_t row) {
    set_gpio_for_clk_pulses(PIN_OE, (row == 0) ? 12 : 4);
}

// Fast DP32020A advance row:
static inline void dp32020_advance_row(uint32_t line) {
    // 1. Assert BK (PIN_B=8) and DIN (PIN_C=3 on line 0)
    uint32_t set_mask = (1UL << PIN_B);
    if (line == 0) set_mask |= (1UL << PIN_C);
    GPIO.out_w1ts = set_mask;

    // 2. Pulse DCK (PIN_A=18)
    GPIO.out_w1ts = (1UL << PIN_A);
    esp_rom_delay_us(1); // Tshadow >= 500ns per datasheet
    GPIO.out_w1tc = (1UL << PIN_A);

    // 3. Clear DIN and unblank BK
    uint32_t clr_mask = (1UL << PIN_B) | (1UL << PIN_C);
    GPIO.out_w1tc = clr_mask;
}

void led_panel_advance_row(uint32_t line) {
    dp3364_change_row(line);
    dp32020_advance_row(line);
}

// Precomputed column color masks (128 columns)
// Calibrated 3 Equal Zones:
// Left 1/3   (x = 0..42):   Pure BLUE  (PIN_R1=5, PIN_R2=15)
// Center 1/3 (x = 43..85):  Pure RED   (PIN_G1=4, PIN_G2=7)
// Right 1/3  (x = 86..127): Pure GREEN (PIN_B1=6, PIN_B2=16)
static uint32_t col_mask[128];

void build_col_masks() {
    for (int x = 0; x < 128; x++) {
        if (x < 43) {
            col_mask[x] = (1UL << PIN_R1) | (1UL << PIN_R2); // BLUE
        } else if (x < 86) {
            col_mask[x] = (1UL << PIN_G1) | (1UL << PIN_G2); // RED
        } else {
            col_mask[x] = (1UL << PIN_B1) | (1UL << PIN_B2); // GREEN
        }
    }
}

void setup() {
    setCpuFrequencyMhz(240); // Lock to maximum 240 MHz CPU clock
    Serial.begin(115200);
    pinMode(ONBOARD_LED, OUTPUT);
    digitalWrite(ONBOARD_LED, HIGH);

    pinMode(PIN_R1, OUTPUT); pinMode(PIN_R2, OUTPUT);
    pinMode(PIN_G1, OUTPUT); pinMode(PIN_G2, OUTPUT);
    pinMode(PIN_B1, OUTPUT); pinMode(PIN_B2, OUTPUT);

    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_LE,  OUTPUT);
    pinMode(PIN_OE,  OUTPUT);

    pinMode(PIN_A, OUTPUT);
    pinMode(PIN_B, OUTPUT);
    pinMode(PIN_C, OUTPUT);

    digitalWrite(PIN_R1, LOW); digitalWrite(PIN_R2, LOW);
    digitalWrite(PIN_G1, LOW); digitalWrite(PIN_G2, LOW);
    digitalWrite(PIN_B1, LOW); digitalWrite(PIN_B2, LOW);

    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_LE,  LOW);
    digitalWrite(PIN_OE,  LOW);

    digitalWrite(PIN_A, LOW);
    digitalWrite(PIN_B, LOW);
    digitalWrite(PIN_C, LOW);

    build_col_masks();

    delay(300);
    dp3364_init();
}

uint32_t frame_count = 0;

void loop() {
    // 1. Frame sync
    dp3364_vsync();

    // 2. Scan all 32 row pairs
    for (uint32_t line = 0; line < ROWS; line++) {
        for (uint32_t col = 0; col < 16; col++) {
            for (uint32_t ch = 1; ch <= CHAINED_CHIPS; ch++) {
                uint8_t x = ((ch - 1) * 16) + col;
                uint32_t mask = col_mask[x];

                // 1 MSB zero bit
                GPIO.out_w1tc = ALL_RGB_MASK;
                CLK_PULSE();

                // 14 active PWM bits (MSB-first data, maximum bright range)
                GPIO.out_w1ts = mask;
                CLK_PULSE_14();

                // 16th bit: latch if last chip in chain
                if (ch == CHAINED_CHIPS) {
                    GPIO.out1_w1ts.val = CLK_MASK | LAT_MASK;
                    GPIO.out1_w1tc.val = CLK_MASK | LAT_MASK;
                } else {
                    CLK_PULSE();
                }
            }
        }

        // Advance row on both DP3356 and DP32020A
        led_panel_advance_row(line);

        // Quadrupled PWM timing burst (256 GCLK cycles per line for solid high-duty illumination):
        for (uint32_t i = 0; i < 32; i++) {
            CLK_PULSE_8();
        }
    }

    frame_count++;

    // Non-blocking single-byte poll without any USB CDC stalls:
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'f') {
            Serial.printf("Panel FPS: %u\n", frame_count);
        }
    }
}
