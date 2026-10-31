#include <Arduino.h>

#define PIN_R1  5   // Top Red
#define PIN_G1  4   // Top Green
#define PIN_B1  6   // Top Blue

#define PIN_R2  15  // Bottom Red
#define PIN_G2  7   // Bottom Green
#define PIN_B2  16  // Bottom Blue

#define A_PIN   18  // DCK (Row Clock)
#define B_PIN   8   // BK / RCK (Shift Gate)
#define C_PIN   3   // DIN (Row Serial Data)

#define LAT_PIN 40  // LE / LAT
#define OE_PIN  2   // GCLK / OE
#define CLK_PIN 41  // CLK

#define ONBOARD_LED 47

#define WIDTH  128
#define HEIGHT 64
#define ROWS   32

#define ALL_RGB_MASK ((1UL << PIN_R1) | (1UL << PIN_G1) | (1UL << PIN_B1) | \
                      (1UL << PIN_R2) | (1UL << PIN_G2) | (1UL << PIN_B2))

#define CLK_MASK (1UL << (CLK_PIN - 32))
#define LAT_MASK (1UL << (LAT_PIN - 32))

static inline void clk_pulse() {
    GPIO.out1_w1ts.val = CLK_MASK;
    __asm__ __volatile__(
        "nop; nop; nop; nop; nop; nop; nop; nop;"
        "nop; nop; nop; nop; nop; nop; nop; nop;"
    );
    GPIO.out1_w1tc.val = CLK_MASK;
    __asm__ __volatile__(
        "nop; nop; nop; nop; nop; nop; nop; nop;"
        "nop; nop; nop; nop; nop; nop; nop; nop;"
    );
}

static inline void clk_slow() {
    GPIO.out1_w1ts.val = CLK_MASK;
    delayMicroseconds(1);
    GPIO.out1_w1tc.val = CLK_MASK;
    delayMicroseconds(1);
}

void send_clocks(int count) {
    for (int i = 0; i < count; i++) clk_slow();
}

void send_latches(int latches) {
    GPIO.out1_w1ts.val = LAT_MASK;
    for (int i = 0; i < latches; i++) clk_slow();
    GPIO.out1_w1tc.val = LAT_MASK;
}

void send_to_RGB(uint16_t reg_val, int latches) {
    for (int i = 0; i < WIDTH; i++) {
        bool bit = (reg_val >> (15 - (i % 16))) & 1;
        if (bit) {
            GPIO.out_w1ts = ALL_RGB_MASK;
        } else {
            GPIO.out_w1tc = ALL_RGB_MASK;
        }

        if (i >= WIDTH - latches) GPIO.out1_w1ts.val = LAT_MASK;
        clk_slow();
    }
    GPIO.out1_w1tc.val = LAT_MASK;
    GPIO.out_w1tc = ALL_RGB_MASK;
}

// 13 DP3356 Config Registers
const uint16_t DP3356_CONFIG_REGS[13] = {
    0x021F, // Row scan count: 32 scans
    0x033F, // Multiplier & clock mode
    0x0408, // Mild current gain (soft pleasant glow)
    0x0504, // Knee point & low-gray enhancement
    0x0642, // Ghost cancellation / pull-down
    0x0700, // Refresh / clock division
    0x08BF, // Precharge reference
    0x0960, // Internal oscillator
    0x0ABE, // Offset compensation
    0x0B8B, // Blanking margin
    0x0C88, // S-PWM scramble pattern mode
    0x0D12, // Turn-on speed
    0x1100  // Commit
};

void start_GCLK() {
    ledcSetup(0, 10000000, 2);
    ledcAttachPin(OE_PIN, 0);
    ledcWrite(0, 2);
}

void stop_GCLK() {
    ledcDetachPin(OE_PIN);
    pinMode(OE_PIN, OUTPUT);
    digitalWrite(OE_PIN, LOW);
}

void load_dp3356_registers() {
    stop_GCLK();
    for (int r = 0; r < 13; r++) {
        send_latches(3); send_clocks(16);
        send_latches(14); send_clocks(8);
        send_to_RGB(DP3356_CONFIG_REGS[r], 5);
        send_clocks(8);
        delayMicroseconds(50);
    }
    send_latches(3); send_clocks(16);
    start_GCLK();
}

void set_row_dp32020a(int row) {
    digitalWrite(B_PIN, HIGH);
    digitalWrite(C_PIN, (row == 0) ? HIGH : LOW);
    delayMicroseconds(1);

    digitalWrite(A_PIN, HIGH);
    delayMicroseconds(1);
    digitalWrite(A_PIN, LOW);

    digitalWrite(B_PIN, LOW);
    digitalWrite(C_PIN, LOW);
}

void flush_row_register() {
    digitalWrite(B_PIN, HIGH);
    digitalWrite(C_PIN, LOW);
    for (int i = 0; i < 40; i++) {
        digitalWrite(A_PIN, HIGH);
        delayMicroseconds(1);
        digitalWrite(A_PIN, LOW);
        delayMicroseconds(1);
    }
    digitalWrite(B_PIN, LOW);
}

// 128x64 Framebuffer in RAM
uint8_t fb[HEIGHT][WIDTH];

void drawPixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;
    fb[y][x] = color;
}

void fillRect(int x0, int y0, int w, int h, uint8_t color) {
    for (int y = y0; y < y0 + h; y++) {
        for (int x = x0; x < x0 + w; x++) {
            drawPixel(x, y, color);
        }
    }
}

void clearScreen() {
    memset(fb, 0, sizeof(fb));
}

// 100% PURE GREEN TEST:
// If the panel is purely receiving data, ONLY GREEN pins will ever go HIGH.
// Red and Blue pins are physically LOCKED to 0 (GND).
void render_frame_pure_green() {
    stop_GCLK();

    digitalWrite(OE_PIN, HIGH);
    send_clocks(12);
    digitalWrite(OE_PIN, LOW);

    uint16_t clk_count = 16;
    int current_row = 0;

    // Hard-lock Red and Blue to GND:
    uint32_t set_mask = (1UL << PIN_G1) | (1UL << PIN_G2);
    uint32_t clr_mask = ALL_RGB_MASK & ~set_mask;

    for (int y = 0; y < ROWS; y++) {
        for (int x_channel = 0; x_channel < 16; x_channel++) {
            for (int sect = 0; sect < 8; sect++) {
                if (clk_count == 112) {
                    set_row_dp32020a(current_row);
                    current_row++;
                    if (current_row >= ROWS) current_row = 0;
                }

                if (clk_count == 128) {
                    digitalWrite(OE_PIN, HIGH);
                }

                GPIO.out_w1ts = set_mask;
                GPIO.out_w1tc = clr_mask;

                clk_pulse();
                clk_pulse();
                clk_pulse();

                if (sect < 7) {
                    clk_pulse();
                } else {
                    GPIO.out1_w1ts.val = LAT_MASK;
                    clk_pulse();
                    GPIO.out1_w1tc.val = LAT_MASK;
                }

                if (clk_count == 128) {
                    digitalWrite(OE_PIN, LOW);
                    clk_count = 0;
                }

                clk_count += 16;
            }
        }
    }

    start_GCLK();
}

void setup() {
    Serial.begin(115200);
    pinMode(ONBOARD_LED, OUTPUT);
    digitalWrite(ONBOARD_LED, HIGH);

    pinMode(PIN_R1, OUTPUT); pinMode(PIN_R2, OUTPUT);
    pinMode(PIN_G1, OUTPUT); pinMode(PIN_G2, OUTPUT);
    pinMode(PIN_B1, OUTPUT); pinMode(PIN_B2, OUTPUT);

    pinMode(A_PIN, OUTPUT); pinMode(B_PIN, OUTPUT); pinMode(C_PIN, OUTPUT);
    pinMode(LAT_PIN, OUTPUT); pinMode(CLK_PIN, OUTPUT);
    pinMode(OE_PIN, OUTPUT);

    digitalWrite(PIN_R1, LOW); digitalWrite(PIN_R2, LOW);
    digitalWrite(PIN_G1, LOW); digitalWrite(PIN_G2, LOW);
    digitalWrite(PIN_B1, LOW); digitalWrite(PIN_B2, LOW);

    digitalWrite(A_PIN, LOW);
    digitalWrite(B_PIN, LOW);
    digitalWrite(C_PIN, LOW);
    digitalWrite(LAT_PIN, LOW);
    digitalWrite(CLK_PIN, LOW);
    digitalWrite(OE_PIN, HIGH);

    delay(300);
    start_GCLK();
    delay(100);
    load_dp3356_registers();
    flush_row_register();
}

unsigned long lastBlink = 0;
bool led = false;

void loop() {
    render_frame_pure_green();
    delay(2);

    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        led = !led;
        digitalWrite(ONBOARD_LED, led ? HIGH : LOW);
    }
}
