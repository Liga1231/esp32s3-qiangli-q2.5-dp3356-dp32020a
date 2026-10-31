#include <Arduino.h>
#include "splash_data.h"

// ============================================================================
// TEST 15: FULL 128x64 ARTWORK DISPLAY (CALIBRATED RGB + FULL 64-ROW GEOMETRY)
// Displays full color artwork:
//   - Smiling Emoji Face
//   - Heart symbol
//   - Typography: "ESP32-S3", "QIANG LI", "Q2.5 PRO", "112 FPS OK"
//   - Outer Blue and Inner Green full border framing
// Calibrated Pinout:
//   Top Half:    GPIO 4 (Red), GPIO 5 (Green), GPIO 6 (Blue)
//   Bottom Half: GPIO 7 (Red), GPIO 15 (Green), GPIO 16 (Blue)
// Equalized exposure across all 64 rows (0..63) with zero bleed and zero cut.
// ============================================================================

// Physical RGB LED outputs (calibrated with user feedback):
#define PIN_R1  4   // Top Half Red   (GPIO 4)
#define PIN_G1  5   // Top Half Green (GPIO 5)
#define PIN_B1  6   // Top Half Blue  (GPIO 6)

#define PIN_R2  7   // Bottom Half Red   (GPIO 7)
#define PIN_G2  15  // Bottom Half Green (GPIO 15)
#define PIN_B2  16  // Bottom Half Blue  (GPIO 16)

// Control pins wired to HUB75 connector:
#define PIN_CLK 41  // HUB75 Pin 13 (CLK / DCLK)
#define PIN_LE  40  // HUB75 Pin 14 (LAT / LE)
#define PIN_OE  2   // HUB75 Pin 15 (OE / ROW)

// Row-address DP32020A pins (HUB75 Pin 9, 10, 11):
#define PIN_A   18  // DCK (Data Clock)
#define PIN_B   8   // BK / RCK (Config Clock: MUST REMAIN LOW DURING SCAN)
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
#define CLK_PULSE_15() do { CLK_PULSE_14(); CLK_PULSE(); } while(0)

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

void dp3364_vsync(void) {
    set_gpio_for_clk_pulses(PIN_LE, 3);
}

// 19 DP3356 Configuration Registers (Official Manufacturer Depuwei .DOC specification + MAX 0x08FF Current)
const uint16_t DP3356_CONFIG_REGS[19] = {
    0x021F, // 32 scan lines (31 + 1)
    0x033F, // group num = 63
    0x04FF, // MAXIMUM active PWM time (255 -> 1024 clocks per group)
    0x0508, // Manufacturer doc: 0x08 (interline blanking / precharge)
    0x063A, // Manufacturer doc: 0x3A (High-speed 11x PLL frequency multiplier)
    0x0700, // PWM balancing
    0x08FF, // MAXIMUM 200% CURRENT GAIN (255 / 255)
    0x0967, // Manufacturer doc: 0x67 (output stage stabilization)
    0x0ABE, // Low-gray coupling elimination
    0x0B27, // Manufacturer doc: 0x27 (Optimized Knee Voltage for maximum LED drive current)
    0x0CD8, // High-Current Range Multiplier (0x18 | 0b11000000)
    0x0D08, // Manufacturer doc: 0x08 (EMI edge optimization)
    0x0E27, // Manufacturer doc: 0x27 (Enhanced current channel enable)
    0x0F20, // Manufacturer doc: 0x20 (PWM boost multiplier)
    0x1000,
    0x1180, // Internal boost / high-efficiency PWM enable (bit 7 = 1)
    0x1200,
    0x1300,
    0x1400
};

// Proper DP3356 Register Write with full hardware handshake per register:
void write_dp3356_reg(uint16_t reg_word) {
    set_gpio_for_clk_pulses(PIN_LE, 3);
    for (int i = 0; i < 16; i++) CLK_PULSE();
    set_gpio_for_clk_pulses(PIN_LE, 14);
    for (int i = 0; i < 8; i++) CLK_PULSE();

    for (int i = 0; i < 128; i++) {
        bool bit = (reg_word >> (15 - (i % 16))) & 1;
        if (bit) GPIO.out_w1ts = ALL_RGB_MASK;
        else     GPIO.out_w1tc = ALL_RGB_MASK;

        if (i >= 128 - 5) {
            GPIO.out1_w1ts.val = LAT_MASK;
        } else {
            GPIO.out1_w1tc.val = LAT_MASK;
        }
        CLK_PULSE();
    }
    GPIO.out1_w1tc.val = LAT_MASK;
    GPIO.out_w1tc = ALL_RGB_MASK;

    for (int i = 0; i < 8; i++) CLK_PULSE();
}

void dp3364_init(void) {
    set_gpio_for_clk_pulses(PIN_LE, 15);
}

// Row change pulse on OE (Pin 2 / ROW):
static inline void dp3364_change_row(uint32_t row) {
    set_gpio_for_clk_pulses(PIN_OE, (row == 0) ? 12 : 4);
}

// DP32020A advance row (exact Test 11 structure, zero gap, zero shift):
static inline void dp32020_advance_row(uint32_t line) {
    uint32_t set_mask = 0;
    if (line == 0) set_mask |= (1UL << PIN_C);

    // Pulse DCK (PIN_A=18)
    set_mask |= (1UL << PIN_A);
    GPIO.out_w1ts = set_mask;

    // Hold DCK high for > 500ns
    esp_rom_delay_us(1);

    GPIO.out_w1tc = (1UL << PIN_A);
    uint32_t clr_mask = (1UL << PIN_B) | (1UL << PIN_C);
    GPIO.out_w1tc = clr_mask;
}

void led_panel_advance_row(uint32_t line) {
    dp3364_change_row(line);
    dp32020_advance_row(line);
}

// Active Framebuffer in fast internal SRAM (32 lines x 16 cols x 8 chips = 16KB)
static uint32_t active_frame[32][16][8];

void setup() {
    setCpuFrequencyMhz(240);
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

    // Copy splash screen artwork into RAM buffer:
    memcpy_P(active_frame, SPLASH_FRAME_MASKS, sizeof(active_frame));

    delay(300);
    dp3364_init();
}

// Profiling accumulators (per Section 4 of Optimization Task)
static int64_t sum_frame_time = 0;
static int64_t sum_rgb_time = 0;
static int64_t sum_row_change_time = 0;
static int64_t sum_pwm_time = 0;
static int64_t sum_sync_time = 0;
static uint32_t profile_frames = 0;

void loop() {
    int64_t t_frame_start = esp_timer_get_time();

    // 1. One-time sequence: transmit 19 config registers once, then hold VSYNC only
    int64_t t_sync_start = esp_timer_get_time();
    static uint8_t s_cfg_idx = 0;
    if (s_cfg_idx < 19) {
        write_dp3356_reg(DP3356_CONFIG_REGS[s_cfg_idx]);
        s_cfg_idx++;
    } else {
        dp3364_vsync();
    }
    int64_t t_sync_end = esp_timer_get_time();

    int64_t frame_rgb_time = 0;
    int64_t frame_row_change_time = 0;
    int64_t frame_pwm_time = 0;

    // 2. Scan all 32 row pairs with FPGA-grade hardware blanking during data shifts
    for (uint32_t line = 0; line < ROWS; line++) {
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

        // Step B: Shift 128 pixels of RGB data into DP3356 while PMOS is 100% BLANKED:
        // Optimization: Linear pointer access + unrolled last-chip latch branch
        int64_t t_rgb_start = esp_timer_get_time();
        const uint32_t *line_ptr = &active_frame[line][0][0];
        for (uint32_t col = 0; col < 16; col++) {
            // Chips 0..6: Standard 16 clock pulses without latch
            for (uint32_t ch = 0; ch < 7; ch++) {
                uint32_t mask = *line_ptr++;
                GPIO.out_w1ts = mask;
                GPIO.out_w1tc = ALL_RGB_MASK & ~mask;
                CLK_PULSE_15();
                CLK_PULSE();
            }
            // Chip 7 (last chip in chain): 16th bit latches with LAT_MASK
            uint32_t mask = *line_ptr++;
            GPIO.out_w1ts = mask;
            GPIO.out_w1tc = ALL_RGB_MASK & ~mask;
            CLK_PULSE_15();
            GPIO.out1_w1ts.val = CLK_MASK | LAT_MASK;
            GPIO.out1_w1tc.val = CLK_MASK | LAT_MASK;
        }
        int64_t t_rgb_end = esp_timer_get_time();
        frame_rgb_time += (t_rgb_end - t_rgb_start);

        // Step C: DP3356 Row Change Handshake on OE
        int64_t t_rc_start = esp_timer_get_time();
        dp3364_change_row(line);
        int64_t t_rc_end = esp_timer_get_time();
        frame_row_change_time += (t_rc_end - t_rc_start);

        // Step D: UNBLANK PMOS (DCK=0): Turn ON current row for PWM emission
        GPIO.out_w1tc = (1UL << PIN_A);
        esp_rom_delay_us(1);

        // Step E: Flicker-Free Maximum PWM burst (1760 GCLK cycles ~ 225 us, 62.5 FPS, 45% Duty):
        int64_t t_pwm_start = esp_timer_get_time();
        for (uint32_t i = 0; i < 220; i++) {
            CLK_PULSE_8();
        }
        int64_t t_pwm_end = esp_timer_get_time();
        frame_pwm_time += (t_pwm_end - t_pwm_start);
    }

    int64_t t_frame_end = esp_timer_get_time();
    int64_t frame_total_us = t_frame_end - t_frame_start;

    sum_frame_time += frame_total_us;
    sum_rgb_time += frame_rgb_time;
    sum_row_change_time += frame_row_change_time;
    sum_pwm_time += frame_pwm_time;
    sum_sync_time += (t_sync_end - t_sync_start);
    profile_frames++;

    // Print profiling report once every 100 frames (~1 sec)
    if (profile_frames >= 100) {
        float avg_frame = (float)sum_frame_time / profile_frames;
        float avg_fps = 1000000.0f / avg_frame;
        float avg_line_rgb = (float)sum_rgb_time / (profile_frames * 32);
        float avg_row_change = (float)sum_row_change_time / (profile_frames * 32);
        float avg_pwm = (float)sum_pwm_time / (profile_frames * 32);
        float avg_sync = (float)sum_sync_time / profile_frames;

        Serial.printf("[PROFILE] Frame: %.1f us | FPS: %.1f | RGB/line: %.2f us | RowChange: %.2f us | PWM/line: %.2f us | Sync: %.2f us\n",
                      avg_frame, avg_fps, avg_line_rgb, avg_row_change, avg_pwm, avg_sync);

        sum_frame_time = 0;
        sum_rgb_time = 0;
        sum_row_change_time = 0;
        sum_pwm_time = 0;
        sum_sync_time = 0;
        profile_frames = 0;
    }
}
