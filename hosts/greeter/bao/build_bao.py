"""Build the bao's boing and the Telegram QR card.

The kiosk is hosts/greeter/bao/bao-3d.c. This file only writes
the wav and the QR png. It does not open a display.
"""

from __future__ import annotations

import math
import os
import struct
import sys
import wave
from pathlib import Path

TELEGRAM = "https://t.me/dimsumlabs"


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


def telegram_matrix() -> list:
    import segno

    code = segno.make(TELEGRAM, error="q")
    return quiet_zone([list(row) for row in code.matrix])


def render_qr(pygame: object, matrix: list, side: int) -> object:
    count = len(matrix)
    cell = max(1, side // count)
    used = cell * count
    pad = (side - used) // 2
    card = pygame.Surface((side, side), pygame.SRCALPHA)
    radius = max(8, side // 18)
    pygame.draw.rect(
        card,
        (255, 248, 244),
        card.get_rect(),
        border_radius=radius,
    )
    dark = (26, 18, 16)
    dot = max(0, cell // 5)
    for row_i, row in enumerate(matrix):
        for col_i, module in enumerate(row):
            if module == 0:
                continue
            pygame.draw.rect(
                card,
                dark,
                (
                    pad + col_i * cell,
                    pad + row_i * cell,
                    cell,
                    cell,
                ),
                border_radius=dot,
            )
    return card


def write_qr(path: Path) -> None:
    import pygame

    os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
    pygame.init()
    card = render_qr(pygame, telegram_matrix(), 1024)
    pygame.image.save(card, str(path))


if __name__ == "__main__":
    if "--write-boing" in sys.argv:
        write_boing(Path(sys.argv[-1]))
    elif "--write-qr" in sys.argv:
        write_qr(Path(sys.argv[-1]))
    else:
        sys.stderr.write(
            "use --write-boing or --write-qr\n",
        )
        raise SystemExit(2)
