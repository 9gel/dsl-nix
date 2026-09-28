"""Drifting Dim Sum Labs bao. A tap makes it jump.

Run `python3 bao.py --check` with no display and no pygame.
"""

from __future__ import annotations

import math
import os
import sys
from dataclasses import dataclass
from pathlib import Path

CRUISE = 170.0
MAX_SPEED = 1700.0
DAMP = 1.35
JUMP_UP = 1400.0
MISS_JUMP = 780.0
POKE_HIT = 700.0
POKE_MISS = 280.0
SPIN = 8.0
HOP_TIME = 1.15
GRAVITY = 1600.0
DT_MAX = 1.0 / 20.0
LOGO_FRACTION = 0.40
SPIN_DAMP = 3.0


@dataclass
class Body:
    x: float
    y: float
    vx: float
    vy: float
    w: float
    h: float
    spin: float
    hop: float


def logo_span(
    screen_w: float,
    screen_h: float,
    image_w: float,
    image_h: float,
) -> tuple[int, int]:
    longest = max(image_w, image_h)
    if longest <= 0:
        return 1, 1
    target = min(screen_w, screen_h) * LOGO_FRACTION
    scale = target / longest
    return (
        max(1, int(image_w * scale)),
        max(1, int(image_h * scale)),
    )


def clamp_speed(
    vx: float,
    vy: float,
    limit: float = MAX_SPEED,
) -> tuple[float, float]:
    speed = math.hypot(vx, vy)
    if speed <= limit or speed == 0:
        return vx, vy
    scale = limit / speed
    return vx * scale, vy * scale


def damp(vx: float, vy: float, dt: float) -> tuple[float, float]:
    speed = math.hypot(vx, vy)
    if speed < 1:
        return CRUISE, 0.0
    if speed > CRUISE:
        excess = speed - CRUISE
        speed = CRUISE + excess * math.exp(-DAMP * dt)
    else:
        gain = 1 - math.exp(-DAMP * dt)
        speed = speed + (CRUISE - speed) * gain
    scale = speed / math.hypot(vx, vy)
    return vx * scale, vy * scale


def contains(body: Body, tx: float, ty: float) -> bool:
    return (
        abs(tx - body.x) <= body.w / 2
        and abs(ty - body.y) <= body.h / 2
    )


def poke(body: Body, tx: float, ty: float) -> bool:
    dx = body.x - tx
    dy = body.y - ty
    dist = math.hypot(dx, dy)
    if dist < 1:
        dist = 1
    hit = contains(body, tx, ty)
    shove = POKE_HIT if hit else POKE_MISS
    body.vx += shove * dx / dist
    body.vy = -JUMP_UP if hit else -MISS_JUMP
    body.hop = HOP_TIME if hit else HOP_TIME * 0.6
    if dx < 0:
        body.spin -= SPIN
    else:
        body.spin += SPIN
    body.vx, body.vy = clamp_speed(body.vx, body.vy)
    return hit


def bounce(body: Body, sw: float, sh: float) -> bool:
    half_w = body.w / 2
    half_h = body.h / 2
    hit = False
    if body.x < half_w:
        body.x = half_w
        body.vx = abs(body.vx)
        hit = True
    elif body.x > sw - half_w:
        body.x = sw - half_w
        body.vx = -abs(body.vx)
        hit = True
    if body.y < half_h:
        body.y = half_h
        body.vy = abs(body.vy)
        hit = True
    elif body.y > sh - half_h:
        body.y = sh - half_h
        body.vy = -abs(body.vy)
        hit = True
    return hit


def step(body: Body, sw: float, sh: float, dt: float) -> bool:
    dt = min(max(dt, 0.0), DT_MAX)
    if body.hop > 0:
        body.vy += GRAVITY * dt
        body.hop = max(0.0, body.hop - dt)
    body.x += body.vx * dt
    body.y += body.vy * dt
    hit = bounce(body, sw, sh)
    if body.hop == 0:
        body.vx, body.vy = damp(body.vx, body.vy, dt)
    body.vx, body.vy = clamp_speed(body.vx, body.vy)
    body.spin *= math.exp(-SPIN_DAMP * dt)
    return hit


def check() -> None:
    assert logo_span(1000, 800, 85, 85) == (320, 320)
    assert logo_span(100, 100, 0, 0) == (1, 1)

    vx, vy = clamp_speed(3000, 4000)
    assert math.hypot(vx, vy) <= MAX_SPEED + 1e-6

    vx, vy = damp(1000, 0, 1)
    assert CRUISE < vx < 1000
    assert vy == 0
    vx, vy = damp(10, 0, 1)
    assert 10 < vx <= CRUISE

    body = Body(
        x=5, y=100, vx=-200, vy=0, w=20, h=20, spin=0, hop=0,
    )
    assert step(body, 200, 200, 0.016)
    assert body.x == 10
    assert body.vx > 0

    body = Body(
        x=100, y=195, vx=0, vy=400, w=20, h=20, spin=0, hop=0,
    )
    step(body, 200, 200, 0.016)
    assert body.y == 190
    assert body.vy < 0

    body = Body(
        x=15, y=100, vx=-MAX_SPEED, vy=0,
        w=20, h=20, spin=0, hop=0,
    )
    step(body, 200, 200, 10)
    assert 10 <= body.x <= 190

    body = Body(
        x=100, y=100, vx=0, vy=0, w=40, h=40, spin=0, hop=0,
    )
    assert poke(body, 115, 100)
    assert body.vx < 0
    assert body.vy == -JUMP_UP
    assert body.spin < 0
    assert body.hop == HOP_TIME
    body.x = 1000
    body.y = 1000
    vy_at_poke = body.vy
    step(body, 4000, 4000, 0.1)
    assert body.y < 1000
    assert vy_at_poke < body.vy < 0

    missed = Body(
        x=100, y=100, vx=0, vy=0, w=40, h=40, spin=0, hop=0,
    )
    assert poke(missed, 0, 0) is False
    assert missed.vy == -MISS_JUMP
    assert missed.vy > -JUMP_UP

    spinning = Body(
        x=200, y=200, vx=30, vy=0, w=20, h=20, spin=4, hop=0,
    )
    step(spinning, 800, 800, 0.05)
    assert spinning.spin < 4
    print("ok")


def main() -> None:
    # pygame is the display path. --check stays importable
    # without SDL.
    import pygame

    raw = os.environ.get("DSL_BAO_LOGO", "")
    if raw == "":
        sys.stderr.write("DSL_BAO_LOGO is not set\n")
        raise SystemExit(1)
    logo_path = Path(raw)
    if not logo_path.is_file():
        sys.stderr.write("DSL_BAO_LOGO is missing\n")
        raise SystemExit(1)

    os.environ["SDL_VIDEO_WAYLAND_WMCLASS"] = "dsl-bao"
    os.environ["SDL_VIDEO_X11_WMCLASS"] = "dsl-bao"
    pygame.init()
    pygame.mouse.set_visible(False)
    screen = pygame.display.set_mode(
        (0, 0),
        pygame.FULLSCREEN | pygame.NOFRAME,
        vsync=1,
    )
    pygame.display.set_caption("dsl-bao")
    screen_w, screen_h = screen.get_size()
    image = pygame.image.load(str(logo_path)).convert_alpha()
    span = logo_span(
        screen_w, screen_h, image.get_width(), image.get_height(),
    )
    base = pygame.transform.smoothscale(image, span)
    body = Body(
        x=screen_w / 2,
        y=screen_h / 2,
        vx=CRUISE * 0.72,
        vy=CRUISE * 0.70,
        w=float(span[0]),
        h=float(span[1]),
        spin=0.0,
        hop=0.0,
    )
    squash = 1.0
    last_tap = 0.0
    clock = pygame.time.Clock()
    background = (26, 18, 16)

    while True:
        dt = clock.tick(60) / 1000.0
        now = pygame.time.get_ticks() / 1000.0
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                raise SystemExit(0)
            point = tap_point(event, screen_w, screen_h)
            if point is None or now - last_tap < 0.12:
                continue
            last_tap = now
            poke(body, point[0], point[1])
            squash = 0.62
        if step(body, screen_w, screen_h, dt):
            if squash > 0.85:
                squash = 0.78
        squash = squash + (1 - squash) * min(1.0, dt * 8)
        screen.fill(background)
        draw_shadow(pygame, screen, body)
        blit_logo(pygame, screen, base, body, squash, now)
        pygame.display.flip()


def tap_point(
    event: object,
    screen_w: int,
    screen_h: int,
) -> tuple[float, float] | None:
    import pygame

    if event.type == pygame.FINGERDOWN:
        return event.x * screen_w, event.y * screen_h
    if event.type == pygame.MOUSEBUTTONDOWN and event.button == 1:
        return float(event.pos[0]), float(event.pos[1])
    return None


def draw_shadow(pygame: object, screen: object, body: Body) -> None:
    lift = 0.0
    if body.hop > 0:
        lift = min(1.0, body.hop / HOP_TIME)
    width = max(8, int(body.w * (0.62 - 0.22 * lift)))
    height = max(4, int(body.h * (0.14 - 0.05 * lift)))
    alpha = int(90 * (1 - 0.65 * lift))
    surf = pygame.Surface((width, height), pygame.SRCALPHA)
    pygame.draw.ellipse(
        surf, (0, 0, 0, alpha), surf.get_rect(),
    )
    rect = surf.get_rect()
    rect.center = (
        int(body.x),
        int(body.y + body.h * 0.42 + lift * 36),
    )
    screen.blit(surf, rect)


def blit_logo(
    pygame: object,
    screen: object,
    base: object,
    body: Body,
    squash: float,
    now: float,
) -> None:
    lift = 0.0
    if body.hop > 0:
        lift = min(1.0, body.hop / HOP_TIME)
    bob = math.sin(now * 2.1) * 7 * (1 - lift)
    center = (int(body.x), int(body.y + bob))
    if squash > 0.98 and abs(body.spin) < 0.02:
        rect = base.get_rect()
        rect.center = center
        screen.blit(base, rect)
        return
    width = max(1, int(body.w * (1 + (1 - squash) * 0.5)))
    height = max(1, int(body.h * squash))
    frame = pygame.transform.smoothscale(base, (width, height))
    if abs(body.spin) >= 0.02:
        frame = pygame.transform.rotate(
            frame, -math.degrees(body.spin),
        )
    rect = frame.get_rect()
    rect.center = center
    screen.blit(frame, rect)


if __name__ == "__main__":
    if "--check" in sys.argv:
        check()
    else:
        main()
