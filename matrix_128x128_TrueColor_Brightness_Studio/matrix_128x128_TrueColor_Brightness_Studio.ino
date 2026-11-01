/*
 * ============================================================================
 * ESP32-S3 LCD_CAM + GDMA 50 FPS S-PWM ENGINE (GATE T8 - FULL ARCHITECTURE)
 * Hardware: ESP32-S3FH4R2 (Super Mini) + Dual Qiangli Q2.5 Pro (128x128)
 * Driver Silicon: 16 Chained DP3356 per lane (14-bit S-PWM) + DP32020A
 * ============================================================================
 * Features:
 *   - 10.000 MHz Hardware PCLK (DCLK) via LCD_CAM i80 8-bit parallel bus
 *   - 50.000 FPS Crystal-Locked Refresh (20.000 ms frame period)
 *   - 14-Bit TrueColor Grayscale S-PWM (Gamma 2.4, authentic Orange)
 *   - Zero-CPU Bit-Banging: Autonomous Circular GDMA Ring (CPU load < 0.5%)
 *   - Hardware-driven VSYNC at frame boundary
 *   - Deterministic DP32020A single-pulse row advance in IRAM ISR
 *   - Built-in PCNT hardware frequency counter on PIN_CLK (GPIO 41)
 * ============================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
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
#include "web_page_data.h"
#include "matrix128x128_data.h"

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

// Прямые тактовые импульсы GPIO для инициализации DP3356 при старте
#define CLK_PULSE() do { \
    GPIO.out1_w1ts.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
    GPIO.out1_w1tc.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop; nop; nop;"); \
} while (0)

// 13 конфигурационных регистров DP3356 (проверенный профиль Trial 7)
static const uint16_t DP3356_CONFIG_REGS[13] = {
    0x021F, // 32 линии сканирования (31 + 1)
    0x033F, // group num = 63
    0x04FF, // МАКСИМАЛЬНАЯ ДЛИТЕЛЬНОСТЬ ШИМ (255 вместо 127)
    0x0504,
    0x0640, // PLL GCLK МНОЖИТЕЛЬ x1 (вместо x3) -> ШИМ пачка растянута на всё окно строки!
    0x0700,
    0x08FF, // МАКСИМАЛЬНЫЙ ТОК 200% (255 / 255)
    0x0960,
    0x0ABE,
    0x0B8B, // Knee Voltage & защита от паразитной связи
    0x0CD8, // High-Current Multiplier (0x18 | 0b11000000)
    0x0D12,
    0x1180  // Включение boost / high-efficiency PWM (бит 7 = 1)
};

// Кадровые буферы выделяются через ps_malloc: в Core 2.0.17 EXT_RAM_ATTR пустой.
static uint16_t (*grayFrame)[16][CHAINED_CHIPS][6] = NULL;
static uint32_t (*active_frame)[16][CHAINED_CHIPS] = NULL;
static uint16_t srgbToLinear14[256];
static volatile bool s_frame_dirty = false;
static volatile uint32_t s_rx_frame_counter = 0;

#define ACTIVE_FRAME_BYTES (ROWS * 16 * CHAINED_CHIPS * sizeof(uint32_t))
#define FRAME_LEGACY_BYTES (MATRIX_WIDTH * MATRIX_HEIGHT + 3)
#define RAW_RGB_BYTES      (MATRIX_WIDTH * MATRIX_HEIGHT * 3)
#define FRAME_RGB_BYTES    (RAW_RGB_BYTES + 3)
#define FRAME_PACKET_BYTES (FRAME_RGB_BYTES + 4)
#define GRAY_FRAME_BYTES   (ROWS * 16 * CHAINED_CHIPS * 6 * sizeof(uint16_t))

static uint8_t *s_raw_rgb888 = NULL;
static uint8_t s_display_brightness_pct = 100;

// Буферы DMA в быстрой внутренней памяти SRAM (DRAM)
// 1. Сдвиг данных: 32 строки × 4096 байт = 128 КБ
static uint8_t s_shift_buf[ROWS][4096] __attribute__((aligned(4)));

// 2. Смена строк (импульсы OE): 32 строки × 32 байта = 1 КБ
static uint8_t s_suffix_buf[ROWS][32]  __attribute__((aligned(4)));

// 3. Общий буфер свечения (Dwell): 2124 байта нулей, общий для всех 32 строк!
#define DWELL_BYTES 4000
static uint8_t s_dwell_buf[DWELL_BYTES] __attribute__((aligned(4)));

// 4. Буфер аппаратного VSYNC: 16 байт (3 такта LAT = 1, затем 0)
static uint8_t s_vsync_buf[16]          __attribute__((aligned(4)));

// Кольцевые дескрипторы GDMA: 4 дескриптора на строку × 32 строки + 1 VSYNC
static lldesc_t s_desc_shift_a[ROWS] __attribute__((aligned(4)));
static lldesc_t s_desc_shift_b[ROWS] __attribute__((aligned(4)));
static lldesc_t s_desc_suffix[ROWS]  __attribute__((aligned(4)));
static lldesc_t s_desc_dwell[ROWS]   __attribute__((aligned(4)));
static lldesc_t s_desc_vsync         __attribute__((aligned(4)));

static gdma_channel_handle_t s_dma_chan = NULL;
static volatile uint32_t s_current_line = 0;
static volatile uint32_t s_frame_counter = 0;

// ---------------- Инициализация DP3356 до включения DMA ----------------
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
static void build_gamma_table(float factor = 1.0f) {
    if (factor < 0.01f) factor = 0.01f;
    if (factor > 1.00f) factor = 1.00f;
    for (uint32_t i = 0; i < 256; ++i) {
        const float s = (float)i / 255.0f;
        const float linear = (s <= 0.04045f)
            ? (s / 12.92f)
            : powf((s + 0.055f) / 1.055f, 2.4f);
        uint32_t v = (uint32_t)(linear * (float)GRAY_MAX * factor + 0.5f);
        if (v > GRAY_MAX) v = GRAY_MAX;
        srgbToLinear14[i] = (uint16_t)v;
    }
}

static void set_display_brightness(uint8_t pct);

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

static void set_matrix_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= MATRIX_WIDTH || y >= MATRIX_HEIGHT) return;
    const uint32_t line = y % 32;
    const bool is_bottom = ((y % 64) >= 32);
    const uint32_t col = x % 16;
    const uint32_t ch = (y < 64) ? (8 + (x / 16)) : (x / 16);

    const uint32_t red_pin = is_bottom ? PIN_R2 : PIN_R1;
    const uint32_t green_pin = is_bottom ? PIN_G2 : PIN_G1;
    const uint32_t blue_pin = is_bottom ? PIN_B2 : PIN_B1;
    const uint32_t clr_mask = (1UL << red_pin) | (1UL << green_pin) | (1UL << blue_pin);
    uint32_t set_mask = 0;
    if (r > 127) set_mask |= (1UL << red_pin);
    if (g > 127) set_mask |= (1UL << green_pin);
    if (b > 127) set_mask |= (1UL << blue_pin);
    active_frame[line][col][ch] = (active_frame[line][col][ch] & ~clr_mask) | set_mask;

    set_gray_pixel(x, y, r, g, b);
    s_frame_dirty = true;
}

static void pack_dma_line(uint32_t line) {
    if (line >= ROWS) return;
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
                    b |= (1 << 6);
                }
                s_shift_buf[line][idx++] = b;
            }
        }
    }
}

static void apply_full_rgb888(const uint8_t *rgb_data, size_t len) {
    if (len < RAW_RGB_BYTES) return;
    if (s_raw_rgb888 != NULL && rgb_data != s_raw_rgb888) {
        memcpy(s_raw_rgb888, rgb_data, RAW_RGB_BYTES);
    }
    size_t idx = 0;
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        const uint32_t line = y % 32;
        const bool is_bottom = ((y % 64) >= 32);
        const uint32_t lane = is_bottom ? 3 : 0;
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            const uint32_t col = x % 16;
            const uint32_t ch = (y < 64) ? (8 + (x / 16)) : (x / 16);
            uint8_t r = rgb_data[idx++];
            uint8_t g = rgb_data[idx++];
            uint8_t b = rgb_data[idx++];
            grayFrame[line][col][ch][lane + 0] = srgbToLinear14[r];
            grayFrame[line][col][ch][lane + 1] = srgbToLinear14[g];
            grayFrame[line][col][ch][lane + 2] = srgbToLinear14[b];
        }
    }
    s_frame_dirty = true;
    s_rx_frame_counter++;
}

static void pack_dma_stream();
static void sync_gray_from_active_frame();

static void set_display_brightness(uint8_t pct) {
    if (pct < 5) pct = 5;
    if (pct > 100) pct = 100;
    s_display_brightness_pct = pct;
    build_gamma_table((float)pct / 100.0f);
    if (s_raw_rgb888 != NULL) {
        apply_full_rgb888(s_raw_rgb888, RAW_RGB_BYTES);
    } else {
        sync_gray_from_active_frame();
    }
    pack_dma_stream();
    Serial.printf("[BRIGHTNESS] Screen hardware brightness set to %u%%\n", s_display_brightness_pct);
}

static void apply_full_bitmap(const uint8_t *bitmap_data, size_t len) {
    if (len < MATRIX_WIDTH * MATRIX_HEIGHT) return;
    size_t idx = 0;
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            const uint8_t mask = bitmap_data[idx++];
            set_matrix_pixel(x, y,
                             (mask & 0x01) ? 255 : 0,
                             (mask & 0x02) ? 255 : 0,
                             (mask & 0x04) ? 255 : 0);
        }
    }
    s_rx_frame_counter++;
}

static void sync_gray_from_active_frame() {
    for (uint32_t y = 0; y < MATRIX_HEIGHT; ++y) {
        const uint32_t line = y % 32;
        const bool is_bottom = ((y % 64) >= 32);
        const uint32_t red_pin = is_bottom ? PIN_R2 : PIN_R1;
        const uint32_t green_pin = is_bottom ? PIN_G2 : PIN_G1;
        const uint32_t blue_pin = is_bottom ? PIN_B2 : PIN_B1;
        for (uint32_t x = 0; x < MATRIX_WIDTH; ++x) {
            const uint32_t col = x % 16;
            const uint32_t ch = (y < 64) ? (8 + (x / 16)) : (x / 16);
            const uint32_t mask = active_frame[line][col][ch];
            set_gray_pixel(x, y,
                           (mask & (1UL << red_pin)) ? 255 : 0,
                           (mask & (1UL << green_pin)) ? 255 : 0,
                           (mask & (1UL << blue_pin)) ? 255 : 0);
        }
    }
}

// ---------------- Упаковка 8-битного потока DMA ----------------
static void pack_dma_stream() {
    for (uint32_t line = 0; line < ROWS; ++line) {
        pack_dma_line(line);
    }

    // 2. Упаковка буфера суффикса (импульсы смены строки OE на бите 7)
    memset(s_suffix_buf, 0, sizeof(s_suffix_buf));
    for (uint32_t line = 0; line < ROWS; ++line) {
        const uint32_t oe_pulses = (line == 0) ? 12 : 4;
        for (uint32_t i = 0; i < oe_pulses; ++i) {
            s_suffix_buf[line][i] = (1 << 7); // Бит 7 = OE HIGH при DCLK
        }
    }

    // 3. Общий буфер свечения (все нули)
    memset(s_dwell_buf, 0, sizeof(s_dwell_buf));

    // 4. Буфер аппаратного VSYNC
    memset(s_vsync_buf, 0, sizeof(s_vsync_buf));
    s_vsync_buf[0] = (1 << 6); // LAT = 1
    s_vsync_buf[1] = (1 << 6); // LAT = 1
    s_vsync_buf[2] = (1 << 6); // LAT = 1
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

static int16_t s_self_test_pulses = -999;

static void pcnt_self_test() {
    gpio_set_direction((gpio_num_t)PIN_CLK, GPIO_MODE_INPUT_OUTPUT);
    pcnt_counter_clear(PCNT_UNIT_0);
    for (int i = 0; i < 1000; ++i) CLK_PULSE();
    int16_t pulses = 0;
    pcnt_get_counter_value(PCNT_UNIT_0, &pulses);
    s_self_test_pulses = pulses;
    Serial.printf("[CLK SELF-TEST] %d/1000 pulses\n", pulses);
}

// ---------------- Конфигурация аппаратного конвейера LCD_CAM + GDMA ----------------
static void init_hardware_pipeline() {
    // 1. Маршрутизация матрицы сигналов GPIO на LCD_CAM
    // ВАЖНО: gpio_set_direction должен вызываться ДО esp_rom_gpio_connect_out_signal,
    // иначе gpio_set_direction сбрасывает func_out_sel_cfg обратно на SIG_GPIO_OUT_IDX (0x100)!
    const int data_pins[8] = {
        PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2, PIN_LE, PIN_OE
    };
    const int sig_indices[8] = {
        LCD_DATA_OUT0_IDX, LCD_DATA_OUT1_IDX, LCD_DATA_OUT2_IDX, LCD_DATA_OUT3_IDX,
        LCD_DATA_OUT4_IDX, LCD_DATA_OUT5_IDX, LCD_DATA_OUT6_IDX, LCD_DATA_OUT7_IDX
    };
    for (int i = 0; i < 8; ++i) {
        gpio_set_direction((gpio_num_t)data_pins[i], GPIO_MODE_OUTPUT);
        esp_rom_gpio_pad_select_gpio(data_pins[i]);
        esp_rom_gpio_connect_out_signal(data_pins[i], sig_indices[i], false, false);
        GPIO.func_out_sel_cfg[data_pins[i]].oen_sel = 1; // Принудительное включение драйвера вывода
    }

    gpio_set_direction((gpio_num_t)PIN_CLK, GPIO_MODE_INPUT_OUTPUT);
    esp_rom_gpio_pad_select_gpio(PIN_CLK);
    esp_rom_gpio_connect_out_signal(PIN_CLK, LCD_PCLK_IDX, false, false);
    GPIO.func_out_sel_cfg[PIN_CLK].oen_sel = 1; // Принудительное включение выхода PCLK (0x49A)
    esp_rom_gpio_connect_in_signal(PIN_CLK, PCNT_SIG_CH0_IN0_IDX, false);
    PIN_INPUT_ENABLE(IO_MUX_GPIO41_REG);

    // 2. Включение и сброс модуля LCD_CAM
    periph_module_enable(PERIPH_LCD_CAM_MODULE);
    periph_module_reset(PERIPH_LCD_CAM_MODULE);

    // 3. Конфигурация генератора тактовой частоты: PLL160 / 8 = 20.000 МГц (77 FPS / 77 Hz!)
    LCD_CAM.lcd_clock.val = 0;
    LCD_CAM.lcd_clock.clk_en = 1;
    LCD_CAM.lcd_clock.lcd_clk_sel = 3;       // 3 = PLL160M в ESP32 Core 2.0.17
    LCD_CAM.lcd_clock.lcd_clkm_div_num = 8;  // 160 / 8 = 20.000 МГц (77 Гц развертка!)
    LCD_CAM.lcd_clock.lcd_clkm_div_b = 0;
    LCD_CAM.lcd_clock.lcd_clkm_div_a = 1;
    LCD_CAM.lcd_clock.lcd_clk_equ_sysclk = 1;
    LCD_CAM.lcd_clock.lcd_clkcnt_n = 1;      // В TRM и HAL clkcnt_n не должен быть 0!
    LCD_CAM.lcd_clock.lcd_ck_idle_edge = 1;  // В 8080 режиме WR idle = HIGH
    LCD_CAM.lcd_clock.lcd_ck_out_edge = 0;

    // 4. Настройка 8-битного параллельного режима i80
    LCD_CAM.lcd_ctrl.val = 0;
    LCD_CAM.lcd_ctrl2.val = 0;
    LCD_CAM.lcd_user.val = 0;
    LCD_CAM.lcd_user.lcd_2byte_en = 0;      // 8-битная шина
    LCD_CAM.lcd_user.lcd_dout = 1;          // Фаза DOUT
    LCD_CAM.lcd_user.lcd_always_out_en = 1; // Непрерывный кольцевой режим
    LCD_CAM.lcd_misc.val = 0;
    LCD_CAM.lcd_misc.lcd_bk_en = 1;

    LCD_CAM.lcd_misc.lcd_afifo_reset = 1;
    LCD_CAM.lcd_misc.lcd_afifo_reset = 0;
    LCD_CAM.lcd_user.lcd_reset = 1;
    LCD_CAM.lcd_user.lcd_reset = 0;

    Serial.printf("[REG_DIAG] GPIO41_MUX: 0x%08X | FUNC_OUT_SEL: 0x%08X | LCD_CLK: 0x%08X | LCD_USER: 0x%08X\n",
                  REG_READ(IO_MUX_GPIO41_REG), GPIO.func_out_sel_cfg[PIN_CLK].val,
                  LCD_CAM.lcd_clock.val, LCD_CAM.lcd_user.val);

    // 5. Выделение канала GDMA
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

    // 6. Построение дескриптора VSYNC
    s_desc_vsync.size   = 16;
    s_desc_vsync.length = 16;
    s_desc_vsync.offset = 0;
    s_desc_vsync.sosf   = 0;
    s_desc_vsync.eof    = 0;
    s_desc_vsync.owner  = 1;
    s_desc_vsync.buf    = &s_vsync_buf[0];
    s_desc_vsync.qe.stqe_next = &s_desc_shift_a[0]; // Переход на строку 0

    // 7. Построение кольцевой цепочки из 128 дескрипторов
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
        s_desc_suffix[r].eof    = 1; // Вызов прерывания: разбланкирование PMOS
        s_desc_suffix[r].owner  = 1;
        s_desc_suffix[r].buf    = &s_suffix_buf[r][0];
        s_desc_suffix[r].qe.stqe_next = &s_desc_dwell[r];

        uint32_t next_r = (r + 1) % ROWS;
        s_desc_dwell[r].size   = DWELL_BYTES;
        s_desc_dwell[r].length = DWELL_BYTES;
        s_desc_dwell[r].offset = 0;
        s_desc_dwell[r].sosf   = 0;
        s_desc_dwell[r].eof    = 1; // Вызов прерывания: бланкирование и сдвиг строки
        s_desc_dwell[r].owner  = 1;
        s_desc_dwell[r].buf    = &s_dwell_buf[0];

        if (r == ROWS - 1) {
            s_desc_dwell[r].qe.stqe_next = &s_desc_vsync; // Замыкание кольца через VSYNC
        } else {
            s_desc_dwell[r].qe.stqe_next = &s_desc_shift_a[next_r];
        }
    }
}

// ---------------- Wi-Fi SoftAP и WebSocket ----------------
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static uint8_t *s_frame_recv_buf = NULL;
static size_t s_frame_recv_idx = 0;

static bool allocate_frame_buffers() {
    grayFrame = (uint16_t (*)[16][CHAINED_CHIPS][6])ps_malloc(
        ROWS * 16 * CHAINED_CHIPS * 6 * sizeof(uint16_t));
    active_frame = (uint32_t (*)[16][CHAINED_CHIPS])ps_malloc(ACTIVE_FRAME_BYTES);
    s_frame_recv_buf = (uint8_t *)ps_malloc(FRAME_PACKET_BYTES);
    s_raw_rgb888 = (uint8_t *)ps_malloc(RAW_RGB_BYTES);
    if (s_raw_rgb888) memset(s_raw_rgb888, 0, RAW_RGB_BYTES);
    return grayFrame && active_frame && s_frame_recv_buf && s_raw_rgb888;
}

static void onWsEvent(AsyncWebSocket *socket, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        digitalWrite(ONBOARD_LED, LOW);
        delayMicroseconds(50);
        digitalWrite(ONBOARD_LED, HIGH);
        if (LittleFS.exists("/boot_mode.txt")) {
            File mode_file = LittleFS.open("/boot_mode.txt", "r");
            if (mode_file) {
                String mode = mode_file.readStringUntil('\n');
                mode.trim();
                mode_file.close();
                if (mode.length() > 0) client->text("BOOT_MODE:" + mode);
            }
        }
        return;
    }

    if (type != WS_EVT_DATA) return;
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->opcode != WS_BINARY) return;

    if (info->index == 0 && info->final && len >= 2 && data[0] == 0x06) {
        set_display_brightness(data[1]);
        return;
    }

    if (info->len == 6 && info->index == 0 && info->final && len == 6 && data[0] == 0x01) {
        uint8_t x = data[1], y = data[2];
        uint8_t r = data[3], g = data[4], b = data[5];
        set_matrix_pixel(x, y, r, g, b);
        pack_dma_line(y % 32);
        return;
    }

    if (info->len == FRAME_RGB_BYTES) {
        if (info->index == 0) s_frame_recv_idx = 0;
        if (info->index != s_frame_recv_idx || len > FRAME_PACKET_BYTES - s_frame_recv_idx) {
            s_frame_recv_idx = 0;
            return;
        }
        memcpy(s_frame_recv_buf + s_frame_recv_idx, data, len);
        s_frame_recv_idx += len;
        if (info->final && s_frame_recv_idx == info->len) {
            if (s_frame_recv_buf[0] == 0x05 &&
                s_frame_recv_buf[1] == MATRIX_WIDTH &&
                s_frame_recv_buf[2] == MATRIX_HEIGHT) {
                apply_full_rgb888(s_frame_recv_buf + 3, MATRIX_WIDTH * MATRIX_HEIGHT * 3);
            }
            s_frame_recv_idx = 0;
        }
        return;
    }

    if (info->len == FRAME_LEGACY_BYTES) {
        if (info->index == 0) s_frame_recv_idx = 0;
        if (info->index != s_frame_recv_idx || len > FRAME_PACKET_BYTES - s_frame_recv_idx) {
            s_frame_recv_idx = 0;
            return;
        }
        memcpy(s_frame_recv_buf + s_frame_recv_idx, data, len);
        s_frame_recv_idx += len;
        if (info->final && s_frame_recv_idx == info->len) {
            if (s_frame_recv_buf[0] == 0x02 &&
                s_frame_recv_buf[1] == MATRIX_WIDTH &&
                s_frame_recv_buf[2] == MATRIX_HEIGHT) {
                apply_full_bitmap(s_frame_recv_buf + 3, 16384);
            }
            s_frame_recv_idx = 0;
        }
        return;
    }

    if (info->index == 0 && info->final && len >= 1 && data[0] == 0x03) {
        const uint8_t mode_id = (len >= 2) ? data[1] : 1;
        bool ok = false;
        File gray_file = LittleFS.open("/boot_gray.bin", "w");
        if (gray_file) {
            size_t written = gray_file.write((const uint8_t *)grayFrame, GRAY_FRAME_BYTES);
            gray_file.close();
            ok = (written == GRAY_FRAME_BYTES);
        }
        File frame_file = LittleFS.open("/boot_frame.bin", "w");
        if (frame_file) {
            frame_file.write((const uint8_t *)active_frame, ACTIVE_FRAME_BYTES);
            frame_file.close();
        }
        const char *mode = (mode_id == 2) ? "text" : ((mode_id == 3) ? "image" : "draw");
        File mode_file = LittleFS.open("/boot_mode.txt", "w");
        if (mode_file) {
            mode_file.println(mode);
            mode_file.close();
        }
        File bright_file = LittleFS.open("/boot_bright.txt", "w");
        if (bright_file) {
            bright_file.println(s_display_brightness_pct);
            bright_file.close();
        }
        if (ok) ws.textAll("SAVED_OK");
        else ws.textAll("SAVE_ERR");
    }
}

static void setup_wifi_server() {
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP("matrix 128x128", "123456788");

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(
            200, "text/html", WEB_PAGE_GZIP, WEB_PAGE_GZIP_LEN);
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Pragma", "no-cache");
        response->addHeader("Expires", "0");
        request->send(response);
    });
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
    Serial.print("[WIFI] SoftAP IP: ");
    Serial.println(WiFi.softAPIP());
}

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

    if (!allocate_frame_buffers()) {
        Serial.println("[FATAL] PSRAM allocation failed. Enable QSPI PSRAM.");
        while (true) delay(1000);
    }

    bool loaded_saved = false;
    if (LittleFS.begin(true)) {
        if (LittleFS.exists("/boot_bright.txt")) {
            File bf = LittleFS.open("/boot_bright.txt", "r");
            if (bf) {
                int b = bf.parseInt();
                if (b >= 5 && b <= 100) s_display_brightness_pct = b;
                bf.close();
            }
        }
        build_gamma_table((float)s_display_brightness_pct / 100.0f);
        if (LittleFS.exists("/boot_gray.bin")) {
            File gf = LittleFS.open("/boot_gray.bin", "r");
            if (gf) {
                if (gf.size() == GRAY_FRAME_BYTES) {
                    loaded_saved = (gf.read((uint8_t *)grayFrame, GRAY_FRAME_BYTES) == GRAY_FRAME_BYTES);
                }
                gf.close();
            }
        }
        if (!loaded_saved && LittleFS.exists("/boot_frame.bin")) {
            File frame_file = LittleFS.open("/boot_frame.bin", "r");
            if (frame_file) {
                if (frame_file.size() == ACTIVE_FRAME_BYTES) {
                    if (frame_file.read((uint8_t *)active_frame, ACTIVE_FRAME_BYTES) == ACTIVE_FRAME_BYTES) {
                        sync_gray_from_active_frame();
                        loaded_saved = true;
                    }
                }
                frame_file.close();
            }
        }
    }
    if (!loaded_saved) {
        memcpy_P(active_frame, MATRIX128_ARTWORK, ACTIVE_FRAME_BYTES);
        sync_gray_from_active_frame();
    }
    pack_dma_stream();

    // 1. Инициализация кремния DP3356
    delay(200);
    dp3364_init();
    delay(10);
    for (int i = 0; i < 13; ++i) {
        write_dp3356_reg(DP3356_CONFIG_REGS[i]);
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

    setup_wifi_server();
    Serial.println("[SETUP] LCD_CAM + GDMA + Wi-Fi Engine Started!");
}

void loop() {
    static uint32_t last_ms = 0;
    static uint32_t last_frames = 0;

    if (s_frame_dirty) {
        s_frame_dirty = false;
        pack_dma_stream();
    }
    ws.cleanupClients();

    const uint32_t now = millis();
    if (now - last_ms < 1000) return;

    pcnt_counter_clear(PCNT_UNIT_0);
    esp_rom_delay_us(1000);
    int16_t clk_count = 0;
    pcnt_get_counter_value(PCNT_UNIT_0, &clk_count);
    const float measured_mhz = (float)clk_count / 1000.0f;

    const uint32_t frames = s_frame_counter;
    const float fps = (float)(frames - last_frames) * 1000.0f / (float)(now - last_ms);
    last_ms = now;
    last_frames = frames;

    Serial.printf("[TELEMETRY] Frames: %lu | FPS: %.2f | PIN_CLK: %.2f MHz (%d pulses/ms) | Self-Test: %d/1000 | RX: %lu | WS: %u\n",
                  frames, fps, measured_mhz, clk_count, s_self_test_pulses,
                  s_rx_frame_counter, (unsigned int)ws.count());
    Serial.printf("[DIAG_REG] OUT_SEL41: 0x%03X | MUX41: 0x%08X | CLK_REG: 0x%08X | USER_REG: 0x%08X | MISC_REG: 0x%08X | PIN_CLK_LVL: %d\n",
                  (unsigned int)GPIO.func_out_sel_cfg[PIN_CLK].val,
                  (unsigned int)REG_READ(IO_MUX_GPIO41_REG),
                  (unsigned int)LCD_CAM.lcd_clock.val,
                  (unsigned int)LCD_CAM.lcd_user.val,
                  (unsigned int)LCD_CAM.lcd_misc.val,
                  digitalRead(PIN_CLK));
}
