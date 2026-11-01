/*
 * ============================================================================
 * DP3356 TRUE RGB GRAYSCALE TRIAL 3 (14-BIT S-PWM) - ESP32-S3
 * Hardware: Dual Qiangli Q2.5 Pro (128x128, 1/32 Scan)
 * Driver Silicon: 16 Chained DP3356 (S-PWM) + DP32020A
 * ============================================================================
 * Clean standalone test build: No Serial, No Wi-Fi, No telemetry.
 * Purpose: Verify genuine continuous per-pixel RGB analog grayscale
 *          and True Orange reproduction (R=255, G=100) without spatial dithering.
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_rom_sys.h"
#include "esp_attr.h"

// ---------------- Production Pin Definitions ----------------
#define PIN_R1  4
#define PIN_G1  5
#define PIN_B1  6
#define PIN_R2  7
#define PIN_G2  15
#define PIN_B2  16

#define PIN_CLK 41
#define PIN_LE  40
#define PIN_OE  2
#define PIN_A   18   // DP32020A DCK / Row Blanking Clock
#define PIN_B   8    // DP32020A BK/RCK (Held LOW)
#define PIN_C   3    // DP32020A DIN (Row 0 marker)

#define MATRIX_WIDTH      128
#define MATRIX_HEIGHT     128
#define ROWS              32
#define CHAINED_CHIPS     16
#define CHANNELS_PER_CHIP 16
#define GRAY_BITS         14
#define GRAY_MAX          ((1u << GRAY_BITS) - 1u) // 16383

#define ALL_RGB_MASK ((1UL << PIN_R1) | (1UL << PIN_G1) | (1UL << PIN_B1) | \
                      (1UL << PIN_R2) | (1UL << PIN_G2) | (1UL << PIN_B2))
#define CLK_MASK (1UL << (PIN_CLK - 32))
#define LAT_MASK (1UL << (PIN_LE - 32))

// Direct GPIO clock pulses
#define CLK_PULSE() do { \
    GPIO.out1_w1ts.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop;"); \
    GPIO.out1_w1tc.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop;"); \
} while (0)

#define CLK_PULSE_2()  do { CLK_PULSE(); CLK_PULSE(); } while (0)
#define CLK_PULSE_4()  do { CLK_PULSE_2(); CLK_PULSE_2(); } while (0)
#define CLK_PULSE_8()  do { CLK_PULSE_4(); CLK_PULSE_4(); } while (0)

// 13 DP3356 Configuration Registers (PLL Enabled 0x0642 + Boost 0x1180 + MAX Current 0x08FF)
static const uint16_t DP3356_CONFIG_REGS[13] = {
    0x021F, // 32 scan lines (31 + 1)
    0x033F, // group num = 63
    0x047F, // max active PWM time
    0x0504,
    0x0642, // Bits [7:6] = 01 (INTERNAL PLL ENABLED)
    0x0700,
    0x08FF, // MAXIMUM 200% CURRENT GAIN (255 / 255)
    0x0960,
    0x0ABE,
    0x0B8B, // Stable Knee Voltage & Anti-Coupling
    0x0CD8, // High-Current Range Multiplier (0x18 | 0b11000000)
    0x0D12,
    0x1180  // Internal boost / high-efficiency PWM enable (bit 7 = 1)
};

// 14-Bit TrueColor Framebuffer: [32 rows][16 cols][16 chips][6 lanes] = 98,304 bytes (in PSRAM)
// Lanes: 0=R1, 1=G1, 2=B1, 3=R2, 4=G2, 5=B2
EXT_RAM_ATTR static uint16_t grayFrame[ROWS][16][CHAINED_CHIPS][6];

// Pre-packed bitstream for high-speed scan: [32 rows][4096 clocks] = 128 KB (in fast SRAM)
static uint8_t packedStream[ROWS][4096];

// Fast lookup tables: 6-bit code (0..63) -> 32-bit GPIO set and clear masks
static uint32_t s_rgb_set_mask[64];
static uint32_t s_rgb_clr_mask[64];

// Gamma 2.4 Lookup Table: Maps 8-bit sRGB (0..255) to 14-bit linear PWM (0..16383)
static uint16_t srgbToLinear14[256];

static inline void set_gpio_for_clk_pulses(uint32_t pin, uint32_t width) {
    if (pin >= 32) GPIO.out1_w1ts.val = (1UL << (pin - 32));
    else GPIO.out_w1ts = (1UL << pin);

    while (width-- > 0) CLK_PULSE();

    if (pin >= 32) GPIO.out1_w1tc.val = (1UL << (pin - 32));
    else GPIO.out_w1tc = (1UL << pin);

    CLK_PULSE();
}

static inline void set_all_rgb_low() {
    GPIO.out_w1tc = ALL_RGB_MASK;
}

static void dp3364_vsync() {
    set_gpio_for_clk_pulses(PIN_LE, 3);
}

// Write one configuration register to 16 daisy-chained DP3356 chips
static void write_dp3356_reg(uint16_t reg_word) {
    // Blank all PMOS rows so config data is never displayed
    GPIO.out_w1ts = (1UL << PIN_A);
    esp_rom_delay_us(1);

    set_gpio_for_clk_pulses(PIN_LE, 3);
    for (int i = 0; i < 16; i++) CLK_PULSE();
    set_gpio_for_clk_pulses(PIN_LE, 14);
    for (int i = 0; i < 8; i++) CLK_PULSE();

    for (int i = 0; i < 256; i++) {
        const bool bit = (reg_word >> (15 - (i % 16))) & 1;
        if (bit) GPIO.out_w1ts = ALL_RGB_MASK;
        else     GPIO.out_w1tc = ALL_RGB_MASK;

        if (i >= 256 - 5) GPIO.out1_w1ts.val = LAT_MASK;
        else              GPIO.out1_w1tc.val = LAT_MASK;

        CLK_PULSE();
    }

    GPIO.out1_w1tc.val = LAT_MASK;
    set_all_rgb_low();
    for (int i = 0; i < 8; i++) CLK_PULSE();
}

static void dp3364_init() {
    set_gpio_for_clk_pulses(PIN_LE, 15);
}

static inline void dp3364_change_row(uint32_t row) {
    set_gpio_for_clk_pulses(PIN_OE, (row == 0) ? 12 : 4);
}

// Build standard sRGB to Linear Gamma 2.4 LUT once in setup()
static void build_gamma_table() {
    for (uint32_t i = 0; i < 256; ++i) {
        const float s = (float)i / 255.0f;
        const float linear = (s <= 0.04045f)
            ? (s / 12.92f)
            : powf((s + 0.055f) / 1.055f, 2.4f);
        uint32_t v = (uint32_t)(linear * (float)GRAY_MAX + 0.5f);
        if (v > GRAY_MAX) v = GRAY_MAX;
        srgbToLinear14[i] = (uint16_t)v;
    }
}

// Exact coordinate-to-driver channel mapping
static void set_gray_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return;

    const uint32_t line = y % 32;
    const bool is_bottom = ((y % 64) >= 32);
    const uint32_t col = x % 16;
    const uint32_t ch = (y < 64) ? (8 + (x / 16)) : (x / 16);
    const uint32_t lane = is_bottom ? 3 : 0;

    grayFrame[line][col][ch][lane + 0] = srgbToLinear14[r];
    grayFrame[line][col][ch][lane + 1] = srgbToLinear14[g];
    grayFrame[line][col][ch][lane + 2] = srgbToLinear14[b];
}

// ---------------- Test Pattern Modes (Trial 7) ----------------
#define TEST_MODE_GRID   0 // 4 vertical zones (Red, Green, Blue, Orange) in 4 brightness levels
#define TEST_MODE_RED    1 // Full-screen Solid Red (R=255, G=0, B=0)
#define TEST_MODE_GREEN  2 // Full-screen Solid Green (R=0, G=255, B=0)
#define TEST_MODE_BLUE   3 // Full-screen Solid Blue (R=0, G=0, B=255)
#define TEST_MODE_WHITE  4 // Full-screen Solid White (R=255, G=255, B=255)

// Current Selection: TEST_MODE_GRID (4-color pattern with data setup fix)
#ifndef ACTIVE_TEST_MODE
#define ACTIVE_TEST_MODE TEST_MODE_GRID
#endif

// Test pattern generator for Trial 7
static void make_test_pattern() {
    memset(grayFrame, 0, sizeof(grayFrame));

#if (ACTIVE_TEST_MODE == TEST_MODE_RED)
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            set_gray_pixel(x, y, 255, 0, 0);
        }
    }
#elif (ACTIVE_TEST_MODE == TEST_MODE_GREEN)
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            set_gray_pixel(x, y, 0, 255, 0);
        }
    }
#elif (ACTIVE_TEST_MODE == TEST_MODE_BLUE)
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            set_gray_pixel(x, y, 0, 0, 255);
        }
    }
#elif (ACTIVE_TEST_MODE == TEST_MODE_WHITE)
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            set_gray_pixel(x, y, 255, 255, 255);
        }
    }
#else // Test T11-A (Sonnet v6): Full-Scale Codes Only (R, G, B, White at 255, no intermediate grayscale)
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            uint8_t r = 0, g = 0, b = 0;
            if (x < 32) {
                r = 255; g = 0; b = 0;        // Zone 1: Pure Full-Scale Red
            } else if (x < 64) {
                r = 0; g = 255; b = 0;        // Zone 2: Pure Full-Scale Green
            } else if (x < 96) {
                r = 0; g = 0; b = 255;        // Zone 3: Pure Full-Scale Blue
            } else {
                r = 255; g = 255; b = 255;    // Zone 4: Pure Full-Scale White
            }
            set_gray_pixel(x, y, r, g, b);
        }
    }
#endif
}

// Precompute 64-entry GPIO mask LUT and pack the 14-bit display data stream
static void init_lut_and_pack_stream() {
    // 1. Build 64-entry GPIO mask lookup table for all 6-bit RGB combinations
    for (uint32_t code = 0; code < 64; ++code) {
        uint32_t set_m = 0;
        if (code & (1 << 0)) set_m |= (1UL << PIN_R1);
        if (code & (1 << 1)) set_m |= (1UL << PIN_G1);
        if (code & (1 << 2)) set_m |= (1UL << PIN_B1);
        if (code & (1 << 3)) set_m |= (1UL << PIN_R2);
        if (code & (1 << 4)) set_m |= (1UL << PIN_G2);
        if (code & (1 << 5)) set_m |= (1UL << PIN_B2);
        s_rgb_set_mask[code] = set_m;
        s_rgb_clr_mask[code] = ALL_RGB_MASK & ~set_m;
    }

    // 2. Pre-pack all 32 rows x 4096 DCLK bit-clocks
    for (uint32_t line = 0; line < ROWS; ++line) {
        uint32_t idx = 0;
        for (uint32_t col = 0; col < 16; ++col) {
            for (uint32_t ch = 0; ch < 16; ++ch) {
                uint16_t r1 = grayFrame[line][col][ch][0];
                uint16_t g1 = grayFrame[line][col][ch][1];
                uint16_t b1 = grayFrame[line][col][ch][2];
                uint16_t r2 = grayFrame[line][col][ch][3];
                uint16_t g2 = grayFrame[line][col][ch][4];
                uint16_t b2 = grayFrame[line][col][ch][5];

                for (int bit = 15; bit >= 0; --bit) {
                    uint16_t m = (1u << bit);
                    uint8_t code = 0;
                    if (r1 & m) code |= (1 << 0);
                    if (g1 & m) code |= (1 << 1);
                    if (b1 & m) code |= (1 << 2);
                    if (r2 & m) code |= (1 << 3);
                    if (g2 & m) code |= (1 << 4);
                    if (b2 & m) code |= (1 << 5);

                    packedStream[line][idx++] = code;
                }
            }
        }
    }
}

// Scan all 32 row pairs with accurate per-column DP3356 latching
static void scan_one_frame() {
    for (uint32_t line = 0; line < ROWS; ++line) {
        // Step A: Clock DP32020A and BLANK all PMOS outputs (DCK=1)
        if (line == 0) {
            GPIO.out_w1ts = (1UL << PIN_C); // DIN = 1
            esp_rom_delay_us(1);
            GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (Blanking active: all PMOS OFF / 0mA)
            esp_rom_delay_us(1);
            GPIO.out_w1tc = (1UL << PIN_C); // Clear DIN
        } else {
            GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (Blanking active: all PMOS OFF / 0mA)
            esp_rom_delay_us(1);
        }

        // Step B: High-speed shift of 4096 pre-packed bits with concurrent latch on 16th clock of chip 15
        const uint8_t *line_ptr = &packedStream[line][0];
        for (uint32_t col = 0; col < 16; ++col) {
            // Chips 0..14 (15 chips * 16 bits = 240 bits) + Chip 15 (bits 15..1 = 15 bits) = 255 bits
            for (uint32_t i = 0; i < 255; ++i) {
                uint8_t code = *line_ptr++;
                GPIO.out_w1ts = s_rgb_set_mask[code];
                GPIO.out_w1tc = s_rgb_clr_mask[code];
                CLK_PULSE();
            }

            // Chip 15 Bit 0 (the 256th clock of this column):
            // Proper setup/hold sequence: LAT high BEFORE CLK rises to eliminate race conditions!
            uint8_t code = *line_ptr++;
            GPIO.out_w1ts = s_rgb_set_mask[code];
            GPIO.out_w1tc = s_rgb_clr_mask[code];

            GPIO.out1_w1ts.val = LAT_MASK;
            __asm__ __volatile__("nop; nop; nop; nop;");
            GPIO.out1_w1ts.val = CLK_MASK;
            __asm__ __volatile__("nop; nop; nop; nop;");
            GPIO.out1_w1tc.val = CLK_MASK;
            __asm__ __volatile__("nop; nop; nop; nop;");
            GPIO.out1_w1tc.val = LAT_MASK;
            __asm__ __volatile__("nop; nop; nop; nop;");
        }

        // Step C: DP3356 Row Change Handshake on OE
        dp3364_change_row(line);

        // Step D: UNBLANK PMOS (DCK=0): Turn ON current row for PWM emission
        GPIO.out_w1tc = (1UL << PIN_A);
        esp_rom_delay_us(1);

        // Step E: Settling dwell clocks for DP3356 internal S-PWM
        for (uint32_t i = 0; i < 73; ++i) {
            CLK_PULSE_8();
        }
        CLK_PULSE_2();
    }
}

void setup() {
    setCpuFrequencyMhz(240);

    pinMode(PIN_R1, OUTPUT); pinMode(PIN_R2, OUTPUT);
    pinMode(PIN_G1, OUTPUT); pinMode(PIN_G2, OUTPUT);
    pinMode(PIN_B1, OUTPUT); pinMode(PIN_B2, OUTPUT);
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_LE,  OUTPUT);
    pinMode(PIN_OE,  OUTPUT);
    pinMode(PIN_A,   OUTPUT);
    pinMode(PIN_B,   OUTPUT);
    pinMode(PIN_C,   OUTPUT);

    set_all_rgb_low();
    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_LE,  LOW);
    digitalWrite(PIN_OE,  LOW);
    digitalWrite(PIN_A,   HIGH); // Blanking active: all PMOS OFF on boot
    digitalWrite(PIN_B,   LOW);
    digitalWrite(PIN_C,   LOW);

    build_gamma_table();
    make_test_pattern();
    init_lut_and_pack_stream();

    delay(200);
    dp3364_init();
}

void loop() {
    // 1. Transmit 13 config registers once (one per frame) then hold VSYNC
    static uint8_t s_cfg_idx = 0;
    if (s_cfg_idx < 13) {
        write_dp3356_reg(DP3356_CONFIG_REGS[s_cfg_idx]);
        s_cfg_idx++;
    } else {
        dp3364_vsync();
    }

    // 2. Continuous scan of all 32 row pairs
    scan_one_frame();
}
