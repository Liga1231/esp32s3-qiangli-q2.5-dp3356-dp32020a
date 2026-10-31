#include <Arduino.h>

// Direct Hardware GPIO mapping on ESP32-S3 Super Mini:
// Physical RGB LED outputs (Exact Test 05 calibrated):
#define PIN_R1  4   // Top Half Red
#define PIN_G1  6   // Top Half Green
#define PIN_B1  5   // Top Half Blue

#define PIN_R2  7   // Bottom Half Red
#define PIN_G2  16  // Bottom Half Green
#define PIN_B2  15  // Bottom Half Blue

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

// Clock pulse with safe 12 nops setup/hold to guarantee ZERO tears:
static inline void clock_pulse() {
    GPIO.out1_w1ts.val = CLK_MASK;
    __asm__ __volatile__(
        "nop; nop; nop; nop; nop; nop;"
        "nop; nop; nop; nop; nop; nop;"
    );
    GPIO.out1_w1tc.val = CLK_MASK;
    __asm__ __volatile__(
        "nop; nop; nop; nop; nop; nop;"
        "nop; nop; nop; nop; nop; nop;"
    );
}

// Drive a GPIO high for a given count of CLK pulses, then pull low + 1 extra CLK
static inline void set_gpio_for_clk_pulses(uint32_t pin, uint32_t width) {
    if (pin >= 32) GPIO.out1_w1ts.val = (1UL << (pin - 32));
    else GPIO.out_w1ts = (1UL << pin);

    while (width-- > 0) {
        clock_pulse();
    }

    if (pin >= 32) GPIO.out1_w1tc.val = (1UL << (pin - 32));
    else GPIO.out_w1tc = (1UL << pin);

    clock_pulse();
}

// DP3364 / DP3356 Protocol Primitives:
void dp3364_set_dual_edge_mode(bool dual_edge) {
    set_gpio_for_clk_pulses(PIN_LE, dual_edge ? 15 : 2);
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

        clock_pulse();
        GPIO.out_w1tc = ALL_RGB_MASK;
    }
    GPIO.out1_w1tc.val = LAT_MASK;
}

// Exact 19 configuration registers from Pico reference (.doc spec):
struct RegVal { uint8_t addr; uint8_t val; };
const RegVal INIT_REGS[] = {
    {0x02, 31},   // 31 + 1 lines (1/32 scan)
    {0x03, 63},   // group num
    {0x04, 0x7F}, // max time of active PWM
    {0x05, 0x08},
    {0x06, 0x3A},
    {0x07, 0x00},
    {0x08, 0xB9},
    {0x09, 0x67},
    {0x0A, 0xBE},
    {0x0B, 0x27},
    {0x0C, 0x18 | 0b11000000},
    {0x0D, 0x08},
    {0x0E, 0x27},
    {0x0F, 0x20},
    {0x10, 0x00},
    {0x11, 0x80},
    {0x12, 0x00},
    {0x13, 0x00},
    {0x14, 0x00}
};

void dp3364_panel_draw(uint16_t rgb_lo[3], uint16_t rgb_hi[3], bool latch) {
    uint16_t mask = 1 << 15;
    while (mask) {
        uint32_t set_mask = 0;
        if (rgb_lo[0] & mask) set_mask |= (1UL << PIN_R1);
        if (rgb_lo[1] & mask) set_mask |= (1UL << PIN_G1);
        if (rgb_lo[2] & mask) set_mask |= (1UL << PIN_B1);

        if (rgb_hi[0] & mask) set_mask |= (1UL << PIN_R2);
        if (rgb_hi[1] & mask) set_mask |= (1UL << PIN_G2);
        if (rgb_hi[2] & mask) set_mask |= (1UL << PIN_B2);

        uint32_t clr_mask = ALL_RGB_MASK & ~set_mask;
        GPIO.out_w1ts = set_mask;
        GPIO.out_w1tc = clr_mask;

        if (latch && (mask == 1)) {
            GPIO.out1_w1ts.val = LAT_MASK;
        } else {
            GPIO.out1_w1tc.val = LAT_MASK;
        }

        clock_pulse();
        GPIO.out_w1tc = ALL_RGB_MASK;
        GPIO.out1_w1tc.val = LAT_MASK;

        mask >>= 1;
    }
}

void dp3364_init(void) {
    dp3364_set_dual_edge_mode(true);

    dp3364_vsync();
    dp3364_pre_act();

    for (size_t i = 0; i < sizeof(INIT_REGS) / sizeof(INIT_REGS[0]); i++) {
        for (uint32_t ch = 1; ch <= CHAINED_CHIPS; ch++) {
            dp3364_write_reg(INIT_REGS[i].addr, INIT_REGS[i].val, ch == CHAINED_CHIPS);
        }
    }

    // Stop writing registers by sending dummy zero words:
    for (uint32_t ch = 1; ch <= CHAINED_CHIPS; ch++) {
        uint16_t zero[3] = {0, 0, 0};
        dp3364_panel_draw(zero, zero, ch == CHAINED_CHIPS);
    }
}

// Row change pulse on OE (Pin 2 / ROW):
static inline void dp3364_change_row(uint32_t row) {
    set_gpio_for_clk_pulses(PIN_OE, (row == 0) ? 12 : 4);
}

// Advance row on DP32020A via Pin A (DCK), Pin B (BK) & Pin C (DIN):
static inline void dp32020_advance_row(uint32_t line) {
    digitalWrite(PIN_B, HIGH); // Blank row driver outputs during shift transition
    digitalWrite(PIN_C, (line == 0) ? HIGH : LOW);
    digitalWrite(PIN_A, HIGH);
    delayMicroseconds(1); // Tshadow >= 500ns per datasheet
    digitalWrite(PIN_A, LOW);
    digitalWrite(PIN_C, LOW);
    digitalWrite(PIN_B, LOW);  // Unblank row driver outputs
}

void led_panel_advance_row(uint32_t line) {
    dp3364_change_row(line);
    dp32020_advance_row(line);
}

// 128x64 RGB Framebuffer (R, G, B values 0..16383, 14-bit safe ceiling)
// Left 1/3 (x 0..42):    Pure RED   (R=0x3FFF, G=0, B=0)
// Center 1/3 (x 43..85): Pure GREEN (R=0, G=0x3FFF, B=0)
// Right 1/3 (x 86..127): Pure BLUE  (R=0, G=0, B=0x3FFF)
void get_pixel_rgb(int x, int y, uint16_t rgb[3]) {
    if (x < 43) {
        rgb[0] = 0x3FFF; rgb[1] = 0;      rgb[2] = 0;      // RED
    } else if (x < 86) {
        rgb[0] = 0;      rgb[1] = 0x3FFF; rgb[2] = 0;      // GREEN
    } else {
        rgb[0] = 0;      rgb[1] = 0;      rgb[2] = 0x3FFF; // BLUE
    }
}

void setup() {
    setCpuFrequencyMhz(240); // 240 MHz maximum CPU clock
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

    delay(300);
    dp3364_init();
}

unsigned long lastBlink = 0;
bool led = false;

void loop() {
    // 1. Frame sync
    dp3364_vsync();

    // 2. Scan all 32 row pairs
    for (uint32_t line = 0; line < ROWS; line++) {
        // Iterate 16 outputs per driver chip
        for (uint32_t col = 0; col < 16; col++) {
            // Iterate through each of the 8 cascaded driver chips
            for (uint32_t ch = 1; ch <= CHAINED_CHIPS; ch++) {
                uint8_t x = ((ch - 1) * 16) + col;

                uint16_t rgb_lo[3];
                uint16_t rgb_hi[3];

                get_pixel_rgb(x, line,      rgb_lo); // Upper 32 rows
                get_pixel_rgb(x, line + 32, rgb_hi); // Lower 32 rows

                bool latch = (ch == CHAINED_CHIPS);
                dp3364_panel_draw(rgb_lo, rgb_hi, latch);
            }
        }

        // Advance row on both DP3356 and DP32020A
        led_panel_advance_row(line);

        // Required PWM timing burst:
        const uint32_t clk_cycles_per_line = (2 * (0x0 + 1) + 2 * (0x8 + 1) + 4 * (0x7F + 1)) / (0b010 + 1);
        for (uint32_t i = 0; i < (clk_cycles_per_line / 8 + 1); i++) {
            clock_pulse();
        }
    }

    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        led = !led;
        digitalWrite(ONBOARD_LED, led ? HIGH : LOW);
    }
}
