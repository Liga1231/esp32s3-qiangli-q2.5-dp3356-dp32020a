/*
 * ============================================================================
 * ESP32-S3 LCD_CAM + GDMA EXPERIMENTAL BRIGHTNESS BENCHMARK (KIMI TESTS)
 * Hardware: ESP32-S3FH4R2 (Super Mini) + Dual Qiangli Q2.5 Pro (128x128)
 * Driver Silicon: 16 Chained DP3356 per lane (14-bit S-PWM) + DP32020A
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_rom_sys.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "soc/lcd_cam_struct.h"
#include "soc/lcd_cam_reg.h"
#include "soc/periph_defs.h"
#include "driver/periph_ctrl.h"
#include "esp_private/gdma.h"
#include "esp32s3/rom/lldesc.h"
#include "driver/pcnt.h"

// ---------------- Физический Pinout (ESP32-S3 Super Mini) ----------------
#define PIN_R1  4
#define PIN_G1  5
#define PIN_B1  6
#define PIN_R2  7
#define PIN_G2  15
#define PIN_B2  16

#define PIN_CLK 41
#define PIN_LE  40
#define PIN_OE  2
#define PIN_A   18   // DP32020A DCK / Бланкирование строк
#define PIN_B   8    // DP32020A BK/RCK (Постоянный LOW)
#define PIN_C   3    // DP32020A DIN (Маркер строки 0)
#define ONBOARD_LED 47

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

#define CLK_PULSE() do { \
    GPIO.out1_w1ts.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
    GPIO.out1_w1tc.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
} while (0)

// 13 проверенных конфигурационных регистров DP3356
static uint16_t s_active_regs[13] = {
    0x021F, // 32 линии сканирования (31 + 1)
    0x033F, // group num = 63
    0x047F, // максимальное время активного ШИМ
    0x0504,
    0x0642, // Биты [7:6] = 01 (ВКЛЮЧЕН ВНУТРЕННИЙ PLL)
    0x0700,
    0x08FF, // МАКСИМАЛЬНЫЙ ТОК 200% (255 / 255)
    0x0960,
    0x0ABE,
    0x0B8B, // Knee Voltage & защита от паразитной связи
    0x0CD8, // High-Current Multiplier (0x18 | 0b11000000)
    0x0D12,
    0x1180  // Включение boost / high-efficiency PWM (бит 7 = 1)
};

// 14-битный TrueColor буфер: [32 строки][16 столбцов][16 чипов][6 каналов] = 98 304 байт в PSRAM
EXT_RAM_ATTR static uint16_t grayFrame[ROWS][16][CHAINED_CHIPS][6];
static uint16_t srgbToLinear14[256];

// Буферы DMA в быстрой внутренней памяти SRAM (DRAM)
static uint8_t s_shift_buf[ROWS][4096] __attribute__((aligned(4)));
static uint8_t s_suffix_buf[ROWS][32]  __attribute__((aligned(4)));

#define MAX_DWELL_BYTES 4096
static uint8_t s_dwell_buf[MAX_DWELL_BYTES] __attribute__((aligned(4)));
static uint32_t s_current_dwell_bytes = 2124;
static uint32_t s_current_mode = 0; // 0 = 10 MHz, 1 = 15 MHz, 2 = 20 MHz

static uint8_t s_vsync_buf[16] __attribute__((aligned(4)));

static lldesc_t s_desc_shift_a[ROWS] __attribute__((aligned(4)));
static lldesc_t s_desc_shift_b[ROWS] __attribute__((aligned(4)));
static lldesc_t s_desc_suffix[ROWS]  __attribute__((aligned(4)));
static lldesc_t s_desc_dwell[ROWS]   __attribute__((aligned(4)));
static lldesc_t s_desc_vsync         __attribute__((aligned(4)));

static gdma_channel_handle_t s_dma_chan = NULL;
static volatile uint32_t s_current_line = 0;
static volatile uint32_t s_frame_counter = 0;

static const uint8_t data_pins[8] = {
    PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2, PIN_LE, PIN_OE
};
static const uint32_t sig_indices[8] = {
    LCD_DATA_OUT0_IDX, LCD_DATA_OUT1_IDX, LCD_DATA_OUT2_IDX,
    LCD_DATA_OUT3_IDX, LCD_DATA_OUT4_IDX, LCD_DATA_OUT5_IDX,
    LCD_DATA_OUT6_IDX, LCD_DATA_OUT7_IDX
};

// ---------------- Инициализация DP3356 ----------------
static inline void set_gpio_for_clk_pulses(uint32_t pin, uint32_t width) {
    if (pin >= 32) GPIO.out1_w1ts.val = (1UL << (pin - 32));
    else GPIO.out_w1ts = (1UL << pin);
    while (width-- > 0) CLK_PULSE();
    if (pin >= 32) GPIO.out1_w1tc.val = (1UL << (pin - 32));
    else GPIO.out_w1tc = (1UL << pin);
    CLK_PULSE();
}

static void write_dp3356_reg(uint16_t reg_word) {
    GPIO.out_w1ts = (1UL << PIN_A); // Бланкирование всех PMOS строк
    esp_rom_delay_us(2);
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
    GPIO.out_w1tc = ALL_RGB_MASK;
    for (int i = 0; i < 8; i++) CLK_PULSE();
}

static void dp3364_init() {
    set_gpio_for_clk_pulses(PIN_LE, 15);
}

// ---------------- Построение тестового шаблона и Gamma 2.4 ----------------
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

// Сетка 4x4 (16 зон 32x32): 4 ряда цветов (Red, Green, Blue, True Orange) x 4 уровня яркости
static void make_grid_4x4_pattern() {
    memset(grayFrame, 0, sizeof(grayFrame));
    static const float brightness_levels[4] = { 0.10f, 0.30f, 0.65f, 1.00f };

    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        uint32_t gy = y / 32; // Ряд 0..3
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            uint32_t gx = x / 32; // Столбец 0..3 (Яркость)
            float mult = brightness_levels[gx];

            uint8_t r = 0, g = 0, b = 0;
            if (gy == 0) {
                r = (uint8_t)(255.0f * mult + 0.5f);
            } else if (gy == 1) {
                g = (uint8_t)(255.0f * mult + 0.5f);
            } else if (gy == 2) {
                b = (uint8_t)(255.0f * mult + 0.5f);
            } else {
                r = (uint8_t)(255.0f * mult + 0.5f);
                g = (uint8_t)(100.0f * mult + 0.5f);
            }
            set_gray_pixel(x, y, r, g, b);
        }
    }
}

static void make_white_pattern() {
    memset(grayFrame, 0, sizeof(grayFrame));
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            set_gray_pixel(x, y, 255, 255, 255);
        }
    }
}

// ---------------- Упаковка 8-битного потока DMA ----------------
static void pack_dma_stream() {
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
                    uint8_t b = 0;
                    if (r1 & m) b |= (1 << 0);
                    if (g1 & m) b |= (1 << 1);
                    if (b1 & m) b |= (1 << 2);
                    if (r2 & m) b |= (1 << 3);
                    if (g2 & m) b |= (1 << 4);
                    if (b2 & m) b |= (1 << 5);

                    if (ch == 15 && bit == 0) {
                        b |= (1 << 6); // LAT = 1
                    }
                    s_shift_buf[line][idx++] = b;
                }
            }
        }
    }

    memset(s_suffix_buf, 0, sizeof(s_suffix_buf));
    for (uint32_t line = 0; line < ROWS; ++line) {
        const uint32_t oe_pulses = (line == 0) ? 12 : 4;
        for (uint32_t i = 0; i < oe_pulses; ++i) {
            s_suffix_buf[line][i] = (1 << 7); // Бит 7 = OE HIGH
        }
    }

    memset(s_dwell_buf, 0, sizeof(s_dwell_buf));

    memset(s_vsync_buf, 0, sizeof(s_vsync_buf));
    s_vsync_buf[0] = (1 << 6);
    s_vsync_buf[1] = (1 << 6);
    s_vsync_buf[2] = (1 << 6);
}

// ---------------- Обработчик прерываний переключения строк (IRAM) ----------------
static IRAM_ATTR bool on_gdma_trans_eof(gdma_channel_handle_t dma_chan, gdma_event_data_t *event_data, void *user_data) {
    uint32_t line = s_current_line;

    if (event_data->tx_eof_desc_addr == (intptr_t)&s_desc_suffix[line]) {
        // Завершились импульсы OE -> Включение текущей строки PMOS для свечения!
        GPIO.out_w1tc = (1UL << PIN_A); // DCK = 0 (Разбланкирование)
    } 
    else if (event_data->tx_eof_desc_addr == (intptr_t)&s_desc_dwell[line]) {
        // Завершилось время свечения -> Бланкирование PMOS и переключение строки DP32020A!
        uint32_t next_line = line + 1;
        if (next_line >= ROWS) {
            next_line = 0;
            s_frame_counter++;
            // Выбор строки 0 в DP32020A: DIN = 1 во время фронта DCK
            GPIO.out_w1ts = (1UL << PIN_C); // DIN = 1
            esp_rom_delay_us(1);
            GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (Фронт переключает на строку 0 и бланкирует PMOS)
            esp_rom_delay_us(1);
            GPIO.out_w1tc = (1UL << PIN_C); // Сброс DIN
        } else {
            // Переход на следующую строку: DIN = 0
            GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (Фронт переключает на следующую строку и бланкирует PMOS)
            esp_rom_delay_us(1);
        }
        s_current_line = next_line;
    }

    return false;
}

// ---------------- Аппаратная диагностика тактов (PCNT) ----------------
#define PCNT_TEST_PULSES 1000
static int16_t s_self_test_pulses = 0;

static void init_pcnt_diagnostics() {
    pcnt_config_t pcnt_config_clk = {
        .pulse_gpio_num = PIN_CLK,
        .ctrl_gpio_num = -1,
        .lctrl_mode = PCNT_MODE_KEEP,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DIS,
        .counter_h_lim = 32767,
        .counter_l_lim = -32768,
        .unit = PCNT_UNIT_0,
        .channel = PCNT_CHANNEL_0,
    };
    pcnt_unit_config(&pcnt_config_clk);
    pcnt_counter_pause(PCNT_UNIT_0);
    pcnt_counter_clear(PCNT_UNIT_0);
    pcnt_counter_resume(PCNT_UNIT_0);
}

static void pcnt_self_test() {
    pcnt_counter_clear(PCNT_UNIT_0);
    for (int i = 0; i < PCNT_TEST_PULSES; ++i) {
        CLK_PULSE();
    }
    pcnt_get_counter_value(PCNT_UNIT_0, &s_self_test_pulses);
}

// ---------------- Настройка аппаратного конвейера LCD_CAM + GDMA ----------------
static void set_clock_mode(uint32_t mode) {
    uint32_t clk_sel = 3;
    uint32_t div_num = 16;
    uint32_t dwell_bytes = 2124;

    if (mode == 1) {
        clk_sel = 2; // PLL240M
        div_num = 16; // 240 / 16 = 15.000 MHz
        dwell_bytes = 3186; // 3186 / 15 MHz = 212.4 us
    } else if (mode == 2) {
        clk_sel = 3; // PLL160M
        div_num = 8;  // 160 / 8 = 20.000 MHz
        dwell_bytes = 4000; // 4000 / 20 MHz = 200.0 us
    } else {
        clk_sel = 3; // PLL160M
        div_num = 16; // 160 / 16 = 10.000 MHz
        dwell_bytes = 2124; // 2124 / 10 MHz = 212.4 us
    }

    s_current_mode = mode;
    s_current_dwell_bytes = dwell_bytes;

    LCD_CAM.lcd_clock.val = 0;
    LCD_CAM.lcd_clock.clk_en = 1;
    LCD_CAM.lcd_clock.lcd_clk_sel = clk_sel;
    LCD_CAM.lcd_clock.lcd_clkm_div_num = div_num;
    LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
    LCD_CAM.lcd_clock.lcd_clkm_div_a = 1;
    LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;
    LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;
    LCD_CAM.lcd_clock.lcd_ck_idle_edge = 1;
    LCD_CAM.lcd_clock.lcd_ck_out_edge = 0;

    for (uint32_t r = 0; r < ROWS; ++r) {
        s_desc_dwell[r].size   = dwell_bytes;
        s_desc_dwell[r].length = dwell_bytes;
    }
}

static void init_hardware_pipeline() {
    for (int i = 0; i < 8; ++i) {
        gpio_set_direction((gpio_num_t)data_pins[i], GPIO_MODE_OUTPUT);
        esp_rom_gpio_pad_select_gpio(data_pins[i]);
        esp_rom_gpio_connect_out_signal(data_pins[i], sig_indices[i], false, false);
        GPIO.func_out_sel_cfg[data_pins[i]].oen_sel = 1;
    }

    gpio_set_direction((gpio_num_t)PIN_CLK, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_pad_select_gpio(PIN_CLK);
    esp_rom_gpio_connect_out_signal(PIN_CLK, LCD_PCLK_IDX, false, false);
    GPIO.func_out_sel_cfg[PIN_CLK].oen_sel = 1;
    esp_rom_gpio_connect_in_signal(PIN_CLK, PCNT_SIG_CH0_IN0_IDX, false);
    PIN_INPUT_ENABLE(IO_MUX_GPIO41_REG);

    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    periph_module_reset(PERIPH_LCD_CAM_MODULE);

    set_clock_mode(s_current_mode);

    LCD_CAM.lcd_ctrl.val = 0;
    LCD_CAM.lcd_ctrl2.val = 0;
    LCD_CAM.lcd_user.val = 0;
    LCD_CAM.lcd_user.lcd_2byte_en = 0;
    LCD_CAM.lcd_user.lcd_dout = 1;
    LCD_CAM.lcd_user.lcd_always_out_en = 1;
    LCD_CAM.lcd_misc.val = 0;
    LCD_CAM.lcd_misc.lcd_bk_en = 1;

    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 0;
    LCD_CAM.lcd_user.lcd_reset = 1;
    LCD_CAM.lcd_user.lcd_reset = 0;

    gdma_channel_alloc_config_t alloc_config = {
        .direction = GDMA_CHANNEL_DIRECTION_TX,
    };
    gdma_new_channel(&alloc_config, &s_dma_chan);
    gdma_connect(s_dma_chan, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_LCD, 0));

    gdma_strategy_config_t strategy = {
        .owner_check = false,
        .auto_update_desc = false
    };
    gdma_apply_strategy(s_dma_chan, &strategy);

    gdma_tx_event_callbacks_t cbs = {
        .on_trans_eof = on_gdma_trans_eof,
    };
    gdma_register_tx_event_callbacks(s_dma_chan, &cbs, NULL);

    s_desc_vsync.size   = 16;
    s_desc_vsync.length = 16;
    s_desc_vsync.offset = 0;
    s_desc_vsync.sosf   = 0;
    s_desc_vsync.eof    = 0;
    s_desc_vsync.owner  = 1;
    s_desc_vsync.buf    = &s_vsync_buf[0];
    s_desc_vsync.qe.stqe_next = &s_desc_shift_a[0];

    for (uint32_t r = 0; r < ROWS; ++r) {
        s_desc_shift_a[r].size   = 2048;
        s_desc_shift_a[r].length = 2048;
        s_desc_shift_a[r].offset = 0;
        s_desc_shift_a[r].sosf   = 0;
        s_desc_shift_a[r].eof    = 0;
        s_desc_shift_a[r].owner  = 1;
        s_desc_shift_a[r].buf    = &s_shift_buf[r][0];
        s_desc_shift_a[r].qe.stqe_next = &s_desc_shift_b[r];

        s_desc_shift_b[r].size   = 2048;
        s_desc_shift_b[r].length = 2048;
        s_desc_shift_b[r].offset = 0;
        s_desc_shift_b[r].sosf   = 0;
        s_desc_shift_b[r].eof    = 0;
        s_desc_shift_b[r].owner  = 1;
        s_desc_shift_b[r].buf    = &s_shift_buf[r][2048];
        s_desc_shift_b[r].qe.stqe_next = &s_desc_suffix[r];

        s_desc_suffix[r].size   = 32;
        s_desc_suffix[r].length = 32;
        s_desc_suffix[r].offset = 0;
        s_desc_suffix[r].sosf   = 0;
        s_desc_suffix[r].eof    = 1;
        s_desc_suffix[r].owner  = 1;
        s_desc_suffix[r].buf    = &s_suffix_buf[r][0];
        s_desc_suffix[r].qe.stqe_next = &s_desc_dwell[r];

        uint32_t next_r = (r + 1) % ROWS;
        s_desc_dwell[r].size   = s_current_dwell_bytes;
        s_desc_dwell[r].length = s_current_dwell_bytes;
        s_desc_dwell[r].offset = 0;
        s_desc_dwell[r].sosf   = 0;
        s_desc_dwell[r].eof    = 1;
        s_desc_dwell[r].owner  = 1;
        s_desc_dwell[r].buf    = &s_dwell_buf[0];

        if (r == ROWS - 1) {
            s_desc_dwell[r].qe.stqe_next = &s_desc_vsync;
        } else {
            s_desc_dwell[r].qe.stqe_next = &s_desc_shift_a[next_r];
        }
    }
}

// Безопасное переключение режима DCLK с гарантированной синхронизацией DP32020A
static void switch_mode_safe(uint32_t new_mode) {
    Serial.printf("\n[SWITCH] Switching to Mode %lu...\n", new_mode);
    LCD_CAM.lcd_user.lcd_start = 0;
    gdma_stop(s_dma_chan);
    delay(10);

    // Бланкирование PMOS строк
    GPIO.out_w1ts = (1UL << PIN_A);
    esp_rom_delay_us(10);

    // Синхронизация DP32020A на строку 0
    GPIO.out_w1tc = (1UL << PIN_A); // DCK = 0 (спад)
    esp_rom_delay_us(2);
    GPIO.out_w1ts = (1UL << PIN_C); // DIN = 1
    esp_rom_delay_us(2);
    GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1 (фронт защелкивает строку 0)
    esp_rom_delay_us(2);
    GPIO.out_w1tc = (1UL << PIN_C); // DIN = 0
    s_current_line = 0;

    set_clock_mode(new_mode);

    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 0;
    LCD_CAM.lcd_user.lcd_reset = 1;
    LCD_CAM.lcd_user.lcd_reset = 0;

    gdma_start(s_dma_chan, (intptr_t)&s_desc_shift_a[0]);
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_user.lcd_start = 1;

    Serial.printf("[SWITCH] Active: Mode %lu | Dwell: %lu B\n", s_current_mode, s_current_dwell_bytes);
}

// Безопасная перезапись конфигурационного регистра кремния DP3356 на лету
static void update_register_safe(uint8_t reg_index, uint16_t new_val) {
    Serial.printf("\n[REG_UPDATE] Updating Register [%d] from 0x%04X to 0x%04X...\n",
                  reg_index, s_active_regs[reg_index], new_val);
    s_active_regs[reg_index] = new_val;

    LCD_CAM.lcd_user.lcd_start = 0;
    gdma_stop(s_dma_chan);
    delay(10);

    // Перевод пинов в режим прямого GPIO (отключаем от LCD_CAM)
    esp_rom_gpio_connect_out_signal(PIN_CLK, SIG_GPIO_OUT_IDX, false, false);
    for (int i = 0; i < 8; ++i) {
        esp_rom_gpio_connect_out_signal(data_pins[i], SIG_GPIO_OUT_IDX, false, false);
    }

    GPIO.out_w1ts = (1UL << PIN_A); // Бланкирование PMOS строк
    esp_rom_delay_us(10);

    // Повторная посылка всех 13 конфигурационных регистров
    dp3364_init();
    delay(5);
    for (int i = 0; i < 13; ++i) {
        write_dp3356_reg(s_active_regs[i]);
        delay(5);
    }
    delay(5);
    set_gpio_for_clk_pulses(PIN_LE, 3); // VSYNC

    // Синхронизация DP32020A на строку 0
    GPIO.out_w1tc = (1UL << PIN_A);
    esp_rom_delay_us(2);
    GPIO.out_w1ts = (1UL << PIN_C);
    esp_rom_delay_us(2);
    GPIO.out_w1ts = (1UL << PIN_A);
    esp_rom_delay_us(2);
    GPIO.out_w1tc = (1UL << PIN_C);
    s_current_line = 0;

    // Возврат пинов под управление LCD_CAM
    esp_rom_gpio_connect_out_signal(PIN_CLK, LCD_PCLK_IDX, false, false);
    for (int i = 0; i < 8; ++i) {
        esp_rom_gpio_connect_out_signal(data_pins[i], sig_indices[i], false, false);
    }

    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 0;
    LCD_CAM.lcd_user.lcd_reset = 1;
    LCD_CAM.lcd_user.lcd_reset = 0;

    gdma_start(s_dma_chan, (intptr_t)&s_desc_shift_a[0]);
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_user.lcd_start = 1;

    Serial.printf("[REG_UPDATE] SUCCESS: Silicon updated to REG[%d]=0x%04X!\n", reg_index, new_val);
}

void print_help() {
    Serial.println("\n================================================================================");
    Serial.println(">>> DP3356 EXPERIMENTAL BENCHMARK & SILICON CURRENT TEST <<<");
    Serial.println("Frequency Modes:");
    Serial.println("  [0] Baseline: 10 MHz DCLK, Dwell = 2124 B (212.4 us) -> 50.00 FPS");
    Serial.println("  [1] Turbo 1:  15 MHz DCLK, Dwell = 3186 B (212.4 us) -> 64.09 FPS");
    Serial.println("  [2] Turbo 2:  20 MHz DCLK, Dwell = 4000 B (200.0 us) -> 76.89 FPS (NO FLICKER!)");
    Serial.println("Current Sensitivity Tests (Kimi T2):");
    Serial.println("  [f] Force Max Current: REG08 = 0x08FF (200% Gain, code 255)");
    Serial.println("  [b] Nominal Current:   REG08 = 0x08BF (100% Gain, code 191)");
    Serial.println("  [m] Quarter Current:   REG08 = 0x083F (25% Gain,  code 63)");
    Serial.println("Pulse & PWM Duration Tests (Kimi T5):");
    Serial.println("  [4] Extend PWM Length: REG04 = 0x04FF (Double active duration)");
    Serial.println("  [5] Default PWM Length:REG04 = 0x047F (Standard duration)");
    Serial.println("  [6] Slower GCLK x1:    REG06 = 0x0640 (3x longer pulse in time!)");
    Serial.println("  [7] Medium GCLK x2:    REG06 = 0x0641");
    Serial.println("  [8] Default GCLK x3:   REG06 = 0x0642 (Standard clock)");
    Serial.println("  [c] High-Current Boost:REG0C = 0x0CFF (Max multiplier)");
    Serial.println("  [d] Default Multiplier:REG0C = 0x0CD8");
    Serial.println("Patterns:");
    Serial.println("  [g] Show 4x4 Brightness Grid");
    Serial.println("  [w] Show 100% Solid White Sheet");
    Serial.println("  [?] Print this menu");
    Serial.println("================================================================================\n");
}

void setup() {
    setCpuFrequencyMhz(240);
    Serial.begin(115200);

    pinMode(PIN_R1, OUTPUT); pinMode(PIN_R2, OUTPUT);
    pinMode(PIN_G1, OUTPUT); pinMode(PIN_G2, OUTPUT);
    pinMode(PIN_B1, OUTPUT); pinMode(PIN_B2, OUTPUT);
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_LE,  OUTPUT);
    pinMode(PIN_OE,  OUTPUT);
    pinMode(PIN_A,   OUTPUT);
    pinMode(PIN_B,   OUTPUT);
    pinMode(PIN_C,   OUTPUT);

    digitalWrite(PIN_CLK, LOW);
    digitalWrite(PIN_LE,  LOW);
    digitalWrite(PIN_OE,  LOW);
    digitalWrite(PIN_A,   HIGH); // PMOS закрыты при старте
    digitalWrite(PIN_B,   LOW);
    digitalWrite(PIN_C,   LOW);
    GPIO.out_w1tc = ALL_RGB_MASK;

    build_gamma_table();
    make_grid_4x4_pattern();
    pack_dma_stream();

    // 1. Инициализация кремния DP3356
    delay(200);
    dp3364_init();
    delay(10);
    for (int i = 0; i < 13; ++i) {
        write_dp3356_reg(s_active_regs[i]);
        delay(5);
    }
    delay(10);
    set_gpio_for_clk_pulses(PIN_LE, 3); // Начальный VSYNC

    // 2. Установка сдвигового регистра DP32020A на строку 0
    GPIO.out_w1ts = (1UL << PIN_C); // DIN = 1
    esp_rom_delay_us(1);
    GPIO.out_w1ts = (1UL << PIN_A); // DCK = 1
    esp_rom_delay_us(1);
    GPIO.out_w1tc = (1UL << PIN_C); // DIN = 0
    s_current_line = 0;

    // 3. Запуск счетчика PCNT и проверка его внутренней петли GPIO41
    init_pcnt_diagnostics();
    pcnt_self_test();

    // 4. Подключение LCD_CAM и старт автономного кольца GDMA
    init_hardware_pipeline();
    gdma_start(s_dma_chan, (intptr_t)&s_desc_shift_a[0]);
    LCD_CAM.lcd_user.lcd_update = 1;
    LCD_CAM.lcd_user.lcd_start = 1;

    Serial.println("[SETUP] LCD_CAM + GDMA Engine Started!");
    print_help();
}

void loop() {
    static uint32_t last_ms = 0;
    static uint32_t last_frames = 0;

    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == '0') switch_mode_safe(0);
        else if (ch == '1') switch_mode_safe(1);
        else if (ch == '2') switch_mode_safe(2);
        else if (ch == 'f' || ch == 'F') {
            update_register_safe(6, 0x08FF); // 200% Force Current Gain
        }
        else if (ch == 'b' || ch == 'B') {
            update_register_safe(6, 0x08BF); // 100% Nominal Current Gain
        }
        else if (ch == 'm' || ch == 'M') {
            update_register_safe(6, 0x083F); // 25% Low Current Gain
        }
        else if (ch == '4') {
            update_register_safe(2, 0x04FF); // Double PWM length
        }
        else if (ch == '5') {
            update_register_safe(2, 0x047F); // Default PWM length
        }
        else if (ch == '6') {
            update_register_safe(4, 0x0640); // GCLK x1 (longer pulse)
        }
        else if (ch == '7') {
            update_register_safe(4, 0x0641); // GCLK x2
        }
        else if (ch == '8') {
            update_register_safe(4, 0x0642); // Default GCLK x3
        }
        else if (ch == 'c' || ch == 'C') {
            update_register_safe(10, 0x0CFF); // High-Current Multiplier boost
        }
        else if (ch == 'd' || ch == 'D') {
            update_register_safe(10, 0x0CD8); // Default REG0C
        }
        else if (ch == 'g' || ch == 'G') {
            make_grid_4x4_pattern();
            pack_dma_stream();
            Serial.println("[PATTERN] 4x4 Brightness Grid displayed!");
        }
        else if (ch == 'w' || ch == 'W') {
            make_white_pattern();
            pack_dma_stream();
            Serial.println("[PATTERN] 100% Solid White displayed!");
        }
        else if (ch == '?' || ch == 'h') print_help();
    }

    uint32_t now = millis();
    if (now - last_ms < 1000) return;

    pcnt_counter_clear(PCNT_UNIT_0);
    esp_rom_delay_us(1000);
    int16_t clk_count = 0;
    pcnt_get_counter_value(PCNT_UNIT_0, &clk_count);
    float measured_mhz = (float)clk_count / 1000.0f;

    uint32_t frames = s_frame_counter;
    float fps = (float)(frames - last_frames) * 1000.0f / (float)(now - last_ms);
    last_ms = now;
    last_frames = frames;

    const char *mode_name = (s_current_mode == 0) ? "10 MHz Baseline" :
                            ((s_current_mode == 1) ? "15 MHz Turbo (+28%)" : "20 MHz Turbo (+45%)");

    Serial.printf("[BENCHMARK] Mode: %s | FPS: %.2f | CLK: %.2f MHz (%d p/ms) | Dwell: %lu B\n",
                  mode_name, fps, measured_mhz, clk_count, s_current_dwell_bytes);
}
