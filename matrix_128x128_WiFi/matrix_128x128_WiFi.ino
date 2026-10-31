#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include "web_page_data.h"
#include "matrix128x128_data.h"

// ============================================================================
// DUAL-PANEL 128x128 MATRIX DRIVER WITH WI-FI WEB STUDIO (ESP32-S3)
// SoftAP: "matrix 128x128" | Password: "123456788" | IP: 192.168.4.1
// Hardware: ESP32-S3 Super Mini + 2x Qiangli Q2.5 Pro (128x64, 1/32 scan each)
// ============================================================================

// Physical RGB LED outputs:
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
#define MATRIX_HEIGHT 128
#define ROWS          32
#define CHAINED_CHIPS 16

#define ALL_RGB_MASK ((1UL << PIN_R1) | (1UL << PIN_G1) | (1UL << PIN_B1) | \
                      (1UL << PIN_R2) | (1UL << PIN_G2) | (1UL << PIN_B2))

#define CLK_MASK (1UL << (PIN_CLK - 32))
#define LAT_MASK (1UL << (PIN_LE - 32))

// Clean clock pulse with optimal signal integrity
#define CLK_PULSE() do { \
    GPIO.out1_w1ts.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop;"); \
    GPIO.out1_w1tc.val = CLK_MASK; \
    __asm__ __volatile__("nop; nop;"); \
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

// 13 DP3356 Configuration Registers (PLL Enabled 0x0642 + Boost 0x1180 + MAX Current 0x08FF)
const uint16_t DP3356_CONFIG_REGS[13] = {
    0x021F, // 32 scan lines (31 + 1)
    0x033F, // group num = 63
    0x047F, // max active PWM time
    0x0504,
    0x0642, // Bits [7:6] = 01 (INTERNAL PLL ENABLED!)
    0x0700,
    0x08FF, // MAXIMUM 200% CURRENT GAIN (255 / 255)
    0x0960,
    0x0ABE,
    0x0B8B, // Stable Knee Voltage & Anti-Coupling
    0x0CD8, // High-Current Range Multiplier (0x18 | 0b11000000)
    0x0D12,
    0x1180  // Internal boost / high-efficiency PWM enable (bit 7 = 1)
};

// Proper DP3356 Register Write for 16 daisy-chained chips (2 panels x 8 chips = 16 chips):
void write_dp3356_reg(uint16_t reg_word) {
    set_gpio_for_clk_pulses(PIN_LE, 3);
    for (int i = 0; i < 16; i++) CLK_PULSE();
    set_gpio_for_clk_pulses(PIN_LE, 14);
    for (int i = 0; i < 8; i++) CLK_PULSE();

    // 16 chips x 16 bits = 256 bits total:
    for (int i = 0; i < 256; i++) {
        bool bit = (reg_word >> (15 - (i % 16))) & 1;
        if (bit) GPIO.out_w1ts = ALL_RGB_MASK;
        else     GPIO.out_w1tc = ALL_RGB_MASK;

        // Latch on the final 5 clocks of the chain:
        if (i >= 256 - 5) {
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

// DP32020A advance row:
static inline void dp32020_advance_row(uint32_t line) {
    uint32_t set_mask = 0;
    if (line == 0) set_mask |= (1UL << PIN_C);

    // Pulse DCK (PIN_A=18)
    set_mask |= (1UL << PIN_A);
    GPIO.out_w1ts = set_mask;

    esp_rom_delay_us(1);

    GPIO.out_w1tc = (1UL << PIN_A);
    uint32_t clr_mask = (1UL << PIN_B) | (1UL << PIN_C);
    GPIO.out_w1tc = clr_mask;
}

void led_panel_advance_row(uint32_t line) {
    dp3364_change_row(line);
    dp32020_advance_row(line);
}

// Dual-Panel Active Framebuffer (32 lines x 16 cols x 16 chips = 32KB in fast internal SRAM)
static uint32_t active_frame[32][16][16];

// ============================================================================
// PIXEL & FRAME UPDATE ENGINES (CALIBRATED FOR 128x128 DUAL PANEL)
// ============================================================================

void set_matrix_pixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
    if (x >= 128 || y >= 128) return;
    uint32_t line = y % 32;
    bool is_bottom = ((y % 64) >= 32);
    uint32_t col = x % 16;
    uint32_t ch = (y < 64) ? (8 + (x / 16)) : (x / 16);

    uint32_t clr_mask = 0;
    uint32_t set_mask = 0;

    if (!is_bottom) {
        clr_mask = (1UL << PIN_R1) | (1UL << PIN_G1) | (1UL << PIN_B1);
        if (r > 127) set_mask |= (1UL << PIN_R1);
        if (g > 127) set_mask |= (1UL << PIN_G1);
        if (b > 127) set_mask |= (1UL << PIN_B1);
    } else {
        clr_mask = (1UL << PIN_R2) | (1UL << PIN_G2) | (1UL << PIN_B2);
        if (r > 127) set_mask |= (1UL << PIN_R2);
        if (g > 127) set_mask |= (1UL << PIN_G2);
        if (b > 127) set_mask |= (1UL << PIN_B2);
    }

    active_frame[line][col][ch] = (active_frame[line][col][ch] & ~clr_mask) | set_mask;
}

void apply_full_bitmap(const uint8_t *bitmapData, size_t len) {
    if (len < 16384) return;
    size_t idx = 0;
    for (uint32_t y = 0; y < 128; y++) {
        uint32_t line = y % 32;
        bool is_bottom = ((y % 64) >= 32);

        for (uint32_t x = 0; x < 128; x++) {
            uint32_t col = x % 16;
            uint32_t ch = (y < 64) ? (8 + (x / 16)) : (x / 16);
            uint8_t m = bitmapData[idx++];

            uint32_t clr_mask = 0;
            uint32_t set_mask = 0;

            if (!is_bottom) {
                clr_mask = (1UL << PIN_R1) | (1UL << PIN_G1) | (1UL << PIN_B1);
                if (m & 0x01) set_mask |= (1UL << PIN_R1); // Red
                if (m & 0x02) set_mask |= (1UL << PIN_G1); // Green
                if (m & 0x04) set_mask |= (1UL << PIN_B1); // Blue
            } else {
                clr_mask = (1UL << PIN_R2) | (1UL << PIN_G2) | (1UL << PIN_B2);
                if (m & 0x01) set_mask |= (1UL << PIN_R2); // Red
                if (m & 0x02) set_mask |= (1UL << PIN_G2); // Green
                if (m & 0x04) set_mask |= (1UL << PIN_B2); // Blue
            }

            active_frame[line][col][ch] = (active_frame[line][col][ch] & ~clr_mask) | set_mask;
        }
    }
}

// ============================================================================
// ASYNC WEBSERVER & WEBSOCKET PROTOCOL
// ============================================================================
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

static uint8_t s_frame_recv_buf[16384 + 3];
static size_t s_frame_recv_idx = 0;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        digitalWrite(ONBOARD_LED, LOW); // LED blink on connect
        delayMicroseconds(50);
        digitalWrite(ONBOARD_LED, HIGH);

        // Report saved boot mode to client if available
        if (LittleFS.exists("/boot_mode.txt")) {
            File fm = LittleFS.open("/boot_mode.txt", "r");
            if (fm) {
                String mStr = fm.readStringUntil('\n');
                mStr.trim();
                fm.close();
                if (mStr.length() > 0) {
                    client->text("BOOT_MODE:" + mStr);
                }
            }
        }
    } else if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->opcode == WS_BINARY) {
            // Mode 1: Live single pixel: [0x01, x, y, r, g, b] (6 bytes)
            if (info->len == 6 && len == 6 && data[0] == 0x01) {
                set_matrix_pixel(data[1], data[2], data[3], data[4], data[5]);
            }
            // Mode 2: Full 16KB Frame: [0x02, w, h, 16384 bytes] (16387 bytes)
            else if (info->len == 16387) {
                if (info->index == 0) {
                    s_frame_recv_idx = 0;
                }
                if (s_frame_recv_idx + len <= sizeof(s_frame_recv_buf)) {
                    memcpy(s_frame_recv_buf + s_frame_recv_idx, data, len);
                    s_frame_recv_idx += len;
                }
                if (info->final && (info->index + len == info->len)) {
                    if (s_frame_recv_buf[0] == 0x02) {
                        apply_full_bitmap(s_frame_recv_buf + 3, 16384);
                    }
                    s_frame_recv_idx = 0;
                }
            }
            // Mode 3: Save current active_frame into LittleFS: [0x03, modeId]
            else if (info->len >= 1 && len >= 1 && data[0] == 0x03) {
                uint8_t modeId = (len >= 2) ? data[1] : 1;
                File f = LittleFS.open("/boot_frame.bin", "w");
                if (f) {
                    f.write((const uint8_t*)active_frame, sizeof(active_frame));
                    f.close();

                    const char *mStr = (modeId == 2) ? "text" : ((modeId == 3) ? "image" : "draw");
                    File fm = LittleFS.open("/boot_mode.txt", "w");
                    if (fm) {
                        fm.println(mStr);
                        fm.close();
                    }
                    ws.textAll("SAVED_OK");
                } else {
                    ws.textAll("SAVE_ERR");
                }
            }
        }
    }
}

void setup() {
    setCpuFrequencyMhz(240);
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

    // Mount LittleFS and restore saved boot frame if available:
    bool loaded_saved = false;
    if (LittleFS.begin(true)) {
        if (LittleFS.exists("/boot_frame.bin")) {
            File f = LittleFS.open("/boot_frame.bin", "r");
            if (f && f.size() == sizeof(active_frame)) {
                size_t read_bytes = f.read((uint8_t*)active_frame, sizeof(active_frame));
                if (read_bytes == sizeof(active_frame)) {
                    loaded_saved = true;
                }
                f.close();
            } else if (f) {
                f.close();
            }
        }
    }
    if (!loaded_saved) {
        memcpy_P(active_frame, MATRIX128_ARTWORK, sizeof(active_frame));
    }

    Serial.begin(115200);
    delay(200);
    dp3364_init();

    // Setup Wi-Fi SoftAP (SSID: "matrix 128x128", Pass: "123456788")
    WiFi.mode(WIFI_AP);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.softAP("matrix 128x128", "123456788");

    // Route: Serve SPA Web Studio from Flash (gzip compressed)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", WEB_PAGE_GZIP, WEB_PAGE_GZIP_LEN);
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });

    // WebSocket Handler
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    server.begin();
}

void loop() {
    // 1. One-time sequence: transmit 13 config registers once, then hold VSYNC only
    static uint8_t s_cfg_idx = 0;
    if (s_cfg_idx < 13) {
        write_dp3356_reg(DP3356_CONFIG_REGS[s_cfg_idx]);
        s_cfg_idx++;
    } else {
        dp3364_vsync();
    }

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

        // Step B: Shift 256 pixels of RGB data (16 chips * 16 cols) across both panels:
        const uint32_t *line_ptr = &active_frame[line][0][0];
        for (uint32_t col = 0; col < 16; col++) {
            // Chips 0..14 (Chips 0..7 for Panel 2, Chips 8..14 for Panel 1)
            for (uint32_t ch = 0; ch < 15; ch++) {
                uint32_t mask = *line_ptr++;
                GPIO.out_w1ts = mask;
                GPIO.out_w1tc = ALL_RGB_MASK & ~mask;
                CLK_PULSE_15();
                CLK_PULSE();
            }
            // Chip 15 (last chip in chain): 16th bit latches with LAT_MASK for both panels
            uint32_t mask = *line_ptr++;
            GPIO.out_w1ts = mask;
            GPIO.out_w1tc = ALL_RGB_MASK & ~mask;
            CLK_PULSE_15();
            GPIO.out1_w1ts.val = CLK_MASK | LAT_MASK;
            __asm__ __volatile__("nop; nop;");
            GPIO.out1_w1tc.val = CLK_MASK | LAT_MASK;
            __asm__ __volatile__("nop; nop;");
        }

        // Step C: DP3356 Row Change Handshake on OE
        dp3364_change_row(line);

        // Step D: UNBLANK PMOS (DCK=0): Turn ON current row for PWM emission
        GPIO.out_w1tc = (1UL << PIN_A);
        esp_rom_delay_us(1);

        // Step E: Calibrated PWM burst for EXACT 50.000 FPS (zero beat against 100Hz mains)
        for (uint32_t i = 0; i < 73; i++) {
            CLK_PULSE_8();
        }
        CLK_PULSE_2();
    }

    static int64_t last_time = 0;
    static uint32_t frames = 0;
    int64_t now = esp_timer_get_time();
    frames++;
    if (now - last_time >= 1000000) {
        float fps = (float)frames * 1000000.0f / (float)(now - last_time);
        Serial.printf("[SCAN] FPS: %.2f | Frame: %.1f us | ChipTemp: %.1f C\n", fps, (float)(now - last_time) / (float)frames, temperatureRead());
        frames = 0;
        last_time = now;
    }
}
