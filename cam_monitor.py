"""
Camera monitoring utility for 128x32 LED Matrix via DroidCam stream.
Evaluates brightness, zone colors, exposure, and scanning artifacts.
"""

import sys
import time
import argparse
import json
import urllib.request
import cv2
import numpy as np

DROIDCAM_URL = "http://192.168.31.248:4747"

def send_cam_command(endpoint):
    url = f"{DROIDCAM_URL}{endpoint}"
    try:
        req = urllib.request.Request(url)
        with urllib.request.urlopen(req, timeout=3) as resp:
            return resp.status == 200
    except Exception as e:
        print(f"Cam command {endpoint} failed: {e}", file=sys.stderr)
        return False

def capture_single_raw():
    cap = cv2.VideoCapture(f"{DROIDCAM_URL}/video")
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video stream at {DROIDCAM_URL}/video")
    for _ in range(5):
        cap.grab()
    ret, frame = cap.read()
    cap.release()
    if not ret:
        raise RuntimeError("Failed to capture frame")
    return frame

def classify_hues_in_roi(roi):
    h, w, _ = roi.shape
    total_roi_pixels = h * w
    
    gray = cv2.cvtColor(roi, cv2.COLOR_BGR2GRAY)
    hsv = cv2.cvtColor(roi, cv2.COLOR_BGR2HSV)
    H = hsv[:, :, 0]
    S = hsv[:, :, 1]
    V = hsv[:, :, 2]
    
    # Active lit LEDs must have clear brightness and saturation
    active_mask = (V > 50) & (S > 50) & (gray > 40)
    total_active = int(np.sum(active_mask))
    
    # Minimum threshold: must be at least 400 pixels or > 0.8% of ROI to avoid dark noise/halo bleed
    if total_active < 400 or (total_active / total_roi_pixels) < 0.008:
        return "OFF / DARK"
        
    act_H = H[active_mask]
    
    # Counts
    red_count = int(np.sum((act_H <= 12) | (act_H >= 165)))
    green_count = int(np.sum((act_H >= 35) & (act_H < 80)))
    cyan_count = int(np.sum((act_H >= 80) & (act_H < 105)))      # Turquoise / Cyan
    blue_count = int(np.sum((act_H >= 105) & (act_H < 135)))    # Blue
    purple_count = int(np.sum((act_H >= 135) & (act_H < 165)))  # Magenta / Purple
    
    components = []
    for name, cnt in [("BLUE", blue_count), ("TURQUOISE", cyan_count), ("GREEN", green_count), ("RED", red_count), ("MAGENTA", purple_count)]:
        pct = cnt * 100.0 / total_active
        if pct >= 15.0:
            components.append(f"{name} {pct:.0f}%")
            
    if not components:
        return "DIM NOISE"
    return " + ".join(components)

def get_calibrated_crop(frame):
    # Calibrated from user white corner markings (with 4px margin):
    # TL: [14, 39], TR: [585, 67], BR: [575, 341], BL: [18, 355]
    src_pts = np.float32([
        [14, 39],    # Top-Left
        [585, 67],   # Top-Right
        [575, 341],  # Bottom-Right
        [18, 355]    # Bottom-Left
    ])
    # Target flat matrix aspect ratio (128x64 -> 512x256, true 2:1 aspect ratio)
    dst_pts = np.float32([
        [0, 0],
        [512, 0],
        [512, 256],
        [0, 256]
    ])
    M = cv2.getPerspectiveTransform(src_pts, dst_pts)
    warped = cv2.warpPerspective(frame, M, (512, 256))
    return warped

def analyze_frame(frame, consecutive_diff=0.0):
    panel_crop = get_calibrated_crop(frame)
    cv2.imwrite("panel_warped.jpg", panel_crop)
    pw, ph = 512, 256
    
    # Exact user-calibrated zone dividers:
    # Zone 1 (Blue):  x = 0 .. 173 (cols 0..43)
    # Zone 2 (Red):   x = 173 .. 337 (cols 44..84)
    # Zone 3 (Green): x = 337 .. 512 (cols 85..127)
    z1_end = 173
    z2_end = 337
    
    p_x1, p_y1 = 30, 33
    p_x2, p_y2 = 595, 344
    
    zone_left = panel_crop[:, 0:z1_end]
    zone_center = panel_crop[:, z1_end:z2_end]
    zone_right = panel_crop[:, z2_end:]
    
    left_color = classify_hues_in_roi(zone_left)
    center_color = classify_hues_in_roi(zone_center)
    right_color = classify_hues_in_roi(zone_right)
    
    panel_gray = cv2.cvtColor(panel_crop, cv2.COLOR_BGR2GRAY)
    overall_brightness = float(np.mean(panel_gray))
    
    sat_mask = (panel_crop[:, :, 0] > 250) | (panel_crop[:, :, 1] > 250) | (panel_crop[:, :, 2] > 250)
    sat_pct = float(np.sum(sat_mask) * 100.0 / (panel_crop.shape[0] * panel_crop.shape[1]))
    
    row_profiles = np.mean(panel_gray, axis=1)
    stripe_variance = float(np.std(row_profiles))
    
    result = {
        "overall_brightness": round(overall_brightness, 1),
        "saturation_pct": round(sat_pct, 2),
        "is_overexposed": sat_pct > 3.0,
        "scanning_artifacts": {
            "has_scrolling_lines": stripe_variance > 18.0 or consecutive_diff > 6.0,
            "consecutive_frame_diff": round(consecutive_diff, 2),
        },
        "zones": {
            "left (cols 0-42)": left_color,
            "center (cols 43-85)": center_color,
            "right (cols 86-127)": right_color,
        }
    }
    return result, (p_x1, p_y1, p_x2, p_y2, z1_end, z2_end)

def create_annotated_image(frame, stats, coords, output_path="cam_analysis.jpg"):
    # Keep original full frame without warping
    annotated = frame.copy()
    p_x1, p_y1, p_x2, p_y2, z1_end, z2_end = coords
    
    # Outer box
    cv2.rectangle(annotated, (p_x1, p_y1), (p_x2, p_y2), (0, 255, 0), 2)
    cv2.line(annotated, (p_x1 + z1_end, p_y1), (p_x1 + z1_end, p_y2), (255, 255, 0), 2)
    cv2.line(annotated, (p_x1 + z2_end, p_y1), (p_x1 + z2_end, p_y2), (255, 255, 0), 2)
    
    def draw_badge(img, text, x, y, color=(0, 255, 255)):
        font = cv2.FONT_HERSHEY_SIMPLEX
        scale = 0.42
        thick = 1
        (tw, th), baseline = cv2.getTextSize(text, font, scale, thick)
        cv2.rectangle(img, (x, y - th - 3), (x + tw + 4, y + baseline), (20, 20, 20), -1)
        cv2.rectangle(img, (x, y - th - 3), (x + tw + 4, y + baseline), color, 1)
        cv2.putText(img, text, (x + 2, y - 2), font, scale, (255, 255, 255), thick)

    draw_badge(annotated, f"L: {stats['zones']['left (cols 0-42)']}", p_x1 + 6, p_y1 - 8, (0, 255, 0))
    draw_badge(annotated, f"C: {stats['zones']['center (cols 43-85)']}", p_x1 + z1_end + 6, p_y1 - 8, (0, 255, 255))
    draw_badge(annotated, f"R: {stats['zones']['right (cols 86-127)']}", p_x1 + z2_end + 6, p_y1 - 8, (0, 165, 255))
    
    cv2.imwrite(output_path, annotated)

def main():
    parser = argparse.ArgumentParser(description="DroidCam LED Matrix Monitor")
    parser.add_argument("--save", type=str, default="cam_current.jpg")
    parser.add_argument("--ev-reduce", type=float, default=0.65, help="Exposure reduction factor for contrast")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    
    frame = capture_single_raw()
    if args.ev_reduce < 1.0:
        frame = np.clip(frame.astype(np.float32) * args.ev_reduce, 0, 255).astype(np.uint8)
        
    stats, coords = analyze_frame(frame)
    create_annotated_image(frame, stats, coords, output_path=args.save)
    
    if args.json:
        print(json.dumps(stats, indent=2))
    else:
        print("\n=== CAMERA MONITOR EVALUATION ===")
        print(f"Overall Brightness: {stats['overall_brightness']:.1f} / 255")
        print(f"Saturation Pct:     {stats['saturation_pct']:.2f}% (Overexposed: {stats['is_overexposed']})")
        print("Zone Analysis:")
        for z_name, z_val in stats["zones"].items():
            print(f"  {z_name:20}: {z_val}")
        print(f"Annotated frame saved to: {args.save}")

if __name__ == "__main__":
    main()
