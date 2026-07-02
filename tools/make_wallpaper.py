#!/usr/bin/env python3
"""Generate tools/wallpaper.bmp: a 1280x800 24-bit uncompressed BMP
(the only variant the kernel's minimal loader in kernel/src/wallpaper.c
accepts). Scene: a dusk-sky vertical gradient with a sun disc and layered
mountain silhouettes - visually distinct from the built-in gradient
fallback so a screendump unambiguously shows which path rendered.

Run: python3 tools/make_wallpaper.py   (writes tools/wallpaper.bmp)
"""
import math
import os
import struct

W, H = 1280, 800


def sky(y):
    t = y / (H - 1)
    # deep indigo (top) -> warm orange (horizon at ~65% height)
    r = int(30 + t * 190)
    g = int(30 + t * 110)
    b = int(70 + t * 40)
    return min(r, 255), min(g, 255), min(b, 255)


def mountain(x, base, amp, seed):
    return base - amp * (
        0.6 * math.sin(x * 0.004 + seed)
        + 0.3 * math.sin(x * 0.013 + seed * 2)
        + 0.1 * math.sin(x * 0.031 + seed * 3)
    )


LAYERS = [  # (base_y, amplitude, seed, color)
    (560, 90, 1.7, (60, 45, 90)),
    (640, 70, 4.2, (40, 30, 65)),
    (730, 55, 7.9, (25, 18, 42)),
]
SUN_X, SUN_Y, SUN_R = 880, 500, 70

ridges = [[mountain(x, b, a, s) for x in range(W)] for b, a, s, _ in LAYERS]

rows = []
for y in range(H):
    row = bytearray()
    for x in range(W):
        r, g, b = sky(y)
        d = math.hypot(x - SUN_X, y - SUN_Y)
        if d < SUN_R:
            r, g, b = 255, 235, 180
        elif d < SUN_R * 2.2:  # soft glow
            f = (d - SUN_R) / (SUN_R * 1.2)
            r = int(r + (255 - r) * (1 - f) * 0.5)
            g = int(g + (235 - g) * (1 - f) * 0.5)
            b = int(b + (180 - b) * (1 - f) * 0.5)
        for i in range(len(LAYERS)):
            if y > ridges[i][x]:
                r, g, b = LAYERS[i][3]
        row += bytes((b, g, r))  # BMP stores BGR
    row += b"\x00" * ((4 - (W * 3) % 4) % 4)
    rows.append(bytes(row))

pixel_data = b"".join(reversed(rows))  # bottom-up row order
header = struct.pack("<2sIHHI", b"BM", 54 + len(pixel_data), 0, 0, 54)
info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 24, 0, len(pixel_data), 2835, 2835, 0, 0)

out = os.path.join(os.path.dirname(__file__), "wallpaper.bmp")
with open(out, "wb") as f:
    f.write(header + info + pixel_data)
print(f"wrote {out} ({54 + len(pixel_data)} bytes, {W}x{H} 24bpp)")
