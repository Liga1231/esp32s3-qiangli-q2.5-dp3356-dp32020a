#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_sig_map.h>
#include <soc/lcd_cam_struct.h>
#include <soc/lcd_cam_reg.h>
#include <soc/gdma_struct.h>
#include <soc/gdma_reg.h>
#include <esp_private/gdma.h>
#include <esp_rom_gpio.h>
#include <hal/dma_types.h>
#include <hal/gpio_hal.h>
#include <driver/periph_ctrl.h>
#include "splash_data.h"

// ============================================================================
// Qiangli Q2.5 Pro (Q2.5 S20B32 V1.1) 128x64 1/32 Scan Matrix Driver
// TEST 38: Hardware LCD_CAM GDMA with lcd_8bits_order = 1 Byte Swap Fix
// Full 19-register Depuwei configuration + 10 MHz rock-solid PCLK
// Developed autonomously by AI (Google DeepMind Antigravity / Gemini)
// ============================================================================

#define PIN_R1  4
#define PIN_G1  5
#define PIN_B1  6
#define PIN_R2  7
#define PIN_G2  15
#define PIN_B2  16

#define PIN_CLK 41
#define PIN_LE  40
#define PIN_OE  2

#define PIN_A   18  // DCK (Row Clock / PMOS Enable)
#define PIN_B   38  // BK (Row Blanking)
#define PIN_C   39  // DIN (Row Serial Data)

#define ONBOARD_LED 47
#define ROWS 32

#define CLK_MASK (1UL << (PIN_CLK - 32))
#define LAT_MASK (1UL << (PIN_LE - 32))
#define ALL_RGB_MASK ((1UL<<PIN_R1)|(1UL<<PIN_G1)|(1UL<<PIN_B1)|(1UL<<PIN_R2)|(1UL<<PIN_G2)|(1UL<<PIN_B2))

// Configurable PWM pulse chunks for brightness
#define PWM_BURST_CHUNKS 200

static const int DATA_PINS[7] = { PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2, PIN_LE };

static gdma_channel_handle_t s_dma_chan = NULL;
static dma_descriptor_t s_dma_desc;
static uint8_t *s_dma_frame = NULL; // 32 lines * 2048 bytes = 64KB internal DMA SRAM

#define CLK_PULSE() do { \
    GPIO.out1_w1ts.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
    GPIO.out1_w1tc.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
} while(0)

#define CLK_PULSE_2()  do { CLK_PULSE(); CLK_PULSE(); } while(0)
#define CLK_PULSE_4()  do { CLK_PULSE_2(); CLK_PULSE_2(); } while(0)
#define CLK_PULSE_8()  do { CLK_PULSE_4(); CLK_PULSE_4(); } while(0)

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
    0x047F, // max active PWM time
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

static inline void dp3364_change_row(uint32_t row) {
    set_gpio_for_clk_pulses(PIN_OE, (row == 0) ? 12 : 4);
}

static inline void route_pins_to_gpio(void) {
    esp_rom_gpio_connect_out_signal(PIN_CLK, SIG_GPIO_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(PIN_LE,  SIG_GPIO_OUT_IDX, false, false);
    for (int i = 0; i < 6; i++) {
        esp_rom_gpio_connect_out_signal(DATA_PINS[i], SIG_GPIO_OUT_IDX, false, false);
    }
}

static inline void route_pins_to_lcd_cam(void) {
    esp_rom_gpio_connect_out_signal(PIN_CLK, LCD_PCLK_IDX, false, false);
    esp_rom_gpio_connect_out_signal(PIN_LE,  LCD_DATA_OUT6_IDX, false, false);
    for (int i = 0; i < 6; i++) {
        esp_rom_gpio_connect_out_signal(DATA_PINS[i], LCD_DATA_OUT0_IDX + i, false, false);
    }
}

bool init_lcd_dma(void) {
    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    periph_module_reset(PERIPH_LCD_CAM_MODULE);

    LCD_CAM.lcd_user.lcd_reset = 1;
    esp_rom_delay_us(1000);

    LCD_CAM.lcd_clock.lcd_clk_sel = 3; // 160MHz PLL
    LCD_CAM.lcd_clock.lcd_ck_out_edge = 1; // Sample on rising edge
    LCD_CAM.lcd_clock.lcd_ck_idle_edge = 0;
    LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;
    LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;
    LCD_CAM.lcd_clock.lcd_clkm_div_num = 16; // 10 MHz PCLK (100ns period, ultra clean signal)
    LCD_CAM.lcd_clock.lcd_clkm_div_a = 1;
    LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;

    LCD_CAM.lcd_ctrl.lcd_rgb_mode_en = 0;
    LCD_CAM.lcd_rgb_yuv.lcd_conv_bypass = 0;
    LCD_CAM.lcd_misc.lcd_next_frame_en = 0;
    LCD_CAM.lcd_misc.lcd_bk_en = 1;
    LCD_CAM.lcd_data_dout_mode.val = 0;

    // KEY FIX: Stop after 2048 cycles + Byte Swap (lcd_8bits_order = 1)
    LCD_CAM.lcd_user.lcd_always_out_en = 0;
    LCD_CAM.lcd_user.lcd_dout_cyclelen = 2048 - 1;
    LCD_CAM.lcd_user.lcd_8bits_order = 1; // FIX: Transmit Byte 0 before Byte 1!
    LCD_CAM.lcd_user.lcd_bit_order = 0;
    LCD_CAM.lcd_user.lcd_2byte_en = 0; // 8-bit mode
    LCD_CAM.lcd_user.lcd_dummy = 0;
    LCD_CAM.lcd_user.lcd_cmd = 0;

    for (int i = 0; i < 7; i++) {
        pinMode(DATA_PINS[i], OUTPUT);
        gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[DATA_PINS[i]], PIN_FUNC_GPIO);
        gpio_set_drive_capability((gpio_num_t)DATA_PINS[i], (gpio_drive_cap_t)3);
    }
    pinMode(PIN_CLK, OUTPUT);
    gpio_hal_iomux_func_sel(GPIO_PIN_MUX_REG[PIN_CLK], PIN_FUNC_GPIO);
    gpio_set_drive_capability((gpio_num_t)PIN_CLK, (gpio_drive_cap_t)3);

    // Allocate 64KB internal SRAM for all 32 lines of DMA transfer
    s_dma_frame = (uint8_t *)heap_caps_malloc(32 * 2048, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_dma_frame) return false;

    // Convert SPLASH_FRAME_MASKS into hardware DMA format:
    // Bits 0..5 = R1, G1, B1, R2, G2, B2
    // Bit 6 = LAT (pulsed on the 16th clock of chip 7 for each column)
    for (uint32_t line = 0; line < 32; line++) {
        uint8_t *line_dma = &s_dma_frame[line * 2048];
        uint32_t byte_idx = 0;

        for (uint32_t col = 0; col < 16; col++) {
            // Chips 0..6: 16 clocks each, bit 6 (LAT) = 0
            for (uint32_t ch = 0; ch < 7; ch++) {
                uint32_t mask = pgm_read_dword(&SPLASH_FRAME_MASKS[line][col][ch]);
                uint8_t b = 0;
                if (mask & (1UL << PIN_R1)) b |= (1 << 0);
                if (mask & (1UL << PIN_G1)) b |= (1 << 1);
                if (mask & (1UL << PIN_B1)) b |= (1 << 2);
                if (mask & (1UL << PIN_R2)) b |= (1 << 3);
                if (mask & (1UL << PIN_G2)) b |= (1 << 4);
                if (mask & (1UL << PIN_B2)) b |= (1 << 5);

                for (int p = 0; p < 16; p++) {
                    line_dma[byte_idx++] = b;
                }
            }
            // Chip 7 (last chip in chain): 15 clocks LAT=0, 16th clock LAT=1
            uint32_t mask = pgm_read_dword(&SPLASH_FRAME_MASKS[line][col][7]);
            uint8_t b = 0;
            if (mask & (1UL << PIN_R1)) b |= (1 << 0);
            if (mask & (1UL << PIN_G1)) b |= (1 << 1);
            if (mask & (1UL << PIN_B1)) b |= (1 << 2);
            if (mask & (1UL << PIN_R2)) b |= (1 << 3);
            if (mask & (1UL << PIN_G2)) b |= (1 << 4);
            if (mask & (1UL << PIN_B2)) b |= (1 << 5);

            for (int p = 0; p < 15; p++) {
                line_dma[byte_idx++] = b;
            }
            line_dma[byte_idx++] = b | (1 << 6); // Latch on 16th bit of 8th chip
        }
    }

    s_dma_desc.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
    s_dma_desc.dw0.suc_eof = 1;
    s_dma_desc.dw0.size = 2048;
    s_dma_desc.dw0.length = 2048;
    s_dma_desc.buffer = s_dma_frame;
    s_dma_desc.next = NULL;

    gdma_channel_alloc_config_t dma_chan_config = {
        .sibling_chan = NULL,
        .direction = GDMA_CHANNEL_DIRECTION_TX,
        .flags = { .reserve_sibling = 0 }
    };
    if (gdma_new_channel(&dma_chan_config, &s_dma_chan) != ESP_OK) return false;
    gdma_connect(s_dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));

    static gdma_strategy_config_t strategy_config = {
        .owner_check = false,
        .auto_update_desc = false
    };
    gdma_apply_strategy(s_dma_chan, &strategy_config);

    gdma_transfer_ability_t ability = {
        .sram_trans_align = 32,
        .psram_trans_align = 64,
    };
    gdma_set_transfer_ability(s_dma_chan, &ability);

    LCD_CAM.lcd_user.lcd_dout = 1;
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;

    return true;
}

void setup() {
    setCpuFrequencyMhz(240);
    Serial.begin(115200);
    pinMode(ONBOARD_LED, OUTPUT);
    digitalWrite(ONBOARD_LED, HIGH);

    pinMode(PIN_OE, OUTPUT);
    pinMode(PIN_A,  OUTPUT);
    pinMode(PIN_B,  OUTPUT);
    pinMode(PIN_C,  OUTPUT);

    digitalWrite(PIN_OE, LOW);
    digitalWrite(PIN_A,  LOW);
    digitalWrite(PIN_B,  LOW);
    digitalWrite(PIN_C,  LOW);

    route_pins_to_gpio();

    delay(300);
    set_gpio_for_clk_pulses(PIN_LE, 15); // SDR mode

    bool dma_ok = init_lcd_dma();
    Serial.printf("[INIT] LCD_CAM GDMA Init: %s\n", dma_ok ? "OK" : "FAILED");
}

// Profiling accumulators
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
    route_pins_to_gpio();
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

    // 2. Scan all 32 row pairs with hardware GDMA transfer & high PWM duty cycle
    for (uint32_t line = 0; line < ROWS; line++) {
        // Step A: Clock DP32020A and BLANK all PMOS outputs (DCK=1)
        if (line == 0) {
            GPIO.out_w1ts = (1UL << PIN_C); // DIN = 1
            esp_rom_delay_us(1);
            GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (Blanking active: all PMOS OFF)
            esp_rom_delay_us(1);
            GPIO.out_w1tc = (1UL << PIN_C); // Clear DIN
        } else {
            GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (Blanking active: all PMOS OFF)
            esp_rom_delay_us(1);
        }

        // Connect CLK, LE, and RGB lines to hardware LCD_CAM
        route_pins_to_lcd_cam();

        // Step B: Hardware GDMA transmission of exactly 2048 bytes
        int64_t t_rgb_start = esp_timer_get_time();
        s_dma_desc.buffer = &s_dma_frame[line * 2048];
        s_dma_desc.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
        s_dma_desc.dw0.suc_eof = 1;
        s_dma_desc.dw0.length = 2048;

        gdma_reset(s_dma_chan);
        LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
        LCD_CAM.lcd_user.lcd_update = 1;

        gdma_start(s_dma_chan, (intptr_t)&s_dma_desc);
        LCD_CAM.lcd_user.lcd_start = 1;

        // Hardware automatically clears lcd_start when exactly 2048 cycles complete:
        while (LCD_CAM.lcd_user.lcd_start);

        int64_t t_rgb_end = esp_timer_get_time();
        frame_rgb_time += (t_rgb_end - t_rgb_start);

        // Switch CLK and LE back to GPIO for row handshake and PWM
        esp_rom_gpio_connect_out_signal(PIN_CLK, SIG_GPIO_OUT_IDX, false, false);
        esp_rom_gpio_connect_out_signal(PIN_LE,  SIG_GPIO_OUT_IDX, false, false);

        // Step C: DP3356 Row Change Handshake on OE
        int64_t t_rc_start = esp_timer_get_time();
        dp3364_change_row(line);
        int64_t t_rc_end = esp_timer_get_time();
        frame_row_change_time += (t_rc_end - t_rc_start);

        // Step D: UNBLANK PMOS (DCK=0): Turn ON current row for PWM emission
        GPIO.out_w1tc = (1UL << PIN_A);
        esp_rom_delay_us(1);

        // Step E: High-Duty PWM timing burst:
        int64_t t_pwm_start = esp_timer_get_time();
        for (uint32_t i = 0; i < PWM_BURST_CHUNKS; i++) {
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

    // Print profiling report once every 50 frames
    if (profile_frames >= 50) {
        float avg_frame = (float)sum_frame_time / profile_frames;
        float avg_fps = 1000000.0f / avg_frame;
        float avg_line_rgb = (float)sum_rgb_time / (profile_frames * 32);
        float avg_row_change = (float)sum_row_change_time / (profile_frames * 32);
        float avg_pwm = (float)sum_pwm_time / (profile_frames * 32);
        float avg_sync = (float)sum_sync_time / profile_frames;

        Serial.printf("[GDMA FIXED] Frame: %.1f us | FPS: %.1f | DMA: %.2f us | PWM: %.2f us | Duty: %.1f%%\n",
                      avg_frame, avg_fps, avg_line_rgb, avg_pwm, (avg_pwm / (avg_line_rgb + avg_pwm + 2.5f)) * 100.0f);

        sum_frame_time = 0;
        sum_rgb_time = 0;
        sum_row_change_time = 0;
        sum_pwm_time = 0;
        sum_sync_time = 0;
        profile_frames = 0;
    }
}
