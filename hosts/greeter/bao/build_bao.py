"""Build the bao's boing, QR cards, and stacked Dim Sum Labs logo.

The kiosk is hosts/greeter/bao/bao-3d.c. This file writes static
wav and png assets at build time and does not open a display.
The stacked DIM / SUM / LABS logo uses the VAG Rounded Std Bold
glyph outlines from dimsumlabs-graphics circuit_one.ai.
"""

from __future__ import annotations

import math
import os
import re
import struct
import sys
import wave
from pathlib import Path

MEETUP = "https://www.meetup.com/dimsumlabs/"
TELEGRAM = "https://t.me/dimsumlabs"
LOGO_LINES = ("DIM", "SUM", "LABS")
INK = (255, 248, 244)
DARK = (26, 18, 16)

# Exact glyph outlines (x, y in circuit_one.ai wordmark space).
LOGO_ROWS: tuple[tuple[str, ...], ...] = (
    (
        "M40.4,267.2c0-0.9,0.6-1.5,1.5-1.5h3.3"
        "c4.5,0,7.1,2.9,7.1,7.3c0,4.2-2.7,6.9-6.9,6.9H42"
        "c-0.7,0-1.6-0.4-1.6-1.5V267.2z"
        "M43.4,277.1h2c2.6,0,3.8-1.9,3.8-4.3"
        "c0-2.6-1.2-4.4-4-4.4h-1.8V277.1z",
        "M53.8,267.1c0-1,0.6-1.6,1.5-1.6c0.9,0,1.5,0.6,1.5,1.6"
        "v11.4c0,1-0.6,1.6-1.5,1.6c-0.9,0-1.5-0.6-1.5-1.6V267.1z",
        "M60,267.1c0.1-0.7,0.9-1.5,2-1.5c1,0,1.8,0.7,2,1.4"
        "l2.4,7.9h0l2.4-7.9c0.2-0.7,1-1.4,2-1.4"
        "c1.1,0,1.8,0.8,2,1.5l1.9,11.2c0,0.1,0,0.2,0,0.3"
        "c0,0.9-0.6,1.4-1.4,1.4c-1,0-1.4-0.5-1.6-1.4l-1.2-8.2h0"
        "l-2.4,8.4c-0.1,0.5-0.5,1.2-1.6,1.2"
        "c-1.1,0-1.5-0.7-1.6-1.2l-2.4-8.4h0l-1.2,8.2"
        "c-0.1,0.9-0.5,1.4-1.6,1.4c-0.8,0-1.4-0.6-1.4-1.4"
        "c0-0.1,0-0.2,0-0.3L60,267.1z",
    ),
    (
        "M84.5,267.4c0,0.8-0.5,1.5-1.3,1.5"
        "c-0.8,0-1.5-0.6-2.7-0.6c-0.9,0-1.6,0.5-1.6,1.3"
        "c0,2.1,6.7,0.7,6.7,5.9c0,2.9-2.4,4.7-5.1,4.7"
        "c-1.5,0-4.9-0.4-4.9-2.2c0-0.8,0.5-1.4,1.3-1.4"
        "c0.9,0,2.1,0.8,3.4,0.8c1.3,0,2-0.7,2-1.7"
        "c0-2.4-6.7-0.9-6.7-5.6c0-2.8,2.3-4.6,5-4.6"
        "C81.7,265.4,84.5,265.8,84.5,267.4z",
        "M86.7,267.1c0-1,0.6-1.6,1.5-1.6c0.9,0,1.5,0.6,1.5,1.6v7"
        "c0,1.8,1.2,3.3,3.1,3.3c1.8,0,3.1-1.5,3.1-3.3v-7"
        "c0-1,0.6-1.6,1.5-1.6c0.9,0,1.5,0.6,1.5,1.6v7.2"
        "c0,3.4-2.8,5.9-6.1,5.9c-3.3,0-6.1-2.5-6.1-5.9V267.1z",
        "M101.5,267.1c0.1-0.7,0.9-1.5,2-1.5c1,0,1.8,0.7,2,1.4"
        "l2.4,7.9h0l2.4-7.9c0.2-0.7,1-1.4,2-1.4"
        "c1.1,0,1.8,0.8,2,1.5l1.9,11.2c0,0.1,0,0.2,0,0.3"
        "c0,0.9-0.6,1.4-1.4,1.4c-1,0-1.4-0.5-1.6-1.4l-1.2-8.2h0"
        "l-2.4,8.4c-0.1,0.5-0.5,1.2-1.6,1.2"
        "c-1.1,0-1.5-0.7-1.6-1.2l-2.4-8.4h0l-1.2,8.2"
        "c-0.1,0.9-0.5,1.4-1.6,1.4c-0.8,0-1.4-0.6-1.4-1.4"
        "c0-0.1,0-0.2,0-0.3L101.5,267.1z",
    ),
    (
        "M116.7,267.1c0-1,0.6-1.6,1.5-1.6c0.9,0,1.5,0.6,1.5,1.6"
        "v10.1h3.6c1,0,1.5,0.8,1.4,1.4c0,0.7-0.6,1.3-1.4,1.3h-5"
        "c-1,0-1.6-0.6-1.6-1.6V267.1z",
        "M129.6,267.1c0.3-0.9,1.1-1.7,2.1-1.7"
        "c1.1,0,1.8,0.7,2.1,1.7l3.9,10.9c0.1,0.3,0.2,0.6,0.2,0.7"
        "c0,0.8-0.6,1.3-1.4,1.3c-0.8,0-1.3-0.4-1.5-1l-0.6-1.9"
        "H129l-0.6,1.9c-0.2,0.6-0.6,1.1-1.4,1.1"
        "c-0.8,0-1.5-0.6-1.5-1.4c0-0.3,0.1-0.6,0.1-0.7"
        "L129.6,267.1zM129.8,274.3h3.8l-1.9-5.8h0L129.8,274.3z",
        "M139.2,267.1c0-1,0.7-1.5,1.6-1.5h3.8"
        "c2.1,0,3.8,1.4,3.8,3.5c0,1.4-0.6,2.3-1.9,2.9v0"
        "c1.7,0.2,3,1.7,3,3.4c0,2.8-1.9,4.4-4.7,4.4h-4.1"
        "c-0.9,0-1.6-0.6-1.6-1.5V267.1z"
        "M142.2,271.3h1.6c1,0,1.6-0.6,1.6-1.6"
        "c0-0.9-0.7-1.4-1.6-1.4h-1.6V271.3z"
        "M142.2,277.3h2.4c1.1,0,1.9-0.6,1.9-1.7"
        "c0-0.9-0.7-1.6-2.1-1.6h-2.1V277.3z",
        "M159.2,267.4c0,0.8-0.5,1.5-1.3,1.5s-1.5-0.6-2.7-0.6"
        "c-0.9,0-1.6,0.5-1.6,1.3c0,2.1,6.7,0.7,6.7,5.9"
        "c0,2.9-2.4,4.7-5.1,4.7c-1.5,0-4.9-0.4-4.9-2.2"
        "c0-0.8,0.5-1.4,1.3-1.4c0.9,0,2.1,0.8,3.4,0.8"
        "c1.3,0,2-0.7,2-1.7c0-2.4-6.7-0.9-6.7-5.6"
        "c0-2.8,2.3-4.6,5-4.6C156.4,265.4,159.2,265.8,159.2,267.4z",
    ),
)

_TOKEN_RE = re.compile(
    r"[A-Za-z]|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"
)


def write_boing(path: Path) -> None:
    rate = 22050
    seconds = 0.42
    count = int(rate * seconds)
    phase = 0.0
    samples = bytearray()
    for i in range(count):
        t = i / rate
        freq = 540.0 * (0.22 ** (t / seconds))
        phase += 2.0 * math.pi * freq / rate
        env = math.exp(-3.2 * t)
        env *= math.sin(math.pi * min(t / seconds, 1.0))
        value = max(-1.0, min(1.0, math.sin(phase) * env))
        samples += struct.pack("<h", int(value * 30000))
    with wave.open(str(path), "w") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(rate)
        handle.writeframes(samples)


def quiet_zone(rows: list, border: int = 4) -> list:
    if len(rows) == 0:
        return []
    width = len(rows[0]) + border * 2
    padded = []
    for _ in range(border):
        padded.append([0] * width)
    for row in rows:
        padded.append([0] * border + list(row) + [0] * border)
    for _ in range(border):
        padded.append([0] * width)
    return padded


def qr_matrix(url: str) -> list:
    import segno

    code = segno.make(url, error="q")
    return quiet_zone([list(row) for row in code.matrix])


def meetup_matrix() -> list:
    return qr_matrix(MEETUP)


def telegram_matrix() -> list:
    return qr_matrix(TELEGRAM)


def render_qr(pygame: object, matrix: list, side: int) -> object:
    count = len(matrix)
    cell = max(1, side // count)
    used = cell * count
    pad = (side - used) // 2
    card = pygame.Surface((side, side), pygame.SRCALPHA)
    radius = max(8, side // 9)
    pygame.draw.rect(
        card,
        INK,
        card.get_rect(),
        border_radius=radius,
    )
    dot = max(0, cell // 5)
    for row_i, row in enumerate(matrix):
        for col_i, module in enumerate(row):
            if module == 0:
                continue
            pygame.draw.rect(
                card,
                DARK,
                (
                    pad + col_i * cell,
                    pad + row_i * cell,
                    cell,
                    cell,
                ),
                border_radius=dot,
            )
    return card


def _cubic(
    p0: tuple[float, float],
    p1: tuple[float, float],
    p2: tuple[float, float],
    p3: tuple[float, float],
    steps: int = 14,
) -> list[tuple[float, float]]:
    pts: list[tuple[float, float]] = []
    for k in range(1, steps + 1):
        t = k / steps
        u = 1.0 - t
        x = (
            u * u * u * p0[0]
            + 3 * u * u * t * p1[0]
            + 3 * u * t * t * p2[0]
            + t * t * t * p3[0]
        )
        y = (
            u * u * u * p0[1]
            + 3 * u * u * t * p1[1]
            + 3 * u * t * t * p2[1]
            + t * t * t * p3[1]
        )
        pts.append((x, y))
    return pts


def svg_polys(
    path_d: str,
) -> list[list[tuple[float, float]]]:
    tokens = _TOKEN_RE.findall(path_d)
    polys: list[list[tuple[float, float]]] = []
    cur: list[tuple[float, float]] = []
    i = 0
    cmd = ""
    x = y = sx = sy = 0.0
    last_c2: tuple[float, float] | None = None
    while i < len(tokens):
        if tokens[i][0].isalpha():
            cmd = tokens[i]
            i += 1
        if cmd in ("Z", "z"):
            if len(cur) >= 3:
                polys.append(cur)
            cur = []
            x, y = sx, sy
            last_c2 = None
            cmd = ""
            continue
        if cmd in ("M", "m"):
            nx, ny = float(tokens[i]), float(tokens[i + 1])
            i += 2
            x, y = (x + nx, y + ny) if cmd == "m" else (nx, ny)
            sx, sy = x, y
            if len(cur) >= 3:
                polys.append(cur)
            cur = [(x, y)]
            cmd = "l" if cmd == "m" else "L"
            last_c2 = None
        elif cmd in ("L", "l"):
            nx, ny = float(tokens[i]), float(tokens[i + 1])
            i += 2
            x, y = (x + nx, y + ny) if cmd == "l" else (nx, ny)
            cur.append((x, y))
            last_c2 = None
        elif cmd in ("H", "h"):
            nx = float(tokens[i])
            i += 1
            x = x + nx if cmd == "h" else nx
            cur.append((x, y))
            last_c2 = None
        elif cmd in ("V", "v"):
            ny = float(tokens[i])
            i += 1
            y = y + ny if cmd == "v" else ny
            cur.append((x, y))
            last_c2 = None
        elif cmd in ("C", "c"):
            vals = [float(tokens[i + k]) for k in range(6)]
            i += 6
            if cmd == "c":
                p1 = (x + vals[0], y + vals[1])
                p2 = (x + vals[2], y + vals[3])
                p3 = (x + vals[4], y + vals[5])
            else:
                p1 = (vals[0], vals[1])
                p2 = (vals[2], vals[3])
                p3 = (vals[4], vals[5])
            cur.extend(_cubic((x, y), p1, p2, p3))
            x, y = p3
            last_c2 = p2
        elif cmd in ("S", "s"):
            vals = [float(tokens[i + k]) for k in range(4)]
            i += 4
            p1 = (
                (2 * x - last_c2[0], 2 * y - last_c2[1])
                if last_c2 is not None
                else (x, y)
            )
            if cmd == "s":
                p2 = (x + vals[0], y + vals[1])
                p3 = (x + vals[2], y + vals[3])
            else:
                p2 = (vals[0], vals[1])
                p3 = (vals[2], vals[3])
            cur.extend(_cubic((x, y), p1, p2, p3))
            x, y = p3
            last_c2 = p2
        else:
            raise ValueError(f"unsupported SVG cmd: {cmd}")
    if len(cur) >= 3:
        polys.append(cur)
    return polys


def stacked_logo_geometry(
    side: int,
) -> tuple[int, int, int, int, list[list[list[tuple[float, float]]]]]:
    rows_raw = [[svg_polys(g) for g in row] for row in LOGO_ROWS]
    row_bounds = []
    for row in rows_raw:
        xs = [pt[0] for g in row for sub in g for pt in sub]
        ys = [pt[1] for g in row for sub in g for pt in sub]
        row_bounds.append((min(xs), min(ys), max(xs), max(ys)))
    cap_h = max(b[3] - b[1] for b in row_bounds)
    gap_u = cap_h * 0.24
    total_u_h = 3 * cap_h + 2 * gap_u
    max_u_w = max(b[2] - b[0] for b in row_bounds)
    # Match circuit_one.ai frame proportions around stacked glyphs.
    pad_u = cap_h * 0.42
    stroke_u = cap_h * 0.34
    box_u_h = total_u_h + 2 * (pad_u + stroke_u)
    box_u_w = max_u_w + 2 * (pad_u + stroke_u)
    scale = side / box_u_h
    width = round(box_u_w * scale)
    stroke = max(4, round(stroke_u * scale))
    radius = max(8, round((stroke_u + cap_h * 0.26) * scale))
    top_u = stroke_u + pad_u
    placed: list[list[list[tuple[float, float]]]] = []
    for idx, (row, (x0, y0, x1, _)) in enumerate(
        zip(rows_raw, row_bounds)
    ):
        rw = x1 - x0
        ox = (box_u_w - rw) * 0.5 - x0
        oy = top_u + idx * (cap_h + gap_u) - y0
        placed_row = []
        for glyph in row:
            placed_glyph = [
                [((px + ox) * scale, (py + oy) * scale) for px, py in sub]
                for sub in glyph
            ]
            placed_row.append(placed_glyph)
        placed.append(placed_row)
    return width, side, stroke, radius, placed


def render_logo(pygame: object, side: int) -> object:
    ss = 2
    w2, h2, stroke2, radius2, placed = stacked_logo_geometry(
        side * ss
    )
    hi = pygame.Surface((w2, h2), pygame.SRCALPHA)
    pygame.draw.rect(
        hi,
        (*INK, 255),
        hi.get_rect(),
        border_radius=radius2,
    )
    pygame.draw.rect(
        hi,
        (0, 0, 0, 0),
        (stroke2, stroke2, w2 - 2 * stroke2, h2 - 2 * stroke2),
        border_radius=max(0, radius2 - stroke2),
    )
    for row in placed:
        for glyph in row:
            pygame.draw.polygon(hi, (*INK, 255), glyph[0])
            for hole in glyph[1:]:
                pygame.draw.polygon(hi, (0, 0, 0, 0), hole)
    return pygame.transform.smoothscale(hi, (w2 // ss, side))


def write_qr(path: Path, url: str = MEETUP) -> None:
    import pygame

    os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    pygame.init()
    card = render_qr(pygame, qr_matrix(url), 1024)
    pygame.image.save(card, str(path))


def write_logo(path: Path) -> None:
    import pygame

    os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    pygame.init()
    card = render_logo(pygame, 1024)
    pygame.image.save(card, str(path))


if __name__ == "__main__":
    if "--write-boing" in sys.argv:
        write_boing(Path(sys.argv[-1]))
    elif "--write-qr" in sys.argv:
        write_qr(Path(sys.argv[-1]), MEETUP)
    elif "--write-telegram-qr" in sys.argv:
        write_qr(Path(sys.argv[-1]), TELEGRAM)
    elif "--write-logo" in sys.argv:
        write_logo(Path(sys.argv[-1]))
    else:
        sys.stderr.write(
            "use --write-boing, --write-qr,"
            " --write-telegram-qr, or --write-logo\n",
        )
        raise SystemExit(2)
