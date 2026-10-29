#!/usr/bin/env python3
"""Render dua video iklan AudioHub, masing-masing 60 detik.

Gambar dibuat dengan Pillow, musik dan efek suara disintesis dengan numpy
(tidak ada aset atau sampel dari luar), lalu digabung dengan ffmpeg.

Pemakaian:
  python3 render_ads.py                 render kedua video
  python3 render_ads.py 1               render video 1 saja
  python3 render_ads.py still 2 12.5    simpan satu frame video 2 pada detik 12.5
"""
import math
import os
import subprocess
import sys
import tempfile
import wave

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, FPS, SR = 1920, 1080, 30, 48000
DUR = 60.0
XF = 0.4  # durasi crossfade antar scene, detik
HERE = os.path.dirname(os.path.abspath(__file__))

# Palet dari apps/macos/AudioHub/Sources/Model.swift dan palet storyboard
BG = (19, 19, 21)
CARD = (28, 28, 31)
MOCK = (28, 28, 30)
MOCK2 = (42, 42, 47)
LINE = (58, 58, 64)
FG = (237, 237, 239)
FG2 = (201, 201, 207)
MUTE = (142, 142, 150)
TEAL = (77, 209, 189)
AMBER = (250, 184, 77)
RED = (242, 89, 89)
INK = (14, 14, 16)

SITE = "audiohub.khincreator.com"
COPY = "Hak Cipta © 2026 taufiq sholikhin. Seluruh hak dilindungi."


# ---------------------------------------------------------------- util

def clamp(x, a=0.0, b=1.0):
    return max(a, min(b, x))


def smooth(x):
    x = clamp(x)
    return x * x * (3 - 2 * x)


def prog(T, a, b):
    """0 sebelum a, 1 sesudah b, dengan easing di antaranya."""
    return smooth((T - a) / (b - a))


def lerp(p, q, t):
    return tuple(int(round(p[i] + (q[i] - p[i]) * t)) for i in range(3))


_fonts = {}


def font(size, weight="Regular", mono=False):
    key = (size, weight, mono)
    if key not in _fonts:
        path = "/System/Library/Fonts/SFNSMono.ttf" if mono else "/System/Library/Fonts/SFNS.ttf"
        f = ImageFont.truetype(path, size)
        try:
            f.set_variation_by_name(weight)
        except Exception:
            pass
        _fonts[key] = f
    return _fonts[key]


def rr(d, box, r, fill=None, outline=None, width=1):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def text(d, xy, s, size=28, fill=FG, weight="Regular", anchor="lm", mono=False):
    d.text(xy, s, font=font(size, weight, mono), fill=fill, anchor=anchor)


def hl(d, box, num=None, a=1.0):
    """Kotak sorot teal, dengan nomor urut opsional."""
    col = lerp(BG, TEAL, a)
    x0, y0, x1, y1 = box
    d.rounded_rectangle((x0 - 7, y0 - 7, x1 + 7, y1 + 7), radius=14, outline=col, width=4)
    if num is not None:
        cx, cy = x0 - 7, y0 - 7
        d.ellipse((cx - 19, cy - 19, cx + 19, cy + 19), fill=col)
        d.text((cx, cy), str(num), font=font(22, "Bold"), fill=INK, anchor="mm")


def caption(d, lines, a, y=950):
    if a <= 0.001:
        return
    f = font(40, "Semibold")
    lh = 58
    pw = max(d.textlength(s, font=f) for s in lines) + 96
    ph = lh * len(lines) + 34
    x0, y0 = 960 - pw / 2, y - ph / 2
    rr(d, (x0, y0, x0 + pw, y0 + ph), 22, fill=lerp(BG, (8, 8, 10), a * 0.85))
    for k, s in enumerate(lines):
        d.text((960, y0 + 17 + lh * k + lh / 2), s, font=f, fill=lerp(BG, FG, a), anchor="mm")


def cap_alpha(T, t_in, t_out):
    return prog(T, t_in, t_in + 0.5) * (1 - prog(T, t_out - 0.5, t_out))


CURSOR = [(0, 0), (0, 22), (6, 17), (10, 26), (14, 24), (10, 16), (18, 16)]


def cursor_pos(T, path):
    if T <= path[0][0]:
        return path[0][1], path[0][2]
    for (t0, x0, y0), (t1, x1, y1) in zip(path, path[1:]):
        if t0 <= T <= t1:
            s = smooth((T - t0) / (t1 - t0))
            return x0 + (x1 - x0) * s, y0 + (y1 - y0) * s
    return path[-1][1], path[-1][2]


def cursor_overlay(d, T, path, clicks):
    for t, x, y in clicks:
        if 0 <= T - t < 0.4:
            r = 10 + 44 * (T - t) / 0.4
            d.ellipse((x - r, y - r, x + r, y + r), outline=lerp(BG, TEAL, 1 - (T - t) / 0.4), width=4)
    if path[0][0] <= T <= path[-1][0]:
        x, y = cursor_pos(T, path)
        d.polygon([(x + px * 1.25, y + py * 1.25) for px, py in CURSOR],
                  fill=(255, 255, 255), outline=(10, 10, 12))


def logo_bars(d, cx, cy, T, t0, scale=1.0):
    heights = [0.35, 0.55, 0.8, 1.0, 0.8, 0.55, 0.35]
    for i, hf in enumerate(heights):
        p = prog(T, t0 + i * 0.06, t0 + 0.5 + i * 0.06)
        h = 170 * hf * scale * p
        if T > t0 + 1.2:
            h *= 0.9 + 0.1 * math.sin(T * 3 + i)
        if h < 2:
            continue
        x = cx + (i - 3) * 32 * scale
        rr(d, (x - 9 * scale, cy - h / 2, x + 9 * scale, cy + h / 2), 9 * scale, fill=TEAL)


def end_card(d, T, t0, lines, cta_alpha_t, kicker=None):
    """Kartu penutup: logo, tagline, CTA situs, dan hak cipta."""
    logo_bars(d, 960, 380, T, t0)
    text(d, (960, 580), "AudioHub", 118, FG, "Bold", anchor="mm")
    a = prog(T, t0 + 0.9, t0 + 1.7)
    if kicker:
        text(d, (960, 660), kicker, 44, lerp(BG, FG, a), "Semibold", anchor="mm")
    else:
        text(d, (960, 660), lines[0], 40, lerp(BG, FG2, a), "Regular", anchor="mm")
    b = prog(T, cta_alpha_t, cta_alpha_t + 0.8)
    if b > 0.01:
        f = font(40, "Bold")
        w = d.textlength(SITE, font=f) + 80
        rr(d, (960 - w / 2, 740, 960 + w / 2, 808), 34, fill=lerp(BG, TEAL, b))
        d.text((960, 774), SITE, font=f, fill=lerp(BG, INK, b), anchor="mm")
    c = prog(T, cta_alpha_t + 1.2, cta_alpha_t + 2.0)
    text(d, (960, 880), "Versi 0.3.0 · macOS 13 atau lebih baru", 26, lerp(BG, MUTE, c), anchor="mm")
    text(d, (960, 922), COPY, 22, lerp(BG, MUTE, c), anchor="mm")


# ---------------------------------------------------------------- jendela aplikasi

WIN = (90, 160, 1830, 870)
TOP_H = 72
OUT_H = 150


def win_parts(r):
    x0, y0, x1, y1 = r
    bt, bb = y0 + TOP_H, y1 - OUT_H
    iw, sw = int((x1 - x0) * 0.27), int((x1 - x0) * 0.24)
    return {
        "top": (x0, y0, x1, bt),
        "inp": (x0, bt, x0 + iw, bb),
        "mid": (x0 + iw, bt, x1 - sw, bb),
        "side": (x1 - sw, bt, x1, bb),
        "out": (x0, bb, x1, y1),
    }


def top_items(r):
    x0, y0, x1, _ = r
    cy = y0 + TOP_H // 2
    return {
        "dev": (x0 + 230, cy - 20, x0 + 500, cy + 20),
        "vir": (x0 + 520, cy - 20, x0 + 710, cy + 20),
        "buka": (x0 + 728, cy - 20, x0 + 828, cy + 20),
        "mulai": (x0 + 846, cy - 20, x0 + 976, cy + 20),
        "pill": (x0 + 1000, cy - 19, x0 + 1140, cy + 19),
        "gear": (x1 - 82, cy - 20, x1 - 42, cy + 20),
        "cy": cy,
    }


def db_str(v):
    """Angka dB dengan tanda minus Unicode, tanpa '-0.0'."""
    s = f"{v:+.1f}"
    if s in ("+0.0", "-0.0"):
        s = "0.0"
    return s.replace("-", "−")


def tri(d, x, y, col=FG2):
    d.polygon([(x - 7, y - 4), (x + 7, y - 4), (x, y + 4)], fill=col)


def draw_window(d, r, T, st):
    """Jendela AudioHub. st: run (0..1), hl (dict nama -> nomor), menu (0..1), hover (indeks)."""
    x0, y0, x1, y1 = r
    rr(d, r, 18, fill=MOCK, outline=LINE, width=2)
    it = top_items(r)
    cy = it["cy"]
    run = st.get("run", 0.0)

    # bilah atas
    rr(d, (x0 + 24, cy - 14, x0 + 52, cy + 14), 7, fill=TEAL)
    text(d, (x0 + 64, cy), "AudioHub", 26, FG, "Semibold")
    for key, label in (("dev", "Maono ProStudio 2x2"), ("vir", "Virtual 1 + 2")):
        bx = it[key]
        rr(d, bx, 10, fill=MOCK2, outline=LINE)
        text(d, (bx[0] + 16, cy), label, 23, FG2)
        tri(d, bx[2] - 22, cy)
    bx = it["buka"]
    rr(d, bx, 10, fill=MOCK2, outline=LINE)
    text(d, ((bx[0] + bx[2]) / 2, cy), "Buka", 23, FG2, anchor="mm")
    bx = it["mulai"]
    rr(d, bx, 10, fill=lerp(TEAL, RED, run))
    text(d, ((bx[0] + bx[2]) / 2, cy), "Hentikan" if run > 0.5 else "Mulai", 23, INK, "Semibold", anchor="mm")
    bx = it["pill"]
    rr(d, bx, 19, fill=MOCK2)
    d.ellipse((bx[0] + 18, cy - 6, bx[0] + 30, cy + 6), fill=lerp(MUTE, TEAL, run))
    text(d, (bx[0] + 40, cy), "Berjalan" if run > 0.5 else "Berhenti", 22, lerp(MUTE, FG, run))
    gx, gy = (it["gear"][0] + it["gear"][2]) / 2, cy
    d.ellipse((gx - 18, gy - 18, gx + 18, gy + 18), fill=MOCK2)
    d.ellipse((gx - 6, gy - 6, gx + 6, gy + 6), fill=MUTE)

    parts = win_parts(r)

    # kolom input
    ix0, iy0, ix1, iy1 = parts["inp"]
    text(d, (ix0 + 24, iy0 + 28), "INPUT", 19, MUTE, "Semibold")
    names = ["Fisik In 1", "Fisik In 2", "Virtual 1 · 1", "Virtual 1 · 2", "Virtual 2 · 1"]
    for i, nm in enumerate(names):
        y = iy0 + 56 + i * 66
        if y + 56 > iy1:
            break
        rr(d, (ix0 + 14, y, ix1 - 14, y + 56), 10, fill=MOCK2)
        text(d, (ix0 + 28, y + 18), nm, 21, FG2, "Medium")
        lvl = run * clamp(0.5 + 0.32 * math.sin(T * 5.3 + i * 1.7) * math.sin(T * 2.1 + i))
        bx0, bx1, by = ix0 + 28, ix1 - 90, y + 40
        rr(d, (bx0, by - 3, bx1, by + 3), 3, fill=LINE)
        if lvl > 0.005:
            wpx = (bx1 - bx0) * lvl
            end = min(wpx, (bx1 - bx0) * 0.72)
            rr(d, (bx0, by - 3, bx0 + max(end, 6), by + 3), 3, fill=TEAL)
            if wpx > (bx1 - bx0) * 0.72:
                rr(d, (bx0 + (bx1 - bx0) * 0.72, by - 3, bx0 + min(wpx, (bx1 - bx0) * 0.9), by + 3), 3, fill=AMBER)
        for k, (lab, col) in enumerate((("S", AMBER), ("M", RED))):
            cx = ix1 - 48 - k * 36 - 12
            rr(d, (cx - 14, y + 12, cx + 14, y + 40), 6, fill=MOCK)
            text(d, (cx, y + 26), lab, 18, MUTE, "Bold", anchor="mm")

    # kolom routing (matriks)
    mx0, my0, mx1, my1 = parts["mid"]
    text(d, (mx0 + 26, my0 + 28), "ROUTING · MATRIKS", 19, MUTE, "Semibold")
    lit = {(0, 0), (0, 2), (1, 1), (2, 3), (3, 0)}
    for j in range(4):
        text(d, (mx0 + 170 + j * 96 + 40, my0 + 66), f"Out {j + 1}", 19, MUTE, anchor="mm")
    for i in range(5):
        text(d, (mx0 + 26, my0 + 110 + i * 62 + 22), f"In {i + 1}", 20, MUTE)
        for j in range(4):
            cx0 = mx0 + 170 + j * 96
            cy0 = my0 + 92 + i * 62
            on = (i, j) in lit
            rr(d, (cx0, cy0, cx0 + 80, cy0 + 44), 8, fill=TEAL if on else MOCK2,
               outline=None if on else LINE)

    # kolom samping
    sx0, sy0, sx1, sy1 = parts["side"]
    rr(d, (sx0 + 14, sy0 + 14, sx1 - 14, sy0 + 56), 10, fill=MOCK2)
    seg_w = (sx1 - sx0 - 28) / 3
    for k, lab in enumerate(("Preset", "Perangkat", "Diagnostik")):
        if k == 0:
            rr(d, (sx0 + 18 + k * seg_w, sy0 + 18, sx0 + 10 + (k + 1) * seg_w, sy0 + 52), 8, fill=LINE)
        text(d, (sx0 + 14 + (k + 0.5) * seg_w, sy0 + 35), lab, 18, FG if k == 0 else MUTE, anchor="mm")
    text(d, (sx0 + 26, sy0 + 92), "PRESET TERSIMPAN", 18, MUTE, "Semibold")
    for k, nm in enumerate(("Podcast Malam", "Live Stream", "Rapat Online")):
        y = sy0 + 116 + k * 58
        rr(d, (sx0 + 14, y, sx1 - 14, y + 46), 10, fill=MOCK2, outline=TEAL if k == 0 else None, width=2)
        text(d, (sx0 + 30, y + 23), nm, 21, FG2)

    # bar output
    ox0, oy0, ox1, oy1 = parts["out"]
    for j, lvl_f in enumerate((0.833, 0.6)):
        sx = ox0 + 60 + j * 300
        text(d, (sx, oy0 + 24), f"Out {j + 1}", 20, FG2, "Semibold")
        text(d, (sx + 150, oy0 + 24), db_str(lvl_f * 72 - 60) + " dB", 20, MUTE, mono=True)
        ftop, fbot = oy0 + 56, oy1 - 16
        rr(d, (sx + 30, ftop, sx + 36, fbot), 3, fill=MOCK2)
        fy = fbot - (fbot - ftop) * lvl_f
        rr(d, (sx + 30, fy, sx + 36, fbot), 3, fill=TEAL)
        rr(d, (sx + 18, fy - 6, sx + 48, fy + 6), 4, fill=FG2)
        mt = ftop
        rr(d, (sx + 90, mt, sx + 100, fbot), 4, fill=MOCK2)
        rr(d, (sx + 90, fbot - (fbot - mt) * 0.55, sx + 100, fbot), 4, fill=TEAL)

    # tombol gear / menu
    if st.get("menu", 0) > 0.01:
        m = st["menu"]
        mx, my = x1 - 320, y0 + 92
        rr(d, (mx, my, mx + 300, my + 214), 14, fill=lerp(BG, (46, 46, 52), m), outline=LINE, width=2)
        rows = [("Sambung ulang otomatis", "✓"), ("Sample rate", "48,0 kHz"), ("Buffer", "512 frame"), ("Tampilan", "Sistem")]
        for k, (lab, val) in enumerate(rows):
            ry = my + 22 + k * 48
            if k == st.get("hover", -1):
                rr(d, (mx + 8, ry - 4, mx + 292, ry + 36), 9, fill=TEAL)
                ink = INK
            else:
                ink = lerp(BG, FG2, m)
            text(d, (mx + 20, ry + 16), lab, 20, ink)
            text(d, (mx + 282, ry + 16), val, 19, ink if k == st.get("hover", -1) else MUTE, anchor="rm")

    # sorotan
    h = st.get("hl", {})
    boxes = {"dev": it["dev"], "vir": it["vir"], "buka": it["buka"], "mulai": it["mulai"],
             "gear": (gx - 22, gy - 22, gx + 22, gy + 22)}
    for name, num in h.items():
        if name in boxes:
            hl(d, boxes[name], num)
        elif name in parts:
            x_a, y_a, x_b, y_b = parts[name]
            hl(d, (x_a + 8, y_a + 8, x_b - 8, y_b - 8), num)


# ---------------------------------------------------------------- scene video 1

def v1_s1(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    c = 1 - prog(T, 4.6, 7.0)
    cy = 460
    specs = [
        (TEAL, 150, 0.012, 3.1, 0.0, "Mikrofon", 120),
        (AMBER, 120, 0.017, -2.4, 1.3, "Musik", 300),
        (RED, 170, 0.009, 2.2, 2.6, "Aplikasi panggilan", 480),
    ]
    for col, amp, freq, om, ph, label, ly in specs:
        pts = []
        for x in range(0, W + 1, 8):
            y = cy + c * amp * math.sin(freq * x + om * T + ph) * (0.6 + 0.4 * math.sin(T * 0.7 + x * 0.002))
            pts.append((x, y))
        d.line(pts, fill=col, width=5, joint="curve")
        a = 1 - prog(T, 4.2, 5.8)
        if a > 0.01:
            text(d, (120, ly), label, 26, lerp(BG, MUTE, a), "Semibold")
    cap = cap_alpha(T, 1.0, 7.6)
    caption(d, ["Mikrofon, musik, dan aplikasi panggilan.", "Semuanya ingin masuk ke satu tempat."], cap)
    return img


def v1_s2(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    logo_bars(d, 960, 400, T, 8.2, scale=1.25)
    a = prog(T, 9.4, 10.4)
    text(d, (960, 600), "AudioHub", 128, lerp(BG, FG, a), "Bold", anchor="mm")
    text(d, (960, 690), "Routing audio profesional untuk macOS.", 40, lerp(BG, FG2, prog(T, 10.2, 11.0)), anchor="mm")
    text(d, (960, 745), "macOS 13 atau lebih baru · Apple Silicon dan Intel", 26, lerp(BG, MUTE, prog(T, 10.6, 11.4)), anchor="mm")
    return img


def v1_s3(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    # jendela DMG
    a = prog(T, 18.2, 18.8)
    rr(d, (260, 300, 860, 640), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    rr(d, (500, 380, 620, 500), 26, fill=lerp(BG, MOCK2, a))
    text(d, (560, 440), "PKG", 34, lerp(BG, TEAL, a), "Bold", anchor="mm", mono=True)
    text(d, (560, 548), "Install AudioHub.pkg", 28, lerp(BG, FG2, a), "Medium", anchor="mm")
    if T > 20.2 and T < 21.6:
        hl(d, (500, 380, 620, 500), None, a=1 - prog(T, 21.0, 21.6))
    # dialog kata sandi
    b = prog(T, 21.2, 21.8)
    if b > 0.01:
        rr(d, (1000, 300, 1660, 640), 18, fill=lerp(BG, MOCK, b), outline=lerp(BG, LINE, b), width=2)
        text(d, (1040, 350), "Masukkan kata sandi", 32, lerp(BG, FG, b), "Semibold")
        rr(d, (1040, 400, 1620, 462), 10, fill=lerp(BG, MOCK2, b), outline=lerp(BG, LINE, b), width=2)
        n = int(clamp((T - 22.0) / 0.35, 0, 8))
        for k in range(n):
            cx = 1070 + k * 28
            d.ellipse((cx - 7, 421, cx + 7, 435), fill=lerp(BG, FG2, b))
        p = prog(T, 25.3, 27.3)
        rr(d, (1040, 500, 1620, 512), 6, fill=lerp(BG, MOCK2, b))
        if p > 0.001:
            rr(d, (1040, 500, 1040 + 580 * p, 512), 6, fill=lerp(BG, TEAL, b))
        btn_txt = "Memasang…" if T >= 25.1 else "Instal"
        rr(d, (1440, 540, 1620, 596), 10, fill=lerp(BG, TEAL, b))
        text(d, (1530, 568), btn_txt, 26, lerp(BG, INK, b), "Semibold", anchor="mm")
    # baris driver
    c = prog(T, 27.4, 28.0)
    if c > 0.01:
        y = 800 + (1 - c) * 24
        rr(d, (260, y - 40, 1660, y + 40), 16, fill=lerp(BG, MOCK, c), outline=lerp(BG, LINE, c), width=2)
        d.ellipse((294, y - 9, 312, y + 9), fill=lerp(BG, TEAL, c))
        text(d, (340, y - 10), "AVHub Driver terpasang", 30, lerp(BG, FG, c), "Semibold")
        text(d, (340, y + 18), "avhub_virtual1 · avhub_virtual2", 22, lerp(BG, MUTE, c), mono=True)
    # kursor
    cur_path = [(18.0, 1500, 820), (19.0, 560, 420), (20.2, 560, 420), (24.8, 1500, 560),
                (25.0, 1530, 568), (25.1, 1530, 568), (27.0, 1530, 568)]
    cursor_overlay(d, T, cur_path, [(20.2, 560, 420), (25.1, 1530, 568)])
    caption(d, ["Pasang sekali. Driver AVHub ikut terpasang."], cap_alpha(T, 18.4, 31.6))
    return img


def v1_s4(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    hs = {}
    if 32.6 <= T < 35.5:
        hs["dev"] = 1
    elif 35.5 <= T < 38.5:
        hs["vir"] = 2
    elif 38.5 <= T < 41.0:
        hs["buka"] = 3
    elif 41.2 <= T < 43.0:
        hs["mulai"] = 4
    elif T >= 43.5:
        hs["gear"] = 5
    st = {
        "run": prog(T, 41.9, 42.4),
        "hl": hs,
        "menu": prog(T, 44.0, 44.5) - prog(T, 48.5, 49.2),
        "hover": 2 if T >= 46.0 else -1,
    }
    draw_window(d, WIN, T, st)
    path = [(32.0, 1100, 620), (33.9, 455, 196), (35.6, 705, 196), (36.9, 705, 196),
            (38.2, 868, 196), (39.6, 868, 196), (41.2, 1001, 196), (41.8, 1001, 196),
            (43.4, 1768, 196), (43.9, 1768, 196), (45.6, 1600, 300), (49.6, 1600, 300)]
    clicks = [(33.9, 455, 196), (36.9, 705, 196), (39.6, 868, 196), (41.8, 1001, 196), (43.9, 1768, 196)]
    cursor_overlay(d, T, path, clicks)
    caption(d, ["Pilih perangkat. Buka. Mulai."], cap_alpha(T, 32.4, 49.6))
    return img


def v1_s5(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    end_card(d, T, 50.2, ["Routing audio profesional untuk macOS."], 53.0)
    return img


# ---------------------------------------------------------------- scene video 2

def v2_s1(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    hs = {}
    if T >= 0.6:
        hs["inp"] = 1
    if T >= 1.8:
        hs["mid"] = 2
    if T >= 3.2:
        hs["side"] = 3
    if T >= 4.6:
        hs["out"] = 4
    if T >= 5.9:
        hs = {}
    draw_window(d, WIN, T, {"run": 1.0, "hl": hs})
    caption(d, ["Satu jendela untuk semua kanal."], cap_alpha(T, 0.4, 6.0))
    return img


def v2_s2(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    rr(d, (240, 230, 1240, 760), 18, fill=MOCK, outline=LINE, width=2)
    text(d, (280, 290), "Fisik In 1", 40, FG, "Semibold")
    # chip M dan S
    m_on = T >= 14.2
    s_on = T >= 15.2
    rr(d, (1080, 270, 1124, 314), 10, fill=RED if m_on else MOCK2)
    text(d, (1102, 292), "M", 24, INK if m_on else MUTE, "Bold", anchor="mm")
    rr(d, (1136, 270, 1180, 314), 10, fill=AMBER if s_on else MOCK2)
    text(d, (1158, 292), "S", 24, INK if s_on else MUTE, "Bold", anchor="mm")

    # gain
    g = -60 + 72 * prog(T, 7.0, 9.4)
    g = g + (-6 - 12) * prog(T, 9.6, 10.6)
    fg_ = (g + 60) / 72
    text(d, (280, 430), "Gain", 28, MUTE, "Medium")
    rr(d, (420, 425, 1080, 435), 5, fill=MOCK2)
    rr(d, (420, 425, 420 + 660 * fg_, 435), 5, fill=TEAL)
    kx = 420 + 660 * fg_
    d.ellipse((kx - 16, 414, kx + 16, 446), fill=(242, 242, 244))
    text(d, (1110, 430), db_str(g) + " dB", 26, FG2, mono=True)

    # pan
    if T < 10.8:
        p = 0.0
    elif T < 11.6:
        p = -prog(T, 10.8, 11.6)
    elif T < 13.0:
        p = -1 + 2 * prog(T, 11.6, 13.0)
    else:
        p = 1 - prog(T, 13.0, 13.8)
    text(d, (280, 540), "Pan", 28, MUTE, "Medium")
    rr(d, (420, 535, 1080, 545), 5, fill=MOCK2)
    px = 420 + 660 * (p + 1) / 2
    rr(d, (min(750, px), 535, max(750, px), 545), 5, fill=TEAL)
    d.ellipse((px - 16, 524, px + 16, 556), fill=(242, 242, 244))
    lab = "C" if abs(p) < 0.02 else (f"L{abs(p) * 100:.0f}" if p < 0 else f"R{p * 100:.0f}")
    text(d, (1110, 540), lab, 26, FG2, mono=True)

    # output fader
    rr(d, (1320, 230, 1640, 760), 18, fill=MOCK, outline=LINE, width=2)
    text(d, (1480, 270), "Out 1", 26, FG2, "Semibold", anchor="mm")
    f = 0.3 + 0.65 * prog(T, 14.0, 15.6)
    f = f + (0.833 - f) * prog(T, 16.6, 17.2)
    rr(d, (1476, 330, 1484, 640), 4, fill=MOCK2)
    fy = 640 - 310 * f
    rr(d, (1476, fy, 1484, 640), 4, fill=TEAL)
    rr(d, (1450, fy - 9, 1510, fy + 9), 5, fill=(242, 242, 244))
    text(d, (1480, 676), f"{f * 72 - 60:+.1f} dB".replace("-", "−"), 24, MUTE, anchor="mm", mono=True)
    rr(d, (1380, 694, 1580, 740), 10, fill=MOCK2)
    text(d, (1480, 717), "0 dB", 24, FG2, "Semibold", anchor="mm")

    # kursor
    path = [(6.8, 420, 430), (7.0, 420, 430), (9.4, 1080, 430), (10.6, 915, 430),
            (10.8, 750, 540), (11.6, 420, 540), (13.0, 1080, 540), (13.8, 750, 540),
            (14.2, 450, 665), (15.2, 530, 665), (15.6, 1480, 345), (16.2, 1480, 382),
            (16.6, 1480, 713), (17.4, 1480, 713)]
    clicks = [(14.2, 450, 665), (15.2, 530, 665), (16.6, 1480, 713)]
    cursor_overlay(d, T, path, clicks)

    # sorotan 1 saat gain bergerak
    caption(d, ["Gain. Pan. Mute. Solo."], cap_alpha(T, 6.4, 17.6))
    return img


def v2_s3(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    rr(d, (300, 230, 1620, 760), 18, fill=MOCK, outline=LINE, width=2)
    text(d, (340, 270), "Routing", 30, FG, "Semibold")
    graph = prog(T, 26.0, 26.4) > 0.5
    rr(d, (1420, 246, 1600, 296), 12, fill=MOCK2)
    rr(d, (1420 + (90 if graph else 4), 250, 1420 + (176 if graph else 86), 292), 9, fill=LINE)
    text(d, (1465, 271), "Matriks", 22, FG if not graph else MUTE, anchor="mm")
    text(d, (1555, 271), "Graph", 22, FG if graph else MUTE, anchor="mm")

    lit_t = {(0, 0): 19.0, (0, 2): 19.7, (1, 1): 20.4, (2, 3): 21.1, (3, 0): 21.8}
    notes = {(0, 0): 523.25, (0, 2): 659.25, (1, 1): 783.99, (2, 3): 880.0, (3, 0): 1046.5}
    if not graph:
        for j in range(4):
            text(d, (420 + j * 170 + 75, 362), f"Out {j + 1}", 22, MUTE, anchor="mm")
        for i in range(4):
            text(d, (350, 380 + i * 96 + 37), f"In {i + 1}", 22, MUTE, anchor="lm")
            for j in range(4):
                x0 = 420 + j * 170
                y0 = 380 + i * 96
                on = (i, j) in lit_t and T >= lit_t[(i, j)]
                fill = TEAL if on else MOCK2
                rr(d, (x0, y0, x0 + 150, y0 + 74), 12, fill=fill, outline=None if on else LINE, width=2)
                if (i, j) == (1, 1) and T >= 24.0:
                    text(d, (x0 + 75, y0 + 37), "−6 dB", 24, INK, "Semibold", anchor="mm")
        # menu klik kanan
        a = prog(T, 22.5, 22.9) * (1 - prog(T, 24.0, 24.3))
        if a > 0.01:
            mx, my = 700, 440
            rr(d, (mx, my, mx + 290, my + 232), 14, fill=lerp(MOCK, (46, 46, 52), a), outline=LINE, width=2)
            items = [("Send 0 dB", 0), ("Send −6 dB", 1), ("Send −12 dB", 2), ("Matikan koneksi", 3)]
            for k, (lab, idx) in enumerate(items):
                y = my + 26 + k * 46 + (12 if k == 3 else 0)
                if k == 3:
                    d.line((mx + 14, y - 22, mx + 276, y - 22), fill=LINE, width=2)
                if idx == 1 and T >= 23.4:
                    rr(d, (mx + 8, y - 20, mx + 282, y + 20), 9, fill=TEAL)
                    col = INK
                else:
                    col = lerp(MOCK, FG2, a)
                text(d, (mx + 22, y), lab, 22, col)
    else:
        # tampilan graph: hanya menampilkan koneksi
        for i in range(4):
            text(d, (380, 380 + i * 96 + 37), f"In {i + 1}", 22, MUTE)
        for j in range(4):
            text(d, (1440, 380 + j * 96 + 37), f"Out {j + 1}", 22, MUTE)
        routes = [(0, 0), (0, 2), (1, 1), (2, 3), (3, 0)]
        for k, (i, j) in enumerate(routes):
            p = prog(T, 26.4 + k * 0.25, 26.4 + k * 0.25 + 0.9)
            if p < 0.005:
                continue
            p0 = (430, 380 + i * 96 + 37)
            p3 = (1420, 380 + j * 96 + 37)
            mid = (p0[0] + p3[0]) / 2
            pts = []
            for s in range(0, 61):
                u = s / 60 * p
                x = (1 - u) ** 3 * p0[0] + 3 * (1 - u) ** 2 * u * mid + 3 * (1 - u) * u ** 2 * mid + u ** 3 * p3[0]
                y = (1 - u) ** 3 * p0[1] + 3 * (1 - u) ** 2 * u * p0[1] + 3 * (1 - u) * u ** 2 * p3[1] + u ** 3 * p3[1]
                pts.append((x, y))
            d.line(pts, fill=TEAL, width=5, joint="curve")
    # kursor
    path = [(20.8, 1000, 520), (22.4, 665, 513), (23.4, 845, 512), (24.0, 845, 512),
            (25.6, 845, 512), (25.8, 1555, 271), (26.0, 1555, 271), (27.0, 1555, 271)]
    clicks = [(22.4, 665, 513), (24.0, 845, 512), (26.0, 1555, 271)]
    cursor_overlay(d, T, path, clicks)
    caption(d, ["Klik untuk menghubungkan.", "Klik kanan untuk mengatur send."], cap_alpha(T, 18.4, 29.6))
    return img


def v2_s4(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    a = prog(T, 30.2, 30.6)
    rr(d, (200, 220, 900, 780), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    text(d, (240, 262), "Fisik In 1", 30, lerp(BG, FG, a), "Semibold")
    rr(d, (240, 300, 860, 350), 12, fill=lerp(BG, MOCK2, a))
    for k, lab in enumerate(("Gate", "EQ", "Kompresor", "Delay")):
        x0 = 240 + k * 155
        if lab == "EQ":
            rr(d, (x0 + 4, 304, x0 + 151, 346), 9, fill=lerp(BG, LINE, a))
        text(d, (x0 + 77, 325), lab, 22, lerp(BG, FG if lab == "EQ" else MUTE, a), anchor="mm")
    eq_on = prog(T, 31.6, 31.9)
    text(d, (240, 410), "Aktifkan EQ", 26, lerp(BG, FG2, a), "Medium")
    tx = 760 + 40 * eq_on
    rr(d, (760, 396, 820, 424), 14, fill=lerp(MOCK2, TEAL, eq_on))
    d.ellipse((tx + 2, 398, tx + 26, 422), fill=(242, 242, 244))
    eq_vals = [
        ("Low shelf", 3.0 * prog(T, 32.2, 33.4), 0.0),
        ("Mid peak", 2.0 * prog(T, 33.4, 34.6), 0.0),
        ("High shelf", -2.0 * prog(T, 34.6, 35.8), 0.0),
    ]
    for k, (lab, v, _) in enumerate(eq_vals):
        y = 500 + k * 80
        text(d, (240, y), lab, 24, lerp(BG, FG2, a), "Medium")
        frac = (v + 15) / 30
        rr(d, (420, y - 5, 720, y + 5), 5, fill=lerp(BG, MOCK2, a))
        xk = 420 + 300 * frac
        rr(d, (min(570, xk), y - 5, max(570, xk), y + 5), 5, fill=lerp(BG, TEAL, a))
        d.ellipse((xk - 13, y - 13, xk + 13, y + 13), fill=lerp(BG, (242, 242, 244), a))
        text(d, (742, y), db_str(v) + " dB", 22, lerp(BG, MUTE, a), mono=True, anchor="lm")
    # popover limiter
    b = prog(T, 36.0, 36.4)
    if b > 0.01:
        rr(d, (1040, 300, 1620, 640), 18, fill=lerp(BG, MOCK, b), outline=lerp(BG, LINE, b), width=2)
        text(d, (1080, 340), "Limiter · Out 1", 30, lerp(BG, FG, b), "Semibold")
        lim_on = prog(T, 36.6, 36.9)
        rr(d, (1500, 326, 1560, 354), 14, fill=lerp(BG, MOCK2, b) if lim_on < 0.5 else lerp(BG, TEAL, b))
        text(d, (1080, 370), "Aktifkan limiter", 24, lerp(BG, FG2, b), "Medium")
        rows = [
            ("Ceiling", -6 + 5 * prog(T, 36.8, 38.0), -12, 0, "dB"),
            ("Lookahead", 6.0 * prog(T, 38.0, 39.2), 0, 20, "ms"),
        ]
        for k, (lab, v, lo, hi, unit) in enumerate(rows):
            y = 420 + k * 70
            text(d, (1080, y + 40), lab, 22, lerp(BG, FG2, b), "Medium")
            frac = (v - lo) / (hi - lo)
            rr(d, (1240, y + 35, 1500, y + 45), 5, fill=lerp(BG, MOCK2, b))
            xk = 1240 + 260 * frac
            rr(d, (1240, y + 35, xk, y + 45), 5, fill=lerp(BG, TEAL, b))
            d.ellipse((xk - 12, y + 30, xk + 12, y + 50), fill=lerp(BG, (242, 242, 244), b))
            text(d, (1520, y + 40), f"{v:+.1f} {unit}".replace("-", "−") if unit == "dB" else f"{v:.1f} {unit}",
                 20, lerp(BG, MUTE, b), mono=True)
        # lampu clip
        clip_on = prog(T, 39.4, 39.6) * (1 - prog(T, 40.6, 40.8))
        text(d, (1080, 572), "Clip", 24, lerp(BG, FG2, b), "Medium")
        dot = lerp(MOCK2, RED, clip_on)
        d.ellipse((1100, 600, 1140, 640), fill=dot if b > 0.5 else lerp(BG, dot, b))
        text(d, (1156, 620), "Klik untuk menghapus", 20, lerp(BG, MUTE, b))
    path = [(30.6, 330, 300), (31.4, 816, 400), (31.8, 816, 400), (33.0, 620, 530),
            (35.4, 640, 680), (36.2, 1545, 340), (36.6, 1530, 340), (36.9, 1530, 340),
            (40.2, 1100, 620), (40.6, 1100, 620), (42.0, 1100, 620)]
    clicks = [(31.6, 816, 400), (36.6, 1530, 340), (40.6, 1100, 620)]
    cursor_overlay(d, T, path, clicks)
    caption(d, ["Gate, EQ, kompresor, delay.", "Limiter untuk output."], cap_alpha(T, 30.4, 41.6))
    return img


def v2_s5(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    rr(d, (300, 240, 960, 800), 18, fill=MOCK, outline=LINE, width=2)
    text(d, (340, 280), "PRESET TERSIMPAN", 20, MUTE, "Semibold")
    rr(d, (340, 320, 920, 380), 12, fill=MOCK2, outline=LINE, width=2)
    sel = "Podcast Malam" if T >= 44.0 else "Pilih preset"
    text(d, (366, 350), sel, 26, FG if T >= 44.0 else MUTE)
    tri(d, 892, 350)
    if 43.2 <= T < 44.3:
        rr(d, (340, 392, 920, 540), 12, fill=MOCK2, outline=LINE, width=2)
        for k, nm in enumerate(("Live Stream", "Podcast Malam", "Rapat Online")):
            y = 422 + k * 44
            if k == 1:
                rr(d, (352, y - 20, 908, y + 20), 9, fill=TEAL)
                col = INK
            else:
                col = FG2
            text(d, (372, y), nm, 24, col)
    rr(d, (340, 600, 470, 650), 10, fill=MOCK2)
    text(d, (405, 625), "Muat", 24, FG2, "Semibold", anchor="mm")
    rr(d, (490, 600, 630, 650), 10, fill=MOCK2)
    text(d, (560, 625), "Hapus", 24, RED, "Semibold", anchor="mm")
    text(d, (340, 700), "SIMPAN SEBAGAI", 20, MUTE, "Semibold")
    rr(d, (340, 728, 720, 776), 10, fill=MOCK2, outline=LINE, width=2)
    text(d, (366, 752), "Nama preset", 24, MUTE)
    rr(d, (740, 728, 920, 776), 10, fill=TEAL)
    text(d, (830, 752), "Simpan", 24, INK, "Semibold", anchor="mm")

    rr(d, (1000, 240, 1620, 780), 18, fill=MOCK, outline=LINE, width=2)
    text(d, (1040, 280), "TEMPLATE", 20, MUTE, "Semibold")
    cells = [("Podcast", 46.0), ("Streaming", 47.2), ("Online Meeting", 48.4), ("Recording", 49.6)]
    for k, (lab, t_on) in enumerate(cells):
        cx = 1040 + (k % 2) * 294
        cy_ = 310 + (k // 2) * 176
        on = T >= t_on
        rr(d, (cx, cy_, cx + 270, cy_ + 152), 16, fill=TEAL if on else MOCK2, outline=None if on else LINE, width=2)
        text(d, (cx + 135, cy_ + 76), lab, 30, INK if on else FG2, "Semibold", anchor="mm")
    path = [(43.4, 600, 330), (44.0, 370, 418), (44.4, 405, 625), (44.8, 405, 625),
            (45.4, 1175, 386), (46.0, 1175, 386), (46.9, 1469, 386), (47.2, 1469, 386),
            (48.0, 1175, 556), (48.4, 1175, 556), (49.2, 1469, 556), (49.6, 1469, 556),
            (50.6, 1469, 556)]
    clicks = [(44.8, 405, 625), (46.0, 1175, 386), (47.2, 1469, 386), (48.4, 1175, 556), (49.6, 1469, 556)]
    cursor_overlay(d, T, path, clicks)
    caption(d, ["Simpan preset. Mulai dari template."], cap_alpha(T, 42.4, 51.6))
    return img


def v2_s6(T):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    end_card(d, T, 52.0, ["Coba AudioHub."], 54.0, kicker="Coba AudioHub.")
    return img


SCENES = {
    1: [(0, 8, v1_s1), (8, 18, v1_s2), (18, 32, v1_s3), (32, 50, v1_s4), (50, 60, v1_s5)],
    2: [(0, 6, v2_s1), (6, 18, v2_s2), (18, 30, v2_s3), (30, 42, v2_s4), (42, 52, v2_s5), (52, 60, v2_s6)],
}


# ---------------------------------------------------------------- audio

def mtof(m):
    return 440.0 * 2 ** ((m - 69) / 12)


def tone_env(n, decay):
    t = np.arange(n) / SR
    return t, np.exp(-t * decay)


def add(buf, sig, t0, gain=1.0):
    i0 = int(t0 * SR)
    if i0 >= len(buf) or i0 < 0:
        return
    n = min(len(sig), len(buf) - i0)
    buf[i0:i0 + n] += sig[:n] * gain


def s_kick():
    n = int(0.45 * SR)
    t = np.arange(n) / SR
    f = 46 + 110 * np.exp(-t * 26)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * np.exp(-t * 8.5)


def s_hat(rng):
    n = int(0.08 * SR)
    x = rng.standard_normal(n)
    y = np.diff(x, prepend=0.0)
    t = np.arange(n) / SR
    return y * np.exp(-t * 85)


def s_clap(rng):
    n = int(0.16 * SR)
    x = rng.standard_normal(n)
    x = np.convolve(x, np.ones(4) / 4, mode="same")
    t = np.arange(n) / SR
    return x * np.exp(-t * 32)


def s_bass(m, dur):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = mtof(m)
    y = sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, 7))
    env = np.minimum(t / 0.006, 1) * np.exp(-t * 3.2)
    return y * env * 0.5


def s_pad(notes, dur):
    n = int(dur * SR)
    t = np.arange(n) / SR
    y = np.zeros(n)
    for m in notes:
        f = mtof(m)
        y += np.sin(2 * np.pi * f * (1 - 0.003) * t) + np.sin(2 * np.pi * f * (1 + 0.003) * t)
    env = np.minimum(t / 0.7, 1) * np.minimum((dur - t) / 0.9, 1)
    return y * np.clip(env, 0, 1) * 0.07


def s_pluck(m, dur=0.45):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = mtof(m)
    y = np.sin(2 * np.pi * f * t) + 0.25 * np.sin(2 * np.pi * 2 * f * t)
    return y * np.exp(-t * 7) * 0.16


def s_click(rng):
    n = int(0.05 * SR)
    t = np.arange(n) / SR
    y = np.sin(2 * np.pi * 1800 * t) * np.exp(-t * 260) + 0.3 * rng.standard_normal(n) * np.exp(-t * 700)
    return y * 0.32


def s_tick(freq):
    n = int(0.05 * SR)
    t = np.arange(n) / SR
    return np.sin(2 * np.pi * freq * t) * np.exp(-t * 110) * 0.2


def s_key(rng):
    n = int(0.05 * SR)
    t = np.arange(n) / SR
    return (np.sin(2 * np.pi * 1100 * t) * np.exp(-t * 300) + 0.2 * rng.standard_normal(n) * np.exp(-t * 900)) * 0.18


def s_pop():
    n = int(0.16 * SR)
    t = np.arange(n) / SR
    f = 450 + 450 * np.minimum(t / 0.14, 1)
    ph = 2 * np.pi * np.cumsum(f) / SR
    return np.sin(ph) * np.exp(-t * 18) * 0.28


def s_note(m, dur=0.6):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = mtof(m)
    y = np.sin(2 * np.pi * f * t) + 0.2 * np.sin(2 * np.pi * 2 * f * t)
    return y * np.exp(-t * 8) * 0.22


def s_bell(freq, dur, amp):
    n = int(dur * SR)
    t = np.arange(n) / SR
    y = np.sin(2 * np.pi * freq * t) + 0.35 * np.sin(2 * np.pi * 2.01 * freq * t) * np.exp(-t * 4)
    return y * np.exp(-t * 3.2) * amp


def s_whoosh(rng):
    n = int(1.1 * SR)
    x = rng.standard_normal(n)
    y = np.zeros(n)
    acc = 0.0
    for i in range(n):
        a = 0.01 + 0.25 * (i / n) ** 2
        acc += a * (x[i] - acc)
        y[i] = acc
    t = np.arange(n) / SR
    env = (t / 1.1) ** 1.6 * (1 - t / 1.1)
    return y * env * 9.0


def s_warn():
    return np.concatenate([s_tick(1250), np.zeros(int(0.08 * SR)), s_tick(1250)])


def build_sfx(num, rng):
    buf = np.zeros(int(DUR * SR) + SR)
    def ev(t, sig, gain=1.0):
        add(buf, sig, t, gain)

    if num == 1:
        for t in (8.0, 18.0, 32.0, 50.0):
            ev(t, s_whoosh(rng), 0.9)
        ev(9.4, s_bell(880, 1.8, 0.22))
        ev(9.4, s_bell(1318.5, 1.8, 0.12))
        ev(20.2, s_click(rng))
        ev(21.2, s_pop(), 0.9)
        for k in range(9):
            ev(22.0 + k * 0.35, s_key(rng))
        ev(25.1, s_click(rng))
        for k, m in enumerate((72, 76, 79)):
            ev(27.6 + k * 0.12, s_bell(mtof(m), 1.6, 0.22))
        ev(33.9, s_click(rng))
        ev(36.9, s_click(rng))
        ev(39.6, s_click(rng))
        ev(41.8, s_click(rng))
        for k, m in enumerate((72, 76, 79, 84)):
            ev(42.3 + k * 0.1, s_bell(mtof(m), 2.0, 0.24))
        ev(43.9, s_click(rng))
        ev(44.0, s_pop(), 0.8)
        ev(50.2, s_bell(mtof(60), 3.0, 0.22))
        for m in (64, 67, 72):
            ev(50.2, s_bell(mtof(m), 3.0, 0.16))
        ev(53.0, s_bell(mtof(79), 1.2, 0.14))
    else:
        for t in (6.0, 18.0, 30.0, 42.0, 52.0):
            ev(t, s_whoosh(rng), 0.9)
        # ketukan slider gain
        for k, t in enumerate(np.arange(7.0, 10.61, 0.2)):
            ev(t, s_tick(520 + 40 * k))
        # ketukan slider pan
        for t in np.arange(10.8, 13.81, 0.3):
            ev(t, s_tick(700))
        ev(14.2, s_click(rng))
        ev(15.2, s_click(rng))
        ev(16.6, s_click(rng))
        # nada sel matriks
        for t, m in ((19.0, 72), (19.7, 76), (20.4, 79), (21.1, 81), (21.8, 84)):
            ev(t, s_note(m))
        ev(22.4, s_pop(), 0.8)
        ev(23.4, s_tick(1000))
        ev(24.0, s_click(rng))
        ev(26.0, s_click(rng))
        for k in range(5):
            ev(26.4 + k * 0.25, s_pop(), 0.5)
        # efek
        ev(30.2, s_pop(), 0.8)
        ev(31.6, s_click(rng))
        for t in np.arange(32.2, 35.81, 0.25):
            ev(t, s_tick(640))
        ev(36.0, s_pop(), 0.8)
        ev(36.6, s_click(rng))
        for t in np.arange(36.8, 39.21, 0.3):
            ev(t, s_tick(760))
        ev(39.4, s_warn(), 0.7)
        ev(40.6, s_click(rng))
        # preset dan template
        ev(43.2, s_pop(), 0.8)
        ev(44.0, s_click(rng))
        ev(44.8, s_click(rng))
        for t, m in ((46.0, 72), (47.2, 76), (48.4, 79), (49.6, 84)):
            ev(t, s_click(rng))
            ev(t + 0.02, s_note(m, 0.5), 0.8)
        # kartu akhir
        ev(52.0, s_bell(mtof(60), 3.0, 0.22))
        for m in (64, 67, 72):
            ev(52.0, s_bell(mtof(m), 3.0, 0.16))
        ev(54.0, s_bell(mtof(79), 1.2, 0.14))
    return buf


def build_music(num, rng):
    n = int(DUR * SR)
    out = np.zeros(n)
    if num == 1:
        bpm = 108
        chords = [(57, 60, 64), (53, 57, 60), (48, 52, 55), (55, 59, 62)]  # Am F C G
    else:
        bpm = 124
        chords = [(50, 53, 57), (46, 50, 53), (53, 57, 60), (48, 52, 55)]  # Dm Bb F C
    beat = 60.0 / bpm
    bar = 4 * beat

    # level tiap lapisan dari waktu ke waktu (titik kunci)
    if num == 1:
        lv = {
            "pad": [(0, 0.55), (50, 0.55), (52, 0.0)],
            "arp": [(0, 0.0), (4, 0.0), (8, 0.7), (32, 0.9), (50, 0.2), (52, 0.0)],
            "drum": [(0, 0.0), (7.5, 0.0), (8.5, 0.55), (18, 0.6), (32, 0.9), (50, 0.2), (52, 0.0)],
            "bass": [(0, 0.0), (7.5, 0.0), (8.5, 0.8), (32, 1.0), (50, 0.3), (52, 0.0)],
        }
    else:
        lv = {
            "pad": [(0, 0.45), (52, 0.45), (55, 0.0)],
            "arp": [(0, 0.0), (1.5, 0.6), (30, 0.9), (42, 0.5), (52, 0.2), (56, 0.0)],
            "drum": [(0, 0.0), (1.0, 0.8), (18, 1.0), (30, 1.0), (42, 0.7), (52, 0.3), (56, 0.0)],
            "bass": [(0, 0.0), (5.5, 0.0), (6.5, 0.85), (30, 1.0), (42, 0.7), (52, 0.3), (56, 0.0)],
        }

    def gain(key):
        xs, ys = zip(*lv[key])
        return np.interp(np.arange(n) / SR, xs, ys)

    pad = np.zeros(n)
    arp = np.zeros(n)
    drum = np.zeros(n)
    bass = np.zeros(n)
    nbars = int(DUR / bar) + 2
    for b in range(nbars):
        t0 = b * bar
        if t0 >= DUR:
            break
        ch = chords[b % len(chords)]
        add(pad, s_pad([m + 12 for m in ch], bar + 0.6), t0)
        # arpeggio 8 atau 16 nada per bar
        steps = 8 if num == 1 else 16
        for s in range(steps):
            m = ch[s % 3] + (12 if (s // 3) % 2 == 0 else 0)
            add(arp, s_pluck(m + 12, 0.4 if num == 1 else 0.28), t0 + s * bar / steps, 0.9)
        # bass 8 nada
        for s in range(8):
            m = ch[0] - 24 + (12 if s % 4 == 2 else 0)
            add(bass, s_bass(m, beat / 2 * 0.9), t0 + s * beat / 2)
    for k in range(int(DUR / beat) + 2):
        t = k * beat
        if t >= DUR:
            break
        add(drum, s_kick(), t, 0.95 if k % 4 else 1.0)
        if k % 2 == 1:
            add(drum, s_clap(rng), t, 0.6)
        add(drum, s_hat(rng), t + beat / 2, 0.35)
        add(drum, s_hat(rng), t, 0.2)

    out += pad * gain("pad")
    out += arp * gain("arp")
    out += drum * gain("drum")
    out += bass * gain("bass")
    return out


def build_audio(num):
    rng = np.random.default_rng(20261019 + num)
    music = build_music(num, rng)
    sfx = build_sfx(num, rng)[: len(music)]
    x = 0.45 * music + 0.8 * sfx
    x = np.tanh(x * 1.3) / np.tanh(1.3)
    fi = int(0.6 * SR)
    x[:fi] *= np.linspace(0, 1, fi)
    fo = int(1.6 * SR)
    x[-fo:] *= np.linspace(1, 0, fo)
    peak = np.max(np.abs(x))
    x = x / peak * 0.89
    return x


def write_wav(x, path):
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    stereo = np.repeat(pcm[:, None], 2, axis=1)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(stereo.tobytes())


# ---------------------------------------------------------------- render

def make_bg():
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    glow = np.exp(-(((x - 0.22 * W) ** 2 + (y - 0.12 * H) ** 2) / (2 * 760.0 ** 2)))
    glow2 = np.exp(-(((x - 0.85 * W) ** 2 + (y - 1.05 * H) ** 2) / (2 * 620.0 ** 2)))
    base = np.array(BG, dtype=np.float32)
    teal = np.array(TEAL, dtype=np.float32)
    arr = base[None, None, :] + (teal - base)[None, None, :] * (0.075 * glow + 0.04 * glow2)[:, :, None]
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8))


BG_IMG = None


def frame_at(scenes, T):
    idx = len(scenes) - 1
    for i, (a, b, _) in enumerate(scenes):
        if a <= T < b:
            idx = i
            break
    a, _, fn = scenes[idx]
    img = fn(T)
    if idx > 0 and T - a < XF:
        prev = scenes[idx - 1][2](T)
        img = Image.blend(prev, img, (T - a) / XF)
    if T > DUR - 0.6:
        img = Image.blend(img, BG_IMG, (T - (DUR - 0.6)) / 0.6)
    return img


def render_video(num, out_path, scratch):
    global BG_IMG
    if BG_IMG is None:
        BG_IMG = make_bg()
    scenes = SCENES[num]
    wav = os.path.join(scratch, f"audiohub-v{num}.wav")
    write_wav(build_audio(num), wav)
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
        "-i", wav,
        "-map", "0:v", "-map", "1:a",
        "-c:v", "libx264", "-preset", "medium", "-crf", "18", "-pix_fmt", "yuv420p",
        "-c:a", "aac", "-b:a", "192k", "-t", str(DUR),
        "-movflags", "+faststart", out_path,
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    total = int(round(DUR * FPS))
    for i in range(total):
        img = frame_at(scenes, i / FPS)
        proc.stdin.write(img.tobytes())
        if i % 300 == 0:
            print(f"  video {num}: {i / FPS:5.1f}s / {DUR:.0f}s", flush=True)
    proc.stdin.close()
    proc.wait()
    if proc.returncode != 0:
        raise SystemExit(f"ffmpeg gagal untuk video {num}")
    os.remove(wav)
    print(f"selesai: {out_path}", flush=True)


OUT_NAMES = {
    1: "AudioHub-1-Perkenalan-60s.mp4",
    2: "AudioHub-2-Fitur-60s.mp4",
}
SCRATCH = "/private/tmp/claude-501/-Users-khincc-Documents-WEBSITE-BUILD-Audio-Hub/fc90f78d-d462-47c6-b1a1-8c0cd02e368e/scratchpad"


def main(argv):
    global BG_IMG
    os.makedirs(SCRATCH, exist_ok=True)
    if len(argv) >= 2 and argv[1] == "still":
        num = int(argv[2])
        T = float(argv[3])
        BG_IMG = make_bg()
        frame_at(SCENES[num], T).save(os.path.join(SCRATCH, f"still-v{num}-{T:05.1f}.png"))
        print(os.path.join(SCRATCH, f"still-v{num}-{T:05.1f}.png"))
        return
    nums = [int(argv[1])] if len(argv) >= 2 else [1, 2]
    for num in nums:
        render_video(num, os.path.join(HERE, OUT_NAMES[num]), SCRATCH)


if __name__ == "__main__":
    main(sys.argv)
