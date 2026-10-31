import numpy as np
from PIL import Image, ImageDraw, ImageFont
import math

WIDTH = 128
HEIGHT = 128

img = Image.new("RGB", (WIDTH, HEIGHT), (0, 0, 0))
draw = ImageDraw.Draw(img)

# 1. Outer Border (Perimeter test)
# Cyan on horizontal edges, Magenta on vertical edges
for x in range(WIDTH):
    img.putpixel((x, 0), (0, 255, 255))
    img.putpixel((x, HEIGHT - 1), (0, 255, 255))
for y in range(HEIGHT):
    img.putpixel((0, y), (255, 0, 255))
    img.putpixel((WIDTH - 1, y), (255, 0, 255))

# Corner markers (Pure White)
for dx, dy in [(0,0), (WIDTH-1, 0), (0, HEIGHT-1), (WIDTH-1, HEIGHT-1)]:
    for i in range(3):
        for j in range(3):
            if 0 <= dx+i < WIDTH and 0 <= dy+j < HEIGHT:
                img.putpixel((dx+i, dy+j), (255, 255, 255))

# 2. Top Half (Panel 1: Sky, Stars, Sun, Mountains)
# Stars in the deep sky
np.random.seed(42)
for _ in range(35):
    sx = np.random.randint(4, WIDTH - 4)
    sy = np.random.randint(14, 52)
    # Check not in sun area
    dist_sun = math.hypot(sx - 64, sy - 44)
    if dist_sun > 26:
        color = (255, 255, 255) if np.random.rand() > 0.4 else (0, 255, 255)
        img.putpixel((sx, sy), color)

# Retro Synthwave Sun (Center at x=64, y=44, radius=24)
cx, cy, r = 64, 46, 23
for y in range(cy - r, cy + r + 1):
    for x in range(cx - r, cx + r + 1):
        if math.hypot(x - cx, y - cy) <= r:
            # Horizontal cut stripes in the lower half of the sun
            if y > cy - 4:
                # Stripe pattern: 3 pixels on, 2 pixels off, wider gaps lower down
                band = (y - (cy - 4))
                if band % 5 in (3, 4):
                    continue
            # Sun color gradient: Yellow at top, Orange/Red at bottom
            if y < cy:
                img.putpixel((x, y), (255, 255, 0)) # Yellow
            elif y < cy + 12:
                img.putpixel((x, y), (255, 120, 0)) # Orange
            else:
                img.putpixel((x, y), (255, 0, 0))   # Red

# Neon Mountains Silhouette at horizon (Y=52..63)
mountain_pts = [
    (0, 63), (12, 56), (25, 60), (40, 52), (54, 61), 
    (64, 58), (74, 61), (88, 52), (103, 60), (116, 56), (127, 63)
]
for i in range(len(mountain_pts) - 1):
    draw.line([mountain_pts[i], mountain_pts[i+1]], fill=(180, 0, 255), width=1)

# Horizon line at Y=63 (Cyan)
for x in range(WIDTH):
    img.putpixel((x, 63), (0, 255, 255))
    img.putpixel((x, 64), (255, 0, 255))

# 3. Bottom Half (Panel 2: 3D Perspective Synthwave Grid Y=65..127)
# Horizontal grid lines with perspective spacing
h_lines = [66, 69, 73, 78, 85, 93, 103, 114, 126]
for hy in h_lines:
    for x in range(2, WIDTH - 2):
        img.putpixel((x, hy), (0, 255, 255))

# Converging perspective vertical grid lines (vanishing point at (64, 63))
vanish_x, vanish_y = 64, 63
bottom_x_targets = [-40, -10, 15, 35, 50, 64, 78, 93, 113, 138, 168]
for bx in bottom_x_targets:
    draw.line([(vanish_x, vanish_y), (bx, 127)], fill=(255, 0, 255), width=1)

# 4. Large Glowing Emblem in the Center of Panel 2 (Y=72..108)
# A radiant glowing neon heart / diamond emblem spanning Y=74..106
emblem_cx, emblem_cy = 64, 90
for ey in range(-16, 17):
    for ex in range(-18, 19):
        px = emblem_cx + ex
        py = emblem_cy + ey
        if 0 <= px < WIDTH and 0 <= py < HEIGHT:
            # Heart shape equation: (x^2 + y^2 - 1)^3 - x^2 * y^3 <= 0
            nx = ex / 13.0
            ny = -ey / 13.0 + 0.2
            val = (nx**2 + ny**2 - 1)**3 - (nx**2) * (ny**3)
            if val <= 0:
                # Glowing center: Yellow center, Red edge, Magenta border
                if val <= -0.4:
                    img.putpixel((px, py), (255, 255, 0)) # Yellow
                elif val <= -0.1:
                    img.putpixel((px, py), (255, 0, 0))   # Red
                else:
                    img.putpixel((px, py), (255, 0, 255)) # Magenta

# 5. Clean Pixel Text
# Built-in 5x7 bitmap font rendering
FONT_5X7 = {
    'E': ["1111", "1000", "1110", "1000", "1111"],
    'S': ["0111", "1000", "0110", "0001", "1110"],
    'P': ["1110", "1001", "1110", "1000", "1000"],
    '3': ["1110", "0001", "0110", "0001", "1110"],
    '2': ["1110", "0001", "0110", "1000", "1111"],
    '-': ["0000", "0000", "1110", "0000", "0000"],
    '1': ["0100", "1100", "0100", "0100", "1110"],
    '8': ["0110", "1001", "0110", "1001", "0110"],
    'X': ["1001", "1001", "0110", "1001", "1001"],
    'M': ["10001", "11011", "10101", "10001", "10001"],
    'A': ["0110", "1001", "1111", "1001", "1001"],
    'T': ["11111", "00100", "00100", "00100", "00100"],
    'R': ["1110", "1001", "1110", "1010", "1001"],
    'I': ["111", "010", "010", "010", "111"],
}

def draw_text(start_x, start_y, text, color):
    cx = start_x
    for ch in text:
        if ch == ' ':
            cx += 3
            continue
        matrix = FONT_5X7.get(ch)
        if matrix:
            mw = len(matrix[0])
            for r, row in enumerate(matrix):
                for c, bit in enumerate(row):
                    if bit == '1':
                        px = cx + c
                        py = start_y + r
                        if 0 <= px < WIDTH and 0 <= py < HEIGHT:
                            img.putpixel((px, py), color)
            cx += mw + 1

# Top Banner: "ESP32-S3" centered at Y = 4
draw_text(42, 4, "ESP32-S3", (0, 255, 255)) # Cyan

# Bottom Banner: "128X128 MATRIX" at Y = 118
draw_text(16, 118, "128X128 MATRIX", (255, 255, 0)) # Yellow

# Save Preview Image
img.save("artwork_128x128_preview.png")
print("Saved preview to 'artwork_128x128_preview.png'")

# 6. Convert to C++ array for ESP32-S3 driver:
# active_frame[32][16][16]
# line = y % 32
# col  = x % 16
# ch   = (y < 64) ? (8 + (x / 16)) : (x / 16)
# is_bottom = ((y % 64) >= 32)
# Top pins: R1=4, G1=5, B1=6
# Bot pins: R2=7, G2=15, B2=16

rgb_data = np.array(img)
R = rgb_data[:, :, 0] > 60
G = rgb_data[:, :, 1] > 60
B = rgb_data[:, :, 2] > 60

active_frame = np.zeros((32, 16, 16), dtype=np.uint32)

for y in range(HEIGHT):
    line = y % 32
    is_bottom = ((y % 64) >= 32)
    for x in range(WIDTH):
        col = x % 16
        ch = (8 + (x // 16)) if (y < 64) else (x // 16)

        m = 0
        if not is_bottom:
            if R[y, x]: m |= (1 << 4)
            if G[y, x]: m |= (1 << 5)
            if B[y, x]: m |= (1 << 6)
        else:
            if R[y, x]: m |= (1 << 7)
            if G[y, x]: m |= (1 << 15)
            if B[y, x]: m |= (1 << 16)

        active_frame[line, col, ch] |= m

# Output header file
with open("matrix128x128_data.h", "w", encoding="utf-8") as f:
    f.write("// ============================================================================\n")
    f.write("// Dual-Panel 128x128 Synthwave Artwork Framebuffer Data\n")
    f.write("// Auto-generated by create_128x128_artwork.py\n")
    f.write("// ============================================================================\n")
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write("const uint32_t MATRIX128_ARTWORK[32][16][16] PROGMEM = {\n")
    for line in range(32):
        f.write(f"  {{ // Scan Line {line}\n")
        for col in range(16):
            f.write("    { " + ", ".join(f"0x{m:08X}UL" for m in active_frame[line, col]) + " },\n")
        f.write("  },\n")
    f.write("};\n")

print("Generated 'matrix128x128_data.h' successfully!")
