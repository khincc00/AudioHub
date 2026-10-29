#!/usr/bin/env python3
"""Video konsep AudioHub, 90 detik, 60 fps, dua versi audio.

  AudioHub-Konsep-dengan-vokal.mp4   musik + efek suara + narasi
  AudioHub-Konsep-tanpa-vokal.mp4    musik + efek suara saja

Gambar dibuat dengan Pillow. Musik dan efek suara memakai loop dan sampel asli
dari Apple Loops (GarageBand) yang terpasang di Mac ini, bukan sintesis.
Narasi: jika berkas narasi-vokal.wav ada di folder ini, berkas itu yang dipakai.
Jika tidak ada, narasi sementara dibuat dari suara "Damayanti" (macOS, say).

Pemakaian:
  python3 render_konsep.py              render video dan kedua versi audio
  python3 render_konsep.py audio        render ulang audio saja (gambar sudah ada)
  python3 render_konsep.py still 45.0   simpan satu frame untuk diperiksa
"""
import math
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, FPS, SR = 1920, 1080, 60, 48000
DUR = 90.0
XF = 0.35
HERE = os.path.dirname(os.path.abspath(__file__))
APPLE = "/Library/Audio/Apple Loops/Apple"
SCRATCH = "/private/tmp/claude-501/-Users-khincc-Documents-WEBSITE-BUILD-Audio-Hub/fc90f78d-d462-47c6-b1a1-8c0cd02e368e/scratchpad"

BG = (19, 19, 21)
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
KNOB = (242, 242, 244)
SITE = "audiohub.khincreator.com"
COPY = "Hak Cipta © 2026 taufiq sholikhin. Seluruh hak dilindungi."


# ---------------------------------------------------------------- easing dan util

def clamp(x, a=0.0, b=1.0):
    return max(a, min(b, x))


def ease_io(x):
    x = clamp(x)
    return 4 * x ** 3 if x < 0.5 else 1 - (-2 * x + 2) ** 3 / 2


def ease_out(x):
    x = clamp(x)
    return 1 - (1 - x) ** 3


def ease_back(x):
    c1 = 1.25
    c3 = c1 + 1
    x = clamp(x)
    return 1 + c3 * (x - 1) ** 3 + c1 * (x - 1) ** 2


def P(T, a, b, f=ease_io):
    """0 sebelum a, 1 sesudah b, dengan kurva f di antaranya."""
    return f((T - a) / (b - a))


def lerp(p, q, t):
    t = clamp(t)
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


def db_str(v):
    s = f"{v:+.1f}"
    if s in ("+0.0", "-0.0"):
        s = "0.0"
    return s.replace("-", "−")


def hl(d, box, num=None, a=1.0):
    """Sorotan teal yang masuk dengan halus. a = 0..1."""
    if a <= 0.01:
        return
    grow = 10 * (1 - ease_out(a))
    x0, y0, x1, y1 = box
    col = lerp(BG, TEAL, a)
    d.rounded_rectangle((x0 - 7 - grow, y0 - 7 - grow, x1 + 7 + grow, y1 + 7 + grow), radius=14,
                        outline=col, width=4)
    if num is not None:
        cx, cy = x0 - 7 - grow, y0 - 7 - grow
        d.ellipse((cx - 19, cy - 19, cx + 19, cy + 19), fill=col)
        d.text((cx, cy), str(num), font=font(22, "Bold"), fill=INK, anchor="mm")


# ---------------------------------------------------------------- kursor

CURSOR = [(0, 0), (0, 22), (6, 17), (10, 26), (14, 24), (10, 16), (18, 16)]


def cursor_at(T, pts):
    """Posisi kursor pada lintasan melengkung. pts = [(waktu, x, y), ...]. None jika belum/sudah tidak tampil."""
    if T < pts[0][0] or T >= pts[-1][0]:
        return None
    for (t0, x0, y0), (t1, x1, y1) in zip(pts, pts[1:]):
        if t0 <= T < t1:
            u = ease_io((T - t0) / (t1 - t0))
            dx, dy = x1 - x0, y1 - y0
            dist = math.hypot(dx, dy) + 1e-6
            bulge = 0.10 * dist * math.sin(math.pi * u)
            nx, ny = -dy / dist, dx / dist
            return x0 + dx * u + nx * bulge, y0 + dy * u + ny * bulge
    return pts[-1][1], pts[-1][2]


def draw_cursor(d, T, pts, clicks):
    for t, x, y in clicks:
        age = T - t
        if 0 <= age < 0.5:
            r = 8 + 48 * ease_out(age / 0.5)
            d.ellipse((x - r, y - r, x + r, y + r), outline=lerp(BG, TEAL, 1 - age / 0.5), width=4)
    p = cursor_at(T, pts)
    if p:
        x, y = p
        d.polygon([(x + px * 1.25, y + py * 1.25) for px, py in CURSOR], fill=(255, 255, 255),
                  outline=(10, 10, 12))


def fade_blend(base, a, draw_fn):
    """Gambar lapisan penuh lalu campur dengan base sebesar a."""
    if a <= 0.005:
        return base
    layer = BG_IMG.copy()
    draw_fn(ImageDraw.Draw(layer))
    return Image.blend(base, layer, clamp(a))


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


def tri(d, x, y, col=FG2):
    d.polygon([(x - 7, y - 4), (x + 7, y - 4), (x, y + 4)], fill=col)


def draw_window(d, r, T, st):
    """st: run (0..1), hl {nama: (nomor, alpha)}, menu (0..1), hover (indeks), out (dua fader 0..1)."""
    x0, y0, x1, y1 = r
    rr(d, r, 18, fill=MOCK, outline=LINE, width=2)
    it = top_items(r)
    cy = it["cy"]
    run = st.get("run", 0.0)

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
        for k, lab in enumerate(("S", "M")):
            cx = ix1 - 48 - k * 36 - 12
            rr(d, (cx - 14, y + 12, cx + 14, y + 40), 6, fill=MOCK)
            text(d, (cx, y + 26), lab, 18, MUTE, "Bold", anchor="mm")

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
            rr(d, (cx0, cy0, cx0 + 80, cy0 + 44), 8, fill=TEAL if on else MOCK2, outline=None if on else LINE)

    sx0, sy0, sx1, sy1 = parts["side"]
    rr(d, (sx0 + 14, sy0 + 14, sx1 - 14, sy0 + 56), 10, fill=MOCK2)
    seg_w = (sx1 - sx0 - 28) / 3
    rr(d, (sx0 + 18, sy0 + 18, sx0 + 10 + seg_w, sy0 + 52), 8, fill=LINE)
    for k, lab in enumerate(("Preset", "Perangkat", "Diagnostik")):
        text(d, (sx0 + 14 + (k + 0.5) * seg_w, sy0 + 35), lab, 18, FG if k == 0 else MUTE, anchor="mm")
    text(d, (sx0 + 26, sy0 + 92), "PRESET TERSIMPAN", 18, MUTE, "Semibold")
    for k, nm in enumerate(("Podcast Malam", "Live Stream", "Rapat Online")):
        y = sy0 + 116 + k * 58
        rr(d, (sx0 + 14, y, sx1 - 14, y + 46), 10, fill=MOCK2, outline=TEAL if k == 0 else None, width=2)
        text(d, (sx0 + 30, y + 23), nm, 21, FG2)

    ox0, oy0, ox1, oy1 = parts["out"]
    for j, lvl_f in enumerate(st.get("out", (0.833, 0.6))):
        sx = ox0 + 60 + j * 300
        text(d, (sx, oy0 + 24), f"Out {j + 1}", 20, FG2, "Semibold")
        text(d, (sx + 150, oy0 + 24), db_str(lvl_f * 72 - 60) + " dB", 20, MUTE, mono=True)
        ftop, fbot = oy0 + 56, oy1 - 16
        rr(d, (sx + 30, ftop, sx + 36, fbot), 3, fill=MOCK2)
        fy = fbot - (fbot - ftop) * lvl_f
        rr(d, (sx + 30, fy, sx + 36, fbot), 3, fill=TEAL)
        rr(d, (sx + 18, fy - 6, sx + 48, fy + 6), 4, fill=FG2)
        rr(d, (sx + 90, ftop, sx + 100, fbot), 4, fill=MOCK2)
        rr(d, (sx + 90, fbot - (fbot - ftop) * 0.55, sx + 100, fbot), 4, fill=TEAL)

    if st.get("menu", 0) > 0.01:
        m = st["menu"]
        mx, my = x1 - 320, y0 + 92 + (1 - ease_out(m)) * -10
        rr(d, (mx, my, mx + 300, my + 214), 14, fill=lerp(BG, (46, 46, 52), m), outline=LINE, width=2)
        rows = [("Sambung ulang otomatis", "✓"), ("Sample rate", "48,0 kHz"), ("Buffer", "512 frame"), ("Tampilan", "Sistem")]
        for k, (lab, val) in enumerate(rows):
            ry = my + 22 + k * 48
            hov = k == st.get("hover", -1)
            if hov:
                rr(d, (mx + 8, ry - 4, mx + 292, ry + 36), 9, fill=TEAL)
                ink = INK
            else:
                ink = lerp(BG, FG2, m)
            text(d, (mx + 20, ry + 16), lab, 20, ink)
            text(d, (mx + 282, ry + 16), val, 19, ink if hov else MUTE, anchor="rm")

    h = st.get("hl", {})
    boxes = {"dev": it["dev"], "vir": it["vir"], "buka": it["buka"], "mulai": it["mulai"],
             "gear": (gx - 22, gy - 22, gx + 22, gy + 22)}
    for name, (num, a) in h.items():
        if name in boxes:
            hl(d, boxes[name], num, a)
        elif name in parts:
            xa, ya, xb, yb = parts[name]
            hl(d, (xa + 8, ya + 8, xb - 8, yb - 8), num, a)


def hl_alpha(T, a, b, fade=0.25):
    return P(T, a, a + fade) * (1 - P(T, b - fade, b))


# ---------------------------------------------------------------- scene

def s1(T):
    """Pembuka: tiga sumber suara kacau, lalu menyatu."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    conv = P(T, 3.4, 5.4)
    amp_in = P(T, 0.0, 0.8)
    cy = 470
    specs = [(TEAL, 150, 0.012, 3.1, 0.0), (AMBER, 120, 0.017, -2.4, 1.3), (RED, 170, 0.009, 2.2, 2.6)]
    for col, amp, freq, om, ph in specs:
        pts = []
        for x in range(0, W + 1, 6):
            base = amp * math.sin(freq * x + om * T + ph) * (0.7 + 0.3 * math.sin(T * 0.9 + x * 0.0015))
            pts.append((x, cy + (1 - conv) * base * amp_in))
        d.line(pts, fill=col, width=6, joint="curve")
    return img


def s2(T):
    """Logo: bar naik bergantian, nama, dan tagline."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    logo_bars(d, 960, 430, T, 7.2, 1.3)
    a = P(T, 8.6, 9.6)
    text(d, (960, 612 + (1 - a) * 22), "AudioHub", 128, lerp(BG, FG, a), "Bold", anchor="mm")
    b = P(T, 9.9, 10.8)
    text(d, (960, 702 + (1 - b) * 14), "Routing audio profesional untuk macOS.", 40, lerp(BG, FG2, b), anchor="mm")
    return img


def logo_bars(d, cx, cy, T, t0, scale=1.0):
    heights = [0.35, 0.55, 0.8, 1.0, 0.8, 0.55, 0.35]
    for i, hf in enumerate(heights):
        p = P(T, t0 + i * 0.07, t0 + 0.7 + i * 0.07, ease_back)
        h = 170 * hf * scale * max(p, 0)
        if T > t0 + 1.6:
            h *= 0.94 + 0.06 * math.sin(T * 2.4 + i * 0.8)
        if h < 2:
            continue
        x = cx + (i - 3) * 32 * scale
        rr(d, (x - 9 * scale, cy - h / 2, x + 9 * scale, cy + h / 2), 9 * scale, fill=TEAL)


def s3(T):
    """Pemasangan: DMG, dialog kata sandi, driver terpasang."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    a = P(T, 14.6, 15.3)
    dx = (1 - P(T, 14.6, 15.3)) * -90
    rr(d, (260 + dx, 300, 860 + dx, 640), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    rr(d, (500 + dx, 380, 620 + dx, 500), 26, fill=lerp(BG, MOCK2, a))
    text(d, (560 + dx, 440), "PKG", 34, lerp(BG, TEAL, a), "Bold", anchor="mm", mono=True)
    text(d, (560 + dx, 548), "Install AudioHub.pkg", 28, lerp(BG, FG2, a), "Medium", anchor="mm")
    hl(d, (500 + dx, 380, 620 + dx, 500), None, hl_alpha(T, 16.1, 17.0))

    b = P(T, 16.5, 17.2)
    if b > 0.01:
        yy = (1 - b) * 26
        rr(d, (1000, 300 + yy, 1660, 640 + yy), 18, fill=lerp(BG, MOCK, b), outline=lerp(BG, LINE, b), width=2)
        text(d, (1040, 350 + yy), "Masukkan kata sandi", 32, lerp(BG, FG, b), "Semibold")
        rr(d, (1040, 400 + yy, 1620, 462 + yy), 10, fill=lerp(BG, MOCK2, b), outline=lerp(BG, LINE, b), width=2)
        n = int(clamp((T - 17.4) / 0.27, 0, 8))
        for k in range(n):
            cx = 1070 + k * 28
            d.ellipse((cx - 7, 421 + yy, cx + 7, 435 + yy), fill=lerp(BG, FG2, b))
        p = P(T, 20.5, 22.2)
        rr(d, (1040, 500 + yy, 1620, 512 + yy), 6, fill=lerp(BG, MOCK2, b))
        if p > 0.001:
            rr(d, (1040, 500 + yy, 1040 + 580 * p, 512 + yy), 6, fill=lerp(BG, TEAL, b))
        label = "Memasang…" if 20.3 <= T < 22.2 else "Instal"
        rr(d, (1440, 540 + yy, 1620, 596 + yy), 10, fill=lerp(BG, TEAL, b))
        text(d, (1530, 568 + yy), label, 26, lerp(BG, INK, b), "Semibold", anchor="mm")

    c = P(T, 22.3, 22.9, ease_out)
    if c > 0.01:
        y = 800 + (1 - c) * 34
        rr(d, (260, y - 40, 1660, y + 40), 16, fill=lerp(BG, MOCK, c), outline=lerp(BG, LINE, c), width=2)
        d.ellipse((294, y - 9, 312, y + 9), fill=lerp(BG, TEAL, c))
        text(d, (340, y - 10), "AVHub Driver terpasang", 30, lerp(BG, FG, c), "Semibold")
        text(d, (340, y + 18), "avhub_virtual1 · avhub_virtual2", 22, lerp(BG, MUTE, c), mono=True)

    path = [(15.2, 1500, 800), (16.0, 560, 440), (16.3, 560, 440), (19.6, 1530, 568),
            (20.3, 1530, 568), (20.7, 1530, 568), (21.6, 1530, 568), (22.6, 1530, 568)]
    draw_cursor(d, T, path, [(16.3, 560, 440), (20.3, 1530, 568)])
    return img


def s4(T):
    """Setting perangkat: pilih, buka, mulai, lalu pengaturan."""
    img = BG_IMG.copy()
    hs = {
        "dev": (1, hl_alpha(T, 26.0, 27.6)),
        "vir": (2, hl_alpha(T, 27.9, 29.7)),
        "buka": (3, hl_alpha(T, 30.0, 31.6)),
        "mulai": (4, hl_alpha(T, 31.9, 33.2)),
        "gear": (5, hl_alpha(T, 33.8, 36.6)),
    }
    st = {
        "run": P(T, 32.2, 32.9),
        "hl": hs,
        "menu": P(T, 34.3, 34.9) * (1 - P(T, 36.2, 36.6)),
        "hover": 2 if 35.2 <= T < 36.2 else -1,
    }

    def draw(d):
        draw_window(d, WIN, T, st)
        path = [(25.4, 1100, 640), (26.0, 455, 196), (26.3, 455, 196), (27.7, 705, 196), (28.0, 705, 196),
                (29.9, 868, 196), (30.2, 868, 196), (31.7, 1001, 196), (32.0, 1001, 196),
                (33.7, 1768, 196), (34.0, 1768, 196), (35.4, 1640, 300), (36.6, 1640, 300)]
        draw_cursor(d, T, path, [(26.3, 455, 196), (28.0, 705, 196), (30.2, 868, 196),
                                 (32.0, 1001, 196), (34.0, 1768, 196)])

    base = BG_IMG.copy()
    img = BG_IMG.copy()
    draw(ImageDraw.Draw(img))
    return Image.blend(base, img, P(T, 24.6, 25.3))


def s5(T):
    """Slider: gain, pan, mute, solo, dan fader output."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    a = P(T, 36.6, 37.2)
    yy = (1 - a) * 30
    rr(d, (240, 230 + yy, 1240, 760 + yy), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    text(d, (280, 290 + yy), "Fisik In 1", 40, lerp(BG, FG, a), "Semibold")

    m_a = P(T, 45.4, 45.7)
    s_a = P(T, 46.0, 46.3)
    rr(d, (1080, 270 + yy, 1124, 314 + yy), 10, fill=lerp(MOCK2, RED, m_a))
    text(d, (1102, 292 + yy), "M", 24, lerp(MUTE, INK, m_a), "Bold", anchor="mm")
    rr(d, (1136, 270 + yy, 1180, 314 + yy), 10, fill=lerp(MOCK2, AMBER, s_a))
    text(d, (1158, 292 + yy), "S", 24, lerp(MUTE, INK, s_a), "Bold", anchor="mm")

    g = -60 + 72 * P(T, 37.4, 39.2) - 18 * P(T, 40.4, 41.0)
    fg_ = (g + 60) / 72
    text(d, (280, 430 + yy), "Gain", 28, lerp(BG, MUTE, a), "Medium")
    rr(d, (420, 425 + yy, 1080, 435 + yy), 5, fill=lerp(BG, MOCK2, a))
    rr(d, (420, 425 + yy, 420 + 660 * fg_, 435 + yy), 5, fill=lerp(BG, TEAL, a))
    kx = 420 + 660 * fg_
    d.ellipse((kx - 16, 414 + yy, kx + 16, 446 + yy), fill=lerp(BG, KNOB, a))
    text(d, (1110, 430 + yy), db_str(g) + " dB", 26, lerp(BG, FG2, a), mono=True)

    if T < 42.0:
        p = 0.0
    elif T < 42.8:
        p = -P(T, 42.0, 42.8)
    elif T < 44.2:
        p = -1 + 2 * P(T, 42.8, 44.2)
    elif T < 45.0:
        p = 1 - P(T, 44.2, 45.0)
    else:
        p = 0.0
    text(d, (280, 540 + yy), "Pan", 28, lerp(BG, MUTE, a), "Medium")
    rr(d, (420, 535 + yy, 1080, 545 + yy), 5, fill=lerp(BG, MOCK2, a))
    px = 420 + 660 * (p + 1) / 2
    rr(d, (min(750, px), 535 + yy, max(750, px), 545 + yy), 5, fill=lerp(BG, TEAL, a))
    d.ellipse((px - 16, 524 + yy, px + 16, 556 + yy), fill=lerp(BG, KNOB, a))
    lab = "C" if abs(p) < 0.02 else (f"L{abs(p) * 100:.0f}" if p < 0 else f"R{p * 100:.0f}")
    text(d, (1110, 540 + yy), lab, 26, lerp(BG, FG2, a), mono=True)

    lvl = 0.55 + 0.25 * math.sin(T * 6.1) * math.sin(T * 2.3)
    if T > 37.5:
        rr(d, (420, 600 + yy, 1080, 612 + yy), 6, fill=lerp(BG, MOCK2, a))
        rr(d, (420, 600 + yy, 420 + 660 * clamp(lvl) * a, 612 + yy), 6, fill=lerp(BG, TEAL, a))

    rr(d, (1320, 230, 1640, 760), 18, fill=MOCK, outline=LINE, width=2)
    text(d, (1480, 270), "Out 1", 26, FG2, "Semibold", anchor="mm")
    f = 0.3 + 0.65 * P(T, 46.2, 47.4)
    f = f + (0.833 - f) * P(T, 47.8, 48.4)
    rr(d, (1476, 330, 1484, 640), 4, fill=MOCK2)
    fy = 640 - 310 * f
    rr(d, (1476, fy, 1484, 640), 4, fill=TEAL)
    rr(d, (1450, fy - 9, 1510, fy + 9), 5, fill=KNOB)
    text(d, (1480, 676), db_str(f * 72 - 60) + " dB", 24, MUTE, anchor="mm", mono=True)
    rr(d, (1380, 694, 1580, 740), 10, fill=MOCK2)
    text(d, (1480, 717), "0 dB", 24, FG2, "Semibold", anchor="mm")

    path = [(37.0, 1500, 700), (37.6, 420, 430), (39.2, 1080, 430), (40.8, 915, 430), (41.3, 915, 430),
            (42.0, 750, 540), (42.8, 420, 540), (44.2, 1080, 540), (45.0, 750, 540), (45.3, 1102, 292),
            (45.6, 1102, 292), (46.0, 1158, 292), (46.3, 1158, 292), (47.0, 1480, 345), (47.4, 1480, 382),
            (47.9, 1480, 717), (48.3, 1480, 717), (48.7, 1480, 717)]
    draw_cursor(d, T, path, [(45.5, 1102, 292), (46.1, 1158, 292), (47.9, 1480, 717)])
    return img


def s6(T):
    """Routing: matriks, menu klik kanan, dan tampilan graph."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    a = P(T, 50.1, 50.6)
    yy = (1 - a) * 24
    rr(d, (300, 230 + yy, 1620, 760 + yy), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    text(d, (340, 270 + yy), "Routing", 30, lerp(BG, FG, a), "Semibold")

    graph_t = P(T, 57.0, 57.4)
    rr(d, (1420, 246 + yy, 1600, 296 + yy), 12, fill=lerp(BG, MOCK2, a))
    rr(d, (1420 + 4 + 86 * graph_t, 250 + yy, 1420 + 86 + 86 * graph_t, 292 + yy), 9, fill=LINE)
    text(d, (1465, 271 + yy), "Matriks", 22, lerp(MUTE, FG, 1 - graph_t), anchor="mm")
    text(d, (1555, 271 + yy), "Graph", 22, lerp(MUTE, FG, graph_t), anchor="mm")

    lit_t = {(0, 0): 51.0, (0, 2): 51.5, (1, 1): 52.0, (2, 3): 52.5, (3, 0): 53.0}
    if T < 57.2:
        for j in range(4):
            text(d, (420 + j * 170 + 75, 362 + yy), f"Out {j + 1}", 22, lerp(BG, MUTE, a), anchor="mm")
        for i in range(4):
            text(d, (350, 380 + i * 96 + 37 + yy), f"In {i + 1}", 22, lerp(BG, MUTE, a), anchor="lm")
            for j in range(4):
                x0 = 420 + j * 170
                y0 = 380 + i * 96 + yy
                on_a = P(T, lit_t[(i, j)], lit_t[(i, j)] + 0.35, ease_back) if (i, j) in lit_t else 0.0
                fill = lerp(MOCK2, TEAL, on_a)
                rr(d, (x0, y0, x0 + 150, y0 + 74), 12, fill=lerp(BG, fill, a), outline=lerp(BG, LINE, a) if on_a < 0.5 else None, width=2)
                if (i, j) == (1, 1) and T >= 55.7:
                    text(d, (x0 + 75, y0 + 37), "−6 dB", 24, INK, "Semibold", anchor="mm")
        menu_a = P(T, 54.1, 54.6) * (1 - P(T, 55.6, 55.9))
        if menu_a > 0.01:
            mx, my = 700, 440 + (1 - ease_out(menu_a)) * -12
            rr(d, (mx, my, mx + 290, my + 232), 14, fill=lerp(MOCK, (46, 46, 52), menu_a), outline=LINE, width=2)
            items = [("Send 0 dB", 0), ("Send −6 dB", 1), ("Send −12 dB", 2), ("Matikan koneksi", 3)]
            for k, (lab, idx) in enumerate(items):
                y = my + 26 + k * 46 + (12 if k == 3 else 0)
                if k == 3:
                    d.line((mx + 14, y - 22, mx + 276, y - 22), fill=LINE, width=2)
                hov = idx == 1 and 55.0 <= T < 55.6
                if hov:
                    rr(d, (mx + 8, y - 20, mx + 282, y + 20), 9, fill=TEAL)
                    col = INK
                else:
                    col = lerp(MOCK, FG2, menu_a)
                text(d, (mx + 22, y), lab, 22, col)
    else:
        for i in range(4):
            text(d, (380, 380 + i * 96 + 37), f"In {i + 1}", 22, MUTE)
        for j in range(4):
            text(d, (1440, 380 + j * 96 + 37), f"Out {j + 1}", 22, MUTE)
        routes = [(0, 0), (0, 2), (1, 1), (2, 3), (3, 0)]
        for k, (i, j) in enumerate(routes):
            p = P(T, 57.2 + k * 0.2, 57.2 + k * 0.2 + 0.9)
            if p < 0.005:
                continue
            p0 = (430, 380 + i * 96 + 37)
            p3 = (1420, 380 + j * 96 + 37)
            mid = (p0[0] + p3[0]) / 2
            pts = []
            for s in range(0, 81):
                u = s / 80 * p
                x = (1 - u) ** 3 * p0[0] + 3 * (1 - u) ** 2 * u * mid + 3 * (1 - u) * u ** 2 * mid + u ** 3 * p3[0]
                y = (1 - u) ** 3 * p0[1] + 3 * (1 - u) ** 2 * u * p0[1] + 3 * (1 - u) * u ** 2 * p3[1] + u ** 3 * p3[1]
                pts.append((x, y))
            d.line(pts, fill=TEAL, width=5, joint="curve")

    path = [(53.3, 1000, 520), (54.0, 665, 513), (54.3, 665, 513), (55.1, 845, 512), (55.5, 845, 512),
            (56.5, 1555, 271), (56.8, 1555, 271), (57.4, 1555, 271)]
    draw_cursor(d, T, path, [(54.3, 665, 513), (55.5, 845, 512), (56.8, 1555, 271)])
    return img


def s7(T):
    """Efek per input dan limiter output, lalu clip."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    a = P(T, 62.2, 62.8)
    ox = (1 - a) * -40
    rr(d, (200 + ox, 220, 900 + ox, 780), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    text(d, (240 + ox, 262), "Fisik In 1", 30, lerp(BG, FG, a), "Semibold")
    rr(d, (240 + ox, 300, 860 + ox, 350), 12, fill=lerp(BG, MOCK2, a))
    for k, lab in enumerate(("Gate", "EQ", "Kompresor", "Delay")):
        x0 = 240 + k * 155 + ox
        if lab == "EQ":
            rr(d, (x0 + 4, 304, x0 + 151, 346), 9, fill=lerp(BG, LINE, a))
        text(d, (x0 + 77, 325), lab, 22, lerp(BG, FG if lab == "EQ" else MUTE, a), anchor="mm")
    eq_on = P(T, 63.8, 64.1)
    text(d, (240 + ox, 410), "Aktifkan EQ", 26, lerp(BG, FG2, a), "Medium")
    tx = 760 + 40 * eq_on + ox
    rr(d, (760 + ox, 396, 820 + ox, 424), 14, fill=lerp(MOCK2, TEAL, eq_on))
    d.ellipse((tx + 2, 398, tx + 26, 422), fill=lerp(BG, KNOB, a))
    eq_rows = [("Low shelf", 3.0 * P(T, 64.2, 65.2), 500),
               ("Mid peak", 2.0 * P(T, 65.2, 66.2), 580),
               ("High shelf", -2.0 * P(T, 66.2, 67.2), 660)]
    for lab, v, y in eq_rows:
        text(d, (240 + ox, y), lab, 24, lerp(BG, FG2, a), "Medium")
        frac = (v + 15) / 30
        rr(d, (420 + ox, y - 5, 720 + ox, y + 5), 5, fill=lerp(BG, MOCK2, a))
        xk = 420 + 300 * frac + ox
        rr(d, (min(570 + ox, xk), y - 5, max(570 + ox, xk), y + 5), 5, fill=lerp(BG, TEAL, a))
        d.ellipse((xk - 13, y - 13, xk + 13, y + 13), fill=lerp(BG, KNOB, a))
        text(d, (742 + ox, y), db_str(v) + " dB", 22, lerp(BG, MUTE, a), mono=True)

    b = P(T, 67.4, 68.0)
    if b > 0.01:
        bx = (1 - b) * 40
        rr(d, (1040 + bx, 300, 1620 + bx, 700), 18, fill=lerp(BG, MOCK, b), outline=lerp(BG, LINE, b), width=2)
        text(d, (1080 + bx, 340), "Limiter · Out 1", 30, lerp(BG, FG, b), "Semibold")
        lim_on = P(T, 68.2, 68.5)
        rr(d, (1500 + bx, 326, 1560 + bx, 354), 14, fill=lerp(BG, lerp(MOCK2, TEAL, lim_on), b))
        text(d, (1080 + bx, 396), "Aktifkan limiter", 24, lerp(BG, FG2, b), "Medium")
        rows = [("Ceiling", -6 + 5 * P(T, 68.4, 69.4), -12, 0, "dB", 455),
                ("Lookahead", 6.0 * P(T, 69.4, 70.4), 0, 20, "ms", 525)]
        for lab, v, lo, hi, unit, ty in rows:
            text(d, (1080 + bx, ty + 5), lab, 22, lerp(BG, FG2, b), "Medium")
            frac = (v - lo) / (hi - lo)
            rr(d, (1240 + bx, ty, 1500 + bx, ty + 10), 5, fill=lerp(BG, MOCK2, b))
            xk = 1240 + 260 * frac + bx
            rr(d, (1240 + bx, ty, xk, ty + 10), 5, fill=lerp(BG, TEAL, b))
            d.ellipse((xk - 12, ty - 7, xk + 12, ty + 17), fill=lerp(BG, KNOB, b))
            vtxt = db_str(v) + " dB" if unit == "dB" else f"{v:.1f} ms"
            text(d, (1520 + bx, ty + 5), vtxt, 20, lerp(BG, MUTE, b), mono=True)
        clip_on = P(T, 70.9, 71.0, ease_out) * (1 - P(T, 71.4, 71.6))
        text(d, (1080 + bx, 632), "Clip", 24, lerp(BG, FG2, b), "Medium")
        dot = lerp(MOCK2, RED, clip_on)
        d.ellipse((1140 + bx, 614, 1172 + bx, 646), fill=lerp(BG, dot, b))
        text(d, (1190 + bx, 630), "Klik untuk menghapus", 20, lerp(BG, MUTE, b))

    path = [(63.0, 640, 720), (63.6, 816, 400), (63.9, 816, 400), (64.8, 600, 500), (66.0, 590, 580),
            (67.0, 550, 660), (67.8, 1530, 340), (68.2, 1530, 340), (68.8, 1478, 455), (70.0, 1318, 525),
            (70.6, 1318, 525), (71.2, 1156, 630), (71.6, 1156, 630), (72.2, 1156, 630)]
    draw_cursor(d, T, path, [(63.9, 816, 400), (68.2, 1530, 340), (71.4, 1156, 630)])
    return img


def s8(T):
    """Preset dan template."""
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    a = P(T, 72.6, 73.2)
    rr(d, (300, 240 + (1 - a) * 24, 960, 800 + (1 - a) * 24), 18, fill=lerp(BG, MOCK, a), outline=lerp(BG, LINE, a), width=2)
    b = P(T, 72.9, 73.5)
    rr(d, (1000, 240 + (1 - b) * 24, 1620, 800 + (1 - b) * 24), 18, fill=lerp(BG, MOCK, b), outline=lerp(BG, LINE, b), width=2)
    text(d, (340, 280), "PRESET TERSIMPAN", 20, lerp(BG, MUTE, a), "Semibold")
    rr(d, (340, 320, 920, 380), 12, fill=lerp(BG, MOCK2, a), outline=lerp(BG, LINE, a), width=2)
    chosen = T >= 74.5
    text(d, (366, 350), "Podcast Malam" if chosen else "Pilih preset", 26,
         lerp(BG, FG if chosen else MUTE, a))
    tri(d, 892, 350, lerp(BG, FG2, a))
    list_a = P(T, 73.8, 74.1) * (1 - P(T, 74.6, 74.9))
    if list_a > 0.01:
        rr(d, (340, 392, 920, 540), 12, fill=lerp(BG, MOCK2, list_a), outline=lerp(BG, LINE, list_a), width=2)
        for k, nm in enumerate(("Live Stream", "Podcast Malam", "Rapat Online")):
            y = 422 + k * 44
            sel = k == 1 and T >= 74.0
            if sel:
                rr(d, (352, y - 20, 908, y + 20), 9, fill=lerp(MOCK2, TEAL, list_a))
            text(d, (372, y), nm, 24, lerp(MOCK2, INK if sel else FG2, list_a))
    rr(d, (340, 600, 470, 650), 10, fill=lerp(BG, MOCK2, a))
    text(d, (405, 625), "Muat", 24, lerp(BG, FG2, a), "Semibold", anchor="mm")
    rr(d, (490, 600, 630, 650), 10, fill=lerp(BG, MOCK2, a))
    text(d, (560, 625), "Hapus", 24, lerp(BG, RED, a), "Semibold", anchor="mm")
    text(d, (340, 700), "SIMPAN SEBAGAI", 20, lerp(BG, MUTE, a), "Semibold")
    rr(d, (340, 728, 720, 776), 10, fill=lerp(BG, MOCK2, a), outline=lerp(BG, LINE, a), width=2)
    text(d, (366, 752), "Nama preset", 24, lerp(BG, MUTE, a))
    rr(d, (740, 728, 920, 776), 10, fill=lerp(BG, TEAL, a))
    text(d, (830, 752), "Simpan", 24, lerp(BG, INK, a), "Semibold", anchor="mm")

    text(d, (1040, 280), "TEMPLATE", 20, lerp(BG, MUTE, b), "Semibold")
    cells = [("Podcast", 76.6), ("Streaming", 77.6), ("Online Meeting", 78.6), ("Recording", 79.6)]
    for k, (lab, t_on) in enumerate(cells):
        cx = 1040 + (k % 2) * 294
        cy_ = 310 + (k // 2) * 176
        on = P(T, t_on, t_on + 0.3)
        rr(d, (cx, cy_, cx + 270, cy_ + 152), 16, fill=lerp(MOCK2, TEAL, on), outline=lerp(BG, LINE, b) if on < 0.5 else None, width=2)
        text(d, (cx + 135, cy_ + 76), lab, 30, lerp(FG2, INK, on), "Semibold", anchor="mm")

    path = [(73.4, 700, 430), (73.6, 630, 350), (73.8, 630, 350), (74.2, 640, 466), (74.4, 640, 466),
            (75.0, 640, 466), (75.5, 405, 625), (75.8, 405, 625), (76.2, 1175, 386), (76.6, 1175, 386),
            (77.2, 1469, 386), (77.6, 1469, 386), (78.2, 1175, 564), (78.6, 1175, 564), (79.2, 1469, 564),
            (79.6, 1469, 564), (80.2, 1469, 564)]
    draw_cursor(d, T, path, [(73.6, 630, 350), (74.4, 640, 466), (75.6, 405, 625), (76.6, 1175, 386),
                             (77.6, 1469, 386), (78.6, 1175, 564), (79.6, 1469, 564)])
    return img


def end_card(T, t0, cta_t):
    img = BG_IMG.copy()
    d = ImageDraw.Draw(img)
    logo_bars(d, 960, 380, T, t0)
    text(d, (960, 580), "AudioHub", 118, FG, "Bold", anchor="mm")
    a = P(T, t0 + 1.4, t0 + 2.3)
    text(d, (960, 664 + (1 - a) * 12), "Routing audio profesional untuk macOS.", 40, lerp(BG, FG2, a), anchor="mm")
    b = P(T, cta_t, cta_t + 0.7, ease_back)
    if b > 0.01:
        f = font(40, "Bold")
        w = d.textlength(SITE, font=f) + 80
        rr(d, (960 - w / 2 * b, 748 - 34 * (1 - b), 960 + w / 2 * b, 816 + 34 * (1 - b)), 34, fill=TEAL)
        if b > 0.6:
            d.text((960, 782), SITE, font=f, fill=INK, anchor="mm")
    c = P(T, cta_t + 1.0, cta_t + 1.8)
    text(d, (960, 890), "Versi 0.3.0 · macOS 13 atau lebih baru", 26, lerp(BG, MUTE, c), anchor="mm")
    text(d, (960, 930), COPY, 22, lerp(BG, MUTE, c), anchor="mm")
    return img


def s9(T):
    img = end_card(T, 82.2, 85.0)
    return Image.blend(img, BG_IMG, P(T, 89.2, 90.0))


SCENES = [
    (0.0, 7.0, s1), (7.0, 14.5, s2), (14.5, 24.5, s3), (24.5, 36.5, s4), (36.5, 50.0, s5),
    (50.0, 62.0, s6), (62.0, 72.5, s7), (72.5, 82.0, s8), (82.0, 90.0, s9),
]


def frame_at(T):
    idx = len(SCENES) - 1
    for i, (a, b, _) in enumerate(SCENES):
        if a <= T < b:
            idx = i
            break
    a, _, fn = SCENES[idx]
    img = fn(T)
    if idx > 0 and T - a < XF:
        prev = SCENES[idx - 1][2](T)
        img = Image.blend(prev, img, (T - a) / XF)
    return img


# ---------------------------------------------------------------- audio: decode dan utilitas

def decode(path, tempo=1.0):
    """Baca berkas audio jadi float32 stereo 48 kHz. tempo > 1 mempercepat tanpa mengubah nada."""
    filt = ["-filter:a", f"atempo={tempo:.6f}"] if abs(tempo - 1.0) > 1e-4 else []
    raw = subprocess.run(["ffmpeg", "-v", "error", "-i", path, *filt, "-f", "f32le", "-ac", "2", "-ar", str(SR), "-"],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).copy()


def apple(rel):
    return os.path.join(APPLE, rel)


def pitch(x, semis):
    r = 2 ** (semis / 12)
    idx = np.arange(0, len(x) - 1, r)
    i0 = idx.astype(int)
    frac = (idx - i0)[:, None]
    return x[i0] * (1 - frac) + x[i0 + 1] * frac


def fade(x, fin=0.005, fout=0.05):
    x = x.copy()
    n = len(x)
    fi, fo = min(int(fin * SR), n), min(int(fout * SR), n)
    if fi > 0:
        x[:fi] *= np.linspace(0, 1, fi)[:, None]
    if fo > 0:
        x[-fo:] *= np.linspace(1, 0, fo)[:, None]
    return x


def onsets(x, thresh=0.25, gap=0.12):
    m = np.abs(x).max(axis=1)
    env = np.convolve(m, np.ones(64) / 64, mode="same")
    peak = env.max()
    out, last = [], -1e9
    for i in np.where(env > thresh * peak)[0]:
        if i - last > gap * SR:
            out.append(i)
            last = i
    return out


def sample_at(x, start, length, fout=0.05):
    i0 = max(0, int(start))
    return fade(x[i0:i0 + int(length * SR)], 0.002, fout)


def add(buf, sig, t0, gain=1.0):
    i0 = int(round(t0 * SR))
    if i0 < 0 or i0 >= len(buf):
        return
    n = min(len(sig), len(buf) - i0)
    buf[i0:i0 + n] += sig[:n] * gain


def env_curve(points, n):
    xs, ys = zip(*points)
    t = np.arange(n) / SR
    return np.interp(t, xs, ys)[:, None]


def build_samples():
    S = {}
    stick = decode(apple("10 Vintage Breaks/Vintage Sidestick Topper.caf"))
    on = onsets(stick)
    S["click"] = sample_at(stick, on[0] - 40, 0.22, 0.08)
    S["tap"] = sample_at(stick, on[0] - 40, 0.12, 0.05)

    snap = decode(apple("10 Vintage Breaks/Simple Snap Topper.caf"))
    so = onsets(snap, 0.2, 0.25)
    S["ticks"] = [sample_at(snap, k - 60, 0.09, 0.03) for k in so[:12]]

    gtr = decode(apple("07 Chillwave/Landing in LA Guitar.caf"))
    go = onsets(gtr, 0.2, 0.3)
    S["gtr"] = sample_at(gtr, go[0] - 20, 1.6, 0.4)

    riser = decode(apple("09 Disco Funk/80s Synth FX Riser 01.caf"))
    S["whoosh"] = fade(riser[-int(1.6 * SR):], 0.15, 0.5)
    return S


def make_music(n):
    """Lapisan musik: loop 2 bar pada 100 BPM (bar = 2,4 detik). Semua loop Landing in LA sudah diset ke 120 BPM."""
    bar = 2.4
    tile = lambda x: np.tile(x, (int(math.ceil(n / len(x))) + 1, 1))[:n]
    stems = {}
    # faktor atempo = durasi_asli / durasi_baru. Loop 4 detik (2 bar pada 120 BPM) jadi 4,8 detik (2 bar pada 100 BPM).
    f120 = 100 / 120
    stems["clap"] = tile(decode(apple("07 Chillwave/Analog Clap Beat 01.caf")))
    stems["shaker"] = tile(decode(apple("Apple Loops for GarageBand/Shaker 01.caf"), tempo=100 / 94))
    stems["bass"] = tile(decode(apple("07 Chillwave/Landing in LA Bass 01.caf"), tempo=f120))
    stems["synth"] = tile(decode(apple("07 Chillwave/Landing in LA Synth 01.caf"), tempo=f120))
    stems["guitar"] = tile(decode(apple("07 Chillwave/Landing in LA Guitar.caf"), tempo=f120))
    return stems, bar


def music_mix(n):
    stems, _ = make_music(n)
    env = {
        "clap": [(0, 0), (13.5, 0), (15.5, 0.85), (36, 0.9), (72, 0.9), (76, 0.5), (84, 0)],
        "shaker": [(0, 0), (19, 0), (22, 0.55), (72, 0.6), (84, 0), (90, 0)],
        "bass": [(0, 0), (6.5, 0), (8.5, 0.8), (36, 1.0), (72, 0.9), (82, 0.55), (88, 0), (90, 0)],
        "synth": [(0, 0.8), (72, 0.8), (82, 0.5), (88, 0), (90, 0)],
        "guitar": [(0, 0), (22, 0), (26, 0.45), (72, 0.45), (80, 0), (90, 0)],
    }
    gain = {"clap": 0.7, "shaker": 0.45, "bass": 0.8, "synth": 0.5, "guitar": 0.55}
    mix = np.zeros((n, 2), dtype=np.float32)
    for k, x in stems.items():
        mix += x[:n] * env_curve(env[k], n).astype(np.float32) * gain[k]
    return mix


def narration(n):
    user = os.path.join(HERE, "narasi-vokal.wav")
    if os.path.exists(user):
        return decode(user)[:n], "rekaman pemilik (narasi-vokal.wav)"
    lines = [
        (0.6, "Mikrofon, musik, dan aplikasi panggilan, semuanya perlu jalurnya sendiri."),
        (7.6, "Kenalkan AudioHub, routing audio profesional untuk Mac."),
        (14.9, "Pasang lewat installer, masukkan kata sandi, lalu driver AVHub ikut terpasang."),
        (24.9, "Pilih perangkat fisik dan perangkat virtual, lalu tekan Buka dan Mulai."),
        (36.9, "Geser gain untuk mengatur level tiap input. Pan menempatkan suara di kiri atau kanan."),
        (50.5, "Klik kotak persimpangan untuk menghubungkan input ke output. Klik kanan untuk mengatur send."),
        (62.5, "Setiap input punya gate, EQ, kompresor, dan delay. Setiap output punya limiter."),
        (72.8, "Simpan konfigurasi sebagai preset, atau mulai dari template untuk podcast, streaming, rapat online, atau rekaman."),
        (82.6, "AudioHub. Atur audio Mac Anda dengan lebih mudah."),
    ]
    buf = np.zeros((n, 2), dtype=np.float32)
    with tempfile.TemporaryDirectory(dir=SCRATCH) as tmp:
        for k, (t0, s) in enumerate(lines):
            wav_path = os.path.join(tmp, f"l{k}.wav")
            subprocess.run(["say", "-v", "Damayanti", "-r", "172", "-o", wav_path,
                            "--file-format=WAVE", "--data-format=LEI16@48000", s], check=True)
            x = decode(wav_path)
            peak = np.abs(x).max() + 1e-9
            x = x / peak * 0.8
            dur = len(x) / SR
            if t0 + dur > 90:
                print(f"  peringatan: narasi {k + 1} melebihi video ({t0 + dur:.1f}s)")
            print(f"  narasi {k + 1}: mulai {t0:5.1f}s, durasi {dur:4.1f}s", flush=True)
            add(buf, x, t0, 1.0)
    return buf, "suara sementara macOS (Damayanti), ganti dengan rekaman Anda"


def sfx_track(n, S):
    buf = np.zeros((n, 2), dtype=np.float32)
    rng = np.random.default_rng(7)

    def ev(t, sig, g=1.0):
        add(buf, sig, t, g)

    def varied(sig, semis=0.0):
        return pitch(sig, semis + rng.uniform(-0.4, 0.4))

    # transisi (whoosh) di awal setiap scene
    for t in (7.0, 14.5, 24.5, 36.5, 50.0, 62.0, 72.5, 82.0):
        ev(t - 0.25, S["whoosh"], 0.5)

    # pemasangan
    ev(16.3, varied(S["click"]), 0.9)
    for k in range(8):
        ev(17.4 + k * 0.27, varied(S["tap"], 2), 0.42)
    ev(20.3, varied(S["click"]), 0.9)
    ev(22.7, S["gtr"], 0.55)

    # setting perangkat
    for t in (26.3, 28.0, 30.2, 32.0):
        ev(t, varied(S["click"]), 0.8)
    ev(32.2, S["gtr"], 0.45)
    ev(34.3, varied(S["tap"], 3), 0.5)
    ev(34.0, varied(S["click"]), 0.8)

    # slider
    for k, t in enumerate(np.arange(37.4, 41.1, 0.17)):
        ev(t, varied(S["ticks"][k % len(S["ticks"])]), 0.35)
    for t in np.arange(42.0, 45.1, 0.22):
        ev(t, varied(S["ticks"][int(t * 10) % len(S["ticks"])]), 0.3)
    ev(45.5, varied(S["click"]), 0.8)
    ev(46.1, varied(S["click"]), 0.8)
    ev(47.9, varied(S["click"]), 0.8)

    # routing
    for k, (t, semi) in enumerate(((51.0, 0), (51.5, 4), (52.0, 7), (52.5, 9), (53.0, 12))):
        ev(t, pitch(S["gtr"], semi), 0.35)
    ev(54.3, varied(S["tap"], 4), 0.6)
    ev(55.5, varied(S["click"]), 0.8)
    ev(56.8, varied(S["click"]), 0.8)
    ev(57.2, S["gtr"], 0.25)

    # efek
    ev(63.9, varied(S["click"]), 0.8)
    for t in np.arange(64.2, 67.2, 0.25):
        ev(t, varied(S["ticks"][int(t * 7) % len(S["ticks"])]), 0.3)
    ev(68.2, varied(S["click"]), 0.8)
    for t in np.arange(68.4, 70.4, 0.25):
        ev(t, varied(S["ticks"][int(t * 7) % len(S["ticks"])]), 0.3)
    ev(71.0, varied(S["tap"], -2), 0.6)
    ev(71.12, varied(S["tap"], -2), 0.6)
    ev(71.4, varied(S["click"]), 0.8)

    # preset dan template
    ev(73.6, varied(S["click"]), 0.8)
    ev(74.4, varied(S["click"]), 0.8)
    ev(75.6, varied(S["click"]), 0.8)
    for t in (76.6, 77.6, 78.6, 79.6):
        ev(t, varied(S["click"]), 0.8)
        ev(t + 0.02, pitch(S["gtr"], {76.6: 0, 77.6: 4, 78.6: 7, 79.6: 12}[t]), 0.3)

    # kartu akhir
    ev(82.2, S["gtr"], 0.6)
    ev(85.0, varied(S["click"]), 0.7)
    return buf


def master(x):
    x = np.tanh(x * 1.2) / np.tanh(1.2)
    fi = int(0.5 * SR)
    x[:fi] *= np.linspace(0, 1, fi)[:, None]
    fo = int(1.5 * SR)
    x[-fo:] *= np.linspace(1, 0, fo)[:, None]
    peak = np.max(np.abs(x)) + 1e-9
    return (x / peak * 0.89).astype(np.float32)


def duck_for(voice, n):
    """Musik turun sekitar 6 dB saat ada suara, dengan transisi halus."""
    act = (np.abs(voice).max(axis=1) > 0.01).astype(np.float32)
    k = np.ones(int(0.25 * SR), dtype=np.float32) / int(0.25 * SR)
    smooth = np.convolve(act, k, mode="same")
    return (1 - 0.5 * np.clip(smooth * 2, 0, 1))[:, None]


def build_audio(S, vocal):
    n = int(DUR * SR)
    music = music_mix(n)
    sfx = sfx_track(n, S)
    if not vocal:
        return master(music * 0.9 + sfx * 0.9), None
    voice, src = narration(n)
    duck = duck_for(voice, n)
    mix = music * duck * 0.9 + sfx * 0.9 + voice * 1.0
    return master(mix), src


def write_wav(x, path):
    import wave
    pcm = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


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


def render_video(out_path):
    total = int(round(DUR * FPS))
    cmd = [
        "ffmpeg", "-y", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
        "-c:v", "libx264", "-preset", "medium", "-crf", "17", "-pix_fmt", "yuv420p",
        "-movflags", "+faststart", out_path,
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    for i in range(total):
        proc.stdin.write(frame_at(i / FPS).tobytes())
        if i % (FPS * 10) == 0:
            print(f"  gambar {i / FPS:5.1f}s / {DUR:.0f}s", flush=True)
    proc.stdin.close()
    if proc.wait() != 0:
        raise SystemExit("ffmpeg gagal saat render gambar")


def mux(video, wav, out):
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-i", video, "-i", wav,
           "-map", "0:v", "-map", "1:a", "-c:v", "copy", "-c:a", "aac", "-b:a", "192k",
           "-t", str(DUR), "-movflags", "+faststart", out]
    subprocess.run(cmd, check=True)


def main(argv):
    global BG_IMG
    os.makedirs(SCRATCH, exist_ok=True)
    BG_IMG = make_bg()
    if len(argv) >= 2 and argv[1] == "still":
        T = float(argv[2])
        path = os.path.join(SCRATCH, f"konsep-{T:05.1f}.png")
        frame_at(T).save(path)
        print(path)
        return
    video_only = os.path.join(SCRATCH, "konsep-video.mp4")
    audio_only = len(argv) >= 2 and argv[1] == "audio"
    if audio_only:
        # hanya audio: pakai gambar yang sudah dirender, misalnya setelah narasi-vokal.wav diganti
        if not os.path.exists(video_only):
            raise SystemExit("Gambar belum dirender. Jalankan tanpa argumen dulu.")
    else:
        print("Render gambar...", flush=True)
        render_video(video_only)
    print("Render audio...", flush=True)
    S = build_samples()
    tmp_wav = os.path.join(SCRATCH, "konsep-audio.wav")
    for vocal, name in ((True, "AudioHub-Konsep-dengan-vokal.mp4"), (False, "AudioHub-Konsep-tanpa-vokal.mp4")):
        audio, src = build_audio(S, vocal)
        write_wav(audio, tmp_wav)
        out = os.path.join(HERE, name)
        mux(video_only, tmp_wav, out)
        print(f"selesai: {out}" + (f" (narasi: {src})" if src else ""), flush=True)


if __name__ == "__main__":
    main(sys.argv)
