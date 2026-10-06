/* Dim Sum Labs 3D bao on the Pi display.

   The bao chip from model_for_dslan.blend paces the corner of a
   red room. A tap on it makes it jump, spin and yell. A wave
   on the webcam makes it face the camera and look up, with an
   exclamation by its head. The screen turn, touch, QR,
   captions and sounds follow bao.c. --check needs no display.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DSL_BAO_HEADLESS
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

#ifndef DSL_SCREEN_TURN
#define DSL_SCREEN_TURN 1
#endif
#ifndef DSL_BAO_MODELS
#define DSL_BAO_MODELS ""
#endif
#ifndef DSL_BAO_FONT
#define DSL_BAO_FONT ""
#endif
#ifndef DSL_BAO_QR
#define DSL_BAO_QR ""
#endif
#ifndef DSL_BAO_SOUNDS
#define DSL_BAO_SOUNDS ""
#endif

static const double k_pi = 3.14159265358979323846;
/* Room units are Blender units. The bao is about 9 wide. The
   corner is the origin; walls stand on x = 0 and z = 0. The bao
   wanders EDGE_LO..EDGE_HI on both floor axes. It steers off
   the walls there, but only CORNER..EDGE_HI is hard: it walks
   in near the corner, to PEE_X, PEE_Z, to pee. */
static const double EDGE_LO = 9.0;
static const double EDGE_HI = 50.0;
static const double CORNER = 6.5;
static const double PEE_X = 8.0;
static const double PEE_Z = 10.0;
static const double AVOID = 8.0;
static const double WALK = 7.0;
static const double RUN = 26.0;
static const double RELAX = 1.2;
static const double STOP = 4.0;
static const double TURN_MAX = 0.9;
static const double STRIDE = 3.2;
static const double JUMP_UP = 34.0;
static const double MISS_JUMP = 12.0;
static const double GRAVITY = 90.0;
static const double SPIN_EASE = 5.0;
static const double POSE_EASE = 4.0;
static const double DT_MAX = 1.0 / 20.0;
/* Odds per wander decision, one every 1.5 to 4.5 s. */
static const double P_LOOK = 0.12;
static const double P_PEE = 0.04;
static const double LOOK_TIME = 4.5;
static const double PEE_TIME = 4.5;
static const double SEEK_MAX = 20.0;
/* The camera looks down the room diagonal from CAM_X, CAM_Z.
   To look up on its own, the bao turns side-on to that view
   (either way), so the lean shows. A wave aims it at the
   camera, then plays the same lean. To pee it faces the
   viewer, rear to the corner: its near hind pins (local +Z)
   then lift out toward the left wall, in plain view, like a
   dog's leg at a post. */
static const double FACE_VIEW = 0.785398163397448;
static const double FACE_PEE = FACE_VIEW;
static const double CAM_X = 84;
static const double CAM_Z = 84;
static const double NOTICE_LOOK = 3.0;

enum { WALKING, LOOKING, SEEKING, PEEING, NOTICING };

typedef struct {
    double x, z, heading, speed, turn, wander;
    double y, vy, spin, spin_to, phase;
    int mode;
    double act, look, lift, puddle, gait, aim, mark;
} Bao;

typedef struct {
    double x, y;
} Vec;

typedef struct {
    double x, y, w, h, rot;
} Place;

static double wrap_angle(double a)
{
    while (a > k_pi)
        a -= 2 * k_pi;
    while (a < -k_pi)
        a += 2 * k_pi;
    return a;
}

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static Bao bao_at(double x, double z, double heading)
{
    Bao b = {0};
    b.x = x;
    b.z = z;
    b.heading = heading;
    b.speed = WALK;
    b.wander = 10;
    return b;
}

static void set_mode(Bao *b, int mode)
{
    b->mode = mode;
    b->act = 0;
    if (mode == WALKING) {
        b->wander = 0.5;
        b->turn = 0;
    }
}

/* Turns toward want at no more than rate. Returns |rad/s|. */
static double steer(Bao *b, double want, double rate, double dt)
{
    double off = wrap_angle(want - b->heading);
    double step = clampd(off, -rate * dt, rate * dt);
    b->heading = wrap_angle(b->heading + step);
    return dt > 0 ? fabs(step) / dt : 0;
}

/* Wandering: a mostly gentle turn held for a while, sometimes
   none. Near a wall, turn for the middle of the patch. */
static double wander(Bao *b, double dt, double r1, double r2)
{
    double mid = (EDGE_LO + EDGE_HI) / 2;
    double near = fmin(
        fmin(b->x - EDGE_LO, EDGE_HI - b->x),
        fmin(b->z - EDGE_LO, EDGE_HI - b->z));
    double u = r2 * 2 - 1;
    b->wander -= dt;
    if (b->wander <= 0) {
        if (r1 < P_LOOK) {
            set_mode(b, LOOKING);
            b->aim = FACE_VIEW + (r2 < 0.5 ? -k_pi / 2 : k_pi / 2);
            return 0;
        }
        if (r1 < P_LOOK + P_PEE) {
            set_mode(b, SEEKING);
            return 0;
        }
        b->turn = r1 < 0.4 ? 0 : u * u * u * TURN_MAX;
        b->wander = 1.5 + r2 * 3;
    }
    if (near < AVOID) {
        double want = atan2(mid - b->z, mid - b->x);
        return steer(b, want, TURN_MAX * 1.5, dt);
    }
    b->heading = wrap_angle(b->heading + b->turn * dt);
    return fabs(b->turn);
}

/* One tick. r1 and r2 are uniform in [0, 1) and drive the next
   wander decision: turn, stop and look up, or go pee in the
   corner. A wave is separate: notice(). */
static void walk(Bao *b, double dt, double r1, double r2)
{
    double speed_to = WALK, look_to = 0, lift_to = 0;
    double omega = 0, rate, ease, dist, want;
    dt = clampd(dt, 0, DT_MAX);
    b->act += dt;
    switch (b->mode) {
    case LOOKING:
        speed_to = 0;
        omega = steer(b, b->aim, TURN_MAX * 1.5, dt);
        look_to = omega < 0.01 && b->act < LOOK_TIME - 0.7 ? 1 : 0;
        if (b->act > LOOK_TIME)
            set_mode(b, WALKING);
        break;
    case NOTICING:
        /* Face the camera first. The look clock stays put
           until the turn is done, then the same tip-back
           plays. mark eases the exclamation in and out. */
        speed_to = 0;
        omega = steer(b, b->aim, TURN_MAX * 1.5, dt);
        if (omega >= 0.01)
            b->act = 0;
        look_to = b->act > 0.15 && b->act < NOTICE_LOOK ? 1 : 0;
        if (b->act > NOTICE_LOOK + 0.8)
            set_mode(b, WALKING);
        break;
    case SEEKING:
        /* Slow for turns, so it cannot orbit the spot. */
        dist = hypot(b->x - PEE_X, b->z - PEE_Z);
        want = atan2(PEE_Z - b->z, PEE_X - b->x);
        speed_to = fmin(WALK, 1 + dist)
            * fmax(0, cos(wrap_angle(want - b->heading)));
        omega = steer(b, want, TURN_MAX * 1.5, dt);
        if (dist < 0.8)
            set_mode(b, PEEING);
        else if (b->act > SEEK_MAX)
            set_mode(b, WALKING);
        break;
    case PEEING:
        speed_to = 0;
        omega = steer(b, FACE_PEE, TURN_MAX * 1.5, dt);
        lift_to = b->act > 1.0 && b->act < PEE_TIME - 0.6 ? 1 : 0;
        if (b->lift > 0.8)
            b->puddle = fmin(1, b->puddle + dt * 0.4);
        if (b->act > PEE_TIME)
            set_mode(b, WALKING);
        break;
    default:
        omega = wander(b, dt, r1, r2);
    }
    if (b->mode != PEEING)
        b->puddle = fmax(0, b->puddle - dt / 30);
    rate = speed_to < b->speed && speed_to < WALK ? STOP : RELAX;
    b->speed += (speed_to - b->speed) * (1 - exp(-rate * dt));
    b->x += cos(b->heading) * b->speed * dt;
    b->z += sin(b->heading) * b->speed * dt;
    b->x = clampd(b->x, CORNER, EDGE_HI);
    b->z = clampd(b->z, CORNER, EDGE_HI);
    ease = 1 - exp(-POSE_EASE * dt);
    b->look += (look_to - b->look) * ease;
    b->lift += (lift_to - b->lift) * ease;
    b->mark += ((b->mode == NOTICING ? 1.0 : 0.0) - b->mark) * ease;
    b->gait = clampd((b->speed + omega * 3) / WALK, 0, 1);
    if (b->y > 0 || b->vy > 0) {
        b->vy -= GRAVITY * dt;
        b->y += b->vy * dt;
        if (b->y <= 0) {
            b->y = 0;
            b->vy = 0;
        }
    } else {
        b->phase = fmod(
            b->phase + (b->speed + omega * 3) * dt / STRIDE * 2 * k_pi,
            2 * k_pi);
    }
    b->spin += (b->spin_to - b->spin) * (1 - exp(-SPIN_EASE * dt));
}

/* A hit jumps, spins once and bolts along flee, whatever the bao
   was up to. A miss hops. */
static void poke(Bao *b, int hit, double flee)
{
    if (hit) {
        set_mode(b, WALKING);
        b->vy = JUMP_UP;
        b->spin_to += flee > b->heading ? 2 * k_pi : -2 * k_pi;
        b->heading = wrap_angle(flee);
        b->speed = RUN;
        b->wander = 1.5;
    } else if (b->y == 0) {
        b->vy = MISS_JUMP;
    }
}

static double face_camera(const Bao *b)
{
    return atan2(CAM_Z - b->z, CAM_X - b->x);
}

static void notice(Bao *b)
{
    set_mode(b, NOTICING);
    b->aim = face_camera(b);
}

static int norm_turn(int turn)
{
    return turn == 3 ? 3 : 1;
}

static Vec logical_to_fb(int turn, int lw, int lh, double x, double y)
{
    Vec p;
    if (turn == 1) {
        p.x = y;
        p.y = (double)lw - x;
    } else {
        p.x = (double)lh - y;
        p.y = x;
    }
    return p;
}

static Vec fb_to_logical(int turn, int lw, int lh, double x, double y)
{
    Vec p;
    if (turn == 1) {
        p.x = (double)lw - y;
        p.y = x;
    } else {
        p.x = y;
        p.y = (double)lh - x;
    }
    return p;
}

/* Dest rect of the portrait texture on the landscape mode.
   Corners match logical_to_fb. check() locks that. */
static Place scene_place(int turn, int fw, int fh)
{
    if (turn == 1)
        return (Place){0, fh, fh, fw, -90};
    return (Place){fw, 0, fh, fw, 90};
}

static Vec place_point(Place pl, double u, double v)
{
    double rad = pl.rot * k_pi / 180.0;
    Vec o;
    o.x = pl.x + u * cos(rad) - v * sin(rad);
    o.y = pl.y + u * sin(rad) + v * cos(rad);
    return o;
}

static int fail(const char *msg)
{
    fprintf(stderr, "dsl-bao-3d: %s\n", msg);
    return 1;
}

static void run_for(Bao *b, double seconds, double r1, double r2)
{
    double t;
    for (t = 0; t < seconds; t += 1.0 / 60)
        walk(b, 1.0 / 60, r1, r2);
}

/* Camera-right (ux, uz) in the bao's xz. yaw is -heading
   + spin, the same yaw as the model matrix. */
static void local_side(
    float ux, float uz, float yaw, float *lx, float *lz)
{
    float c = cosf(yaw);
    float sn = sinf(yaw);
    *lx = ux * c - uz * sn;
    *lz = ux * sn + uz * c;
}

/* Half-extent of the head along a unit local xz direction. */
static float head_reach(float rx, float rz, float lx, float lz)
{
    return sqrtf((rx * lx) * (rx * lx) + (rz * lz) * (rz * lz));
}

static int check(void)
{
    Bao b = bao_at(30, 30, 0);
    double spin, peak = 0, t, r, x0, z0;
    int i, turn, modes[5] = {0};

    for (i = 0; i < 200000; i++) {
        r = (double)((i * 7919) % 1000) / 1000;
        x0 = b.x;
        z0 = b.z;
        walk(&b, 1.0 / 60, r, 1 - r);
        if (hypot(b.x - x0, b.z - z0) > WALK / 60 + 1e-9)
            return fail("teleport");
        modes[b.mode] = 1;
        if (b.x < CORNER || b.x > EDGE_HI)
            return fail("x out of patch");
        if (b.z < CORNER || b.z > EDGE_HI)
            return fail("z out of patch");
    }
    if (!modes[LOOKING] || !modes[SEEKING] || !modes[PEEING])
        return fail("every act happens");

    /* A long gentle arc, not a tight circle: r2 = 0.9 is a hard
       turn draw, still under TURN_MAX. */
    b = bao_at(30, 30, 0);
    b.wander = 0;
    walk(&b, 1.0 / 60, 0.9, 0.99);
    if (!(fabs(b.turn) <= TURN_MAX) || fabs(b.turn) < 0.5)
        return fail("hard turn");
    b.wander = 0;
    walk(&b, 1.0 / 60, 0.9, 0.7);
    if (!(fabs(b.turn) < 0.1 * TURN_MAX))
        return fail("gentle turn");
    b.wander = 0;
    walk(&b, 1.0 / 60, 0.3, 0.99);
    if (b.turn != 0)
        return fail("straight");

    /* Pinned at a wall, facing it: it must turn away. */
    b = bao_at(EDGE_LO, 30, k_pi);
    run_for(&b, 5, 0.5, 0.5);
    if (!(b.x > EDGE_LO + 1))
        return fail("wall turn");

    /* Look: stop, turn side-on, look up, then walk on. */
    b = bao_at(30, 30, 0);
    b.wander = 0;
    walk(&b, 1.0 / 60, 0.01, 0.5);
    if (b.mode != LOOKING)
        return fail("look starts");
    run_for(&b, 3, 0.5, 0.5);
    if (!(b.speed < 0.5) || !(b.look > 0.8))
        return fail("look pose");
    if (fabs(wrap_angle(b.heading - b.aim)) > 0.01)
        return fail("look turns");
    if (fabs(fabs(wrap_angle(b.aim - FACE_VIEW)) - k_pi / 2) > 1e-9)
        return fail("look side-on");
    run_for(&b, LOOK_TIME, 0.5, 0.5);
    if (b.mode != WALKING || !(b.look < 0.2))
        return fail("look ends");

    /* Wave: stop, face the camera, then look up. The clock
       waits out the turn, so a long turn cannot eat the look. */
    b = bao_at(30, 30, 0);
    notice(&b);
    if (fabs(wrap_angle(b.aim - atan2(CAM_Z - 30, CAM_X - 30)))
        > 1e-9)
        return fail("faces camera");
    if (fabs(fabs(wrap_angle(b.aim - FACE_VIEW)) - k_pi / 2) < 0.2)
        return fail("wave is side-on");
    run_for(&b, 0.25, 0.5, 0.5);
    if (b.mode != NOTICING || b.look > 0.2)
        return fail("look waits");
    if (!(fabs(wrap_angle(b.heading - b.aim)) > 0.2))
        return fail("still turning");
    run_for(&b, 2.0, 0.5, 0.5);
    if (b.mode != NOTICING)
        return fail("wave still on");
    if (fabs(wrap_angle(b.heading - b.aim)) > 0.01)
        return fail("faced camera");
    if (!(b.look > 0.8) || !(b.speed < 0.5) || !(b.mark > 0.8))
        return fail("wave pose");
    b = bao_at(50, 10, 0);
    notice(&b);
    if (fabs(wrap_angle(
            b.aim - atan2(CAM_Z - 10, CAM_X - 50))) > 1e-9)
        return fail("aim follows camera");
    if (fabs(wrap_angle(b.aim - FACE_VIEW)) < 0.2)
        return fail("aim is a fixed heading");
    b = bao_at(30, 30, FACE_VIEW);
    notice(&b);
    run_for(&b, 0.5, 0.5, 0.5);
    if (!(b.look > 0.5))
        return fail("looks once faced");
    run_for(&b, NOTICE_LOOK + 1.2, 0.5, 0.5);
    if (b.mode != WALKING || !(b.look < 0.2) || !(b.mark < 0.2))
        return fail("wave ends");
    notice(&b);
    poke(&b, 1, 1.0);
    if (b.mode != WALKING || b.vy != JUMP_UP)
        return fail("tap beats wave");
    /* Facing the camera, camera-right is the head's +Z side.
       Straight at the front, it is the long axis. */
    {
        float lx, lz, reach;
        local_side(
            -0.70710678f, 0.70710678f,
            (float)(-k_pi / 4), &lx, &lz);
        if (fabsf(lx) > 0.02f || fabsf(lz - 1.0f) > 0.02f)
            return fail("mark side");
        reach = head_reach(4.0f, 3.0f, lx, lz);
        if (fabsf(reach - 3.0f) > 0.02f)
            return fail("mark reach");
        local_side(1.0f, 0.0f, 0.0f, &lx, &lz);
        reach = head_reach(4.0f, 3.0f, lx, lz);
        if (fabsf(reach - 4.0f) > 0.02f)
            return fail("mark reach front");
    }

    /* Pee: walk to the spot, turn side-on, lift, puddle. */
    b = bao_at(40, 30, 0);
    b.wander = 0;
    walk(&b, 1.0 / 60, P_LOOK + 0.01, 0.5);
    if (b.mode != SEEKING)
        return fail("seek starts");
    for (t = 0; t < SEEK_MAX && b.mode == SEEKING; t += 1.0 / 60)
        walk(&b, 1.0 / 60, 0.5, 0.5);
    if (b.mode != PEEING)
        return fail("reaches corner");
    if (hypot(b.x - PEE_X, b.z - PEE_Z) > 1)
        return fail("pee spot");
    run_for(&b, 3, 0.5, 0.5);
    if (!(b.lift > 0.8) || !(b.puddle > 0))
        return fail("pee pose");
    if (fabs(wrap_angle(b.heading - FACE_PEE)) > 0.01)
        return fail("pee pose heading");
    run_for(&b, PEE_TIME, 0.5, 0.5);
    if (b.mode != WALKING || !(b.lift < 0.2))
        return fail("pee ends");
    run_for(&b, 40, 0.5, 0.5);
    if (b.puddle != 0)
        return fail("puddle dries");

    /* A hit interrupts anything. */
    b = bao_at(30, 30, 0);
    set_mode(&b, PEEING);
    poke(&b, 1, 1.0);
    if (b.mode != WALKING || b.vy != JUMP_UP || b.speed != RUN)
        return fail("hit");
    if (b.heading != 1.0)
        return fail("hit heading");
    spin = b.spin_to;
    if (fabs(spin) != 2 * k_pi)
        return fail("hit spin");
    for (t = 0; t < 3; t += 1.0 / 60) {
        walk(&b, 1.0 / 60, 0.5, 0.5);
        if (b.y > peak)
            peak = b.y;
    }
    if (b.y != 0 || b.vy != 0)
        return fail("landing");
    if (!(peak > 5))
        return fail("jump height");
    if (fabs(b.spin - spin) > 0.01)
        return fail("spin settles");
    if (!(b.speed < RUN) || !(b.speed > WALK))
        return fail("run relaxes");

    poke(&b, 0, 0);
    if (b.vy != MISS_JUMP)
        return fail("miss hop");
    b.y = 1;
    b.vy = 5;
    poke(&b, 0, 0);
    if (b.vy != 5)
        return fail("no hop mid-air");

    b = bao_at(30, 30, 0);
    walk(&b, 10, 0.5, 0.5);
    if (fabs(b.x - (30 + WALK * DT_MAX)) > 0.1)
        return fail("dt cap");

    for (turn = 1; turn <= 3; turn += 2) {
        Place pl = scene_place(turn, 2560, 1440);
        double us[4] = {0, 1440, 0, 1440};
        double vs[4] = {0, 0, 2560, 2560};
        Vec fb = logical_to_fb(turn, 1440, 2560, 100, 200);
        Vec back = fb_to_logical(turn, 1440, 2560, fb.x, fb.y);
        if (fabs(back.x - 100) > 1e-6 || fabs(back.y - 200) > 1e-6)
            return fail("touch roundtrip");
        for (i = 0; i < 4; i++) {
            Vec got = place_point(pl, us[i], vs[i]);
            Vec want = logical_to_fb(turn, 1440, 2560, us[i], vs[i]);
            if (fabs(got.x - want.x) > 0.05
                || fabs(got.y - want.y) > 0.05)
                return fail("place corner");
        }
    }
    if (norm_turn(0) != 1 || norm_turn(3) != 3)
        return fail("turn");
    printf("ok\n");
    return 0;
}

#ifndef DSL_BAO_HEADLESS

static const float ROOM = 400;
static const float WALL_H = 90;
static const float QR_FRACTION = 0.2f;
static const char *sound_names[] = {
    "boing",
    "ouch",
    "dont-touch-me",
    "stop-it",
};
static const int sound_n = 4;
static const char *caption_lines[] = {
    "Join us on Telegram!",
    "Scan the code on the right.",
};
static const char FOOTER[] = "Don't touch the bao!";
static const Color INK = {255, 248, 244, 255};
static const Color ROOM_RED = {255, 31, 68, 255};

/* Lambert sun plus sky. With roomShade 1, a cheap corner shade
   from world position: room surfaces darken near the floor and
   walls they do not lie on. colSpecular.r is shine, from the
   glTF metallic factor: a sun highlight, a rim glint and red
   bounce light from the floor. bend and sway deform the bao
   above y = 2 in model space: to look up it is pushed back and
   down, like a head tipped back, and glances side to side.
   Keep them 0 for anything else. */
static const char *VS =
    "attribute vec3 vertexPosition;\n"
    "attribute vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "uniform float bend;\n"
    "uniform float sway;\n"
    "varying vec3 fragNormal;\n"
    "varying vec3 fragPos;\n"
    "void main() {\n"
    "  vec3 q = vertexPosition;\n"
    "  float t = smoothstep(2.0, 7.0, q.y);\n"
    "  q.x -= bend * t * t * 2.2;\n"
    "  q.y -= bend * t * t * 1.0;\n"
    "  q.z += sway * t * t;\n"
    "  fragPos = (matModel * vec4(q, 1.0)).xyz;\n"
    "  fragNormal = (matNormal * vec4(vertexNormal, 0.0)).xyz;\n"
    "  gl_Position = mvp * vec4(q, 1.0);\n"
    "}\n";
static const char *FS =
    "uniform vec4 colDiffuse;\n"
    "uniform float roomShade;\n"
    "uniform vec4 colSpecular;\n"
    "uniform vec3 viewPos;\n"
    "varying vec3 fragNormal;\n"
    "varying vec3 fragPos;\n"
    "void main() {\n"
    "  vec3 n = normalize(fragNormal);\n"
    "  vec3 sun = normalize(vec3(0.55, 1.0, 0.3));\n"
    "  float lit = 0.62 + 0.08 * n.y + 0.35 * max(dot(n, sun), 0.0);\n"
    "  vec3 p = fragPos + step(0.5, n) * 1000.0;\n"
    "  float d = min(min(p.x, p.y), p.z);\n"
    "  float ao = 1.0 - roomShade * 0.28"
    " * (1.0 - smoothstep(0.0, 14.0, d));\n"
    "  vec3 v = normalize(viewPos - fragPos);\n"
    "  float shine = colSpecular.r;\n"
    "  float spec = pow(max(dot(n, normalize(sun + v)), 0.0), 48.0);\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);\n"
    "  vec3 bounce = vec3(1.0, 0.12, 0.27) * max(-n.y, 0.0);\n"
    "  vec3 col = colDiffuse.rgb * lit * ao"
    " + shine * (0.9 * spec + 0.35 * rim + 0.3 * bounce);\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

static Shader load_lit(void)
{
    const char *es_head = "#version 100\nprecision mediump float;\n";
    const char *vs_head =
        "#version 330\n#define attribute in\n#define varying out\n";
    const char *fs_head =
        "#version 330\n#define varying in\n"
        "out vec4 outColor;\n#define gl_FragColor outColor\n";
    int es = rlGetVersion() == RL_OPENGL_ES_20;
    char vs[2048], fs[2048];
    Shader sh;
    snprintf(vs, sizeof vs, "%s%s", es ? es_head : vs_head, VS);
    snprintf(fs, sizeof fs, "%s%s", es ? es_head : fs_head, FS);
    sh = LoadShaderFromMemory(vs, fs);
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh, "matModel");
    sh.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(sh, "matNormal");
    sh.locs[SHADER_LOC_COLOR_SPECULAR] =
        GetShaderLocation(sh, "colSpecular");
    sh.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(sh, "viewPos");
    return sh;
}

/* Also turns the glTF metallic factor into shine. Default
   materials, like the room's, have 0. */
static void use_shader(Model *m, Shader sh)
{
    int i;
    for (i = 0; i < m->materialCount; i++) {
        MaterialMap *maps = m->materials[i].maps;
        float metal = Clamp(maps[MATERIAL_MAP_METALNESS].value, 0, 1);
        unsigned char s = (unsigned char)(metal * 255);
        m->materials[i].shader = sh;
        maps[MATERIAL_MAP_SPECULAR].color = (Color){s, s, s, 255};
    }
}

static Model room_part(Shader sh, Vector3 size, Vector3 at)
{
    Model m = LoadModelFromMesh(GenMeshCube(size.x, size.y, size.z));
    m.transform = MatrixTranslate(at.x, at.y, at.z);
    m.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = ROOM_RED;
    use_shader(&m, sh);
    return m;
}

static const char *asset(const char *key, const char *fallback)
{
    const char *v = getenv(key);
    if (v != NULL && v[0] != '\0')
        return v;
    return fallback;
}

static int screen_turn(void)
{
    const char *v = getenv("DSL_SCREEN_TURN");
    if (v != NULL && v[0] != '\0')
        return norm_turn(atoi(v));
    return norm_turn(DSL_SCREEN_TURN);
}

static int load_model(const char *dir, const char *name, Model *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!FileExists(path)) {
        fprintf(stderr, "dsl-bao-3d: missing %s\n", path);
        return 0;
    }
    *out = LoadModel(path);
    return out->meshCount > 0;
}

static float fit_caption(Font font, float max_w, float max_h)
{
    float size = max_h * 0.38f;
    while (size > 12) {
        float wide = 0, block = 0;
        int i;
        for (i = 0; i < 2; i++) {
            Vector2 m = MeasureTextEx(
                font, caption_lines[i], size, size / 10);
            wide = fmaxf(wide, m.x);
            block += m.y;
        }
        if (wide <= max_w && block <= max_h)
            return size;
        size -= 2;
    }
    return 12;
}

static float fit_footer(Font font, float target_w, float max_h)
{
    float size = 12;
    for (;;) {
        Vector2 m = MeasureTextEx(font, FOOTER, size + 2, (size + 2) / 10);
        if (m.x > target_w || m.y > max_h)
            return size;
        size += 2;
    }
}

static void draw_overlay(
    Font font, Texture2D qr, float cap, float foot, int lw, int lh)
{
    int margin = (int)fmax(16, lh * 0.02);
    int side = (int)(lh * QR_FRACTION);
    float qx = (float)(lw - margin - side);
    Vector2 a = MeasureTextEx(font, caption_lines[0], cap, cap / 10);
    Vector2 b = MeasureTextEx(font, caption_lines[1], cap, cap / 10);
    Vector2 f = MeasureTextEx(font, FOOTER, foot, foot / 10);
    float gap = fmaxf(4, a.y / 8);
    float y = margin + (side - (a.y + gap + b.y)) / 2;
    DrawTexturePro(
        qr, (Rectangle){0, 0, (float)qr.width, (float)qr.height},
        (Rectangle){qx, (float)margin, (float)side, (float)side},
        (Vector2){0, 0}, 0, WHITE);
    DrawTextEx(
        font, caption_lines[0], (Vector2){(float)margin, y},
        cap, cap / 10, INK);
    DrawTextEx(
        font, caption_lines[1], (Vector2){(float)margin, y + a.y + gap},
        cap, cap / 10, INK);
    DrawTextEx(
        font, FOOTER,
        (Vector2){lw - margin - f.x, lh - margin - f.y},
        foot, foot / 10, INK);
}

static int load_sounds(const char *dir, Sound *out)
{
    int n = 0, i;
    if (dir[0] == '\0')
        return 0;
    InitAudioDevice();
    for (i = 0; i < sound_n; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.wav", dir, sound_names[i]);
        if (!FileExists(path)) {
            fprintf(stderr, "dsl-bao-3d: missing %s\n", path);
            continue;
        }
        out[n] = LoadSound(path);
        if (out[n].frameCount > 0)
            n++;
    }
    return n;
}

static void play_hit(Sound *sounds, int count)
{
    int i;
    if (count <= 0 || !IsAudioDeviceReady())
        return;
    for (i = 0; i < count; i++)
        StopSound(sounds[i]);
    PlaySound(sounds[GetRandomValue(0, count - 1)]);
}

static int take_tap(
    int turn, int lw, int lh, int down,
    double now, double *last, Vector2 *at)
{
    Vector2 raw;
    Vec p;
    if (GetTouchPointCount() > 0 && !down)
        raw = GetTouchPosition(0);
    else if (GetTouchPointCount() == 0
             && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        raw = GetMousePosition();
    else
        return 0;
    if (*last >= 0 && now - *last < 0.12)
        return 0;
    p = fb_to_logical(turn, lw, lh, raw.x, raw.y);
    *at = (Vector2){(float)p.x, (float)p.y};
    *last = now;
    return 1;
}

enum { HIND_N = 3 };

typedef struct {
    Model body, legs_a, legs_b, hind[HIND_N];
    float floor_y;
    Matrix hip[HIND_N], hip_back[HIND_N], tilt, tilt_back;
    Matrix sit, sit_back;
    Vector3 tail, head;
    float head_rx, head_rz;
    int bend_loc, sway_loc;
    Shader lit;
} Parts;

static const Color PEE = {255, 212, 40, 255};
/* Hind pins, rearmost first: how far each swings up, in rad, and
   whether it walks with group A (else B). */
static const float HIND_SWING[HIND_N] = {1.4f, 0.8f, 0.6f};
static const int HIND_IN_A[HIND_N] = {0, 1, 0};

/* Legs trot: a group swings forward while lifted, then plants
   and pushes back. A and B run half a cycle apart. */
static Matrix leg_offset(double phase, double lift)
{
    return MatrixTranslate(
        (float)(sin(phase) * 0.45),
        (float)(fmax(0, cos(phase)) * 0.55 * lift),
        0);
}

static void draw_part(Model m, Matrix local, Matrix world)
{
    m.transform = MatrixMultiply(local, world);
    DrawModel(m, (Vector3){0, 0, 0}, 1, WHITE);
}

static void set_float(const Parts *p, int loc, float v)
{
    SetShaderValue(p->lit, loc, &v, SHADER_UNIFORM_FLOAT);
}

/* A rotation through a pivot: pivot, turn, back. */
static Matrix turn_at(Matrix pivot, Matrix turn, Matrix back)
{
    return MatrixMultiply(MatrixMultiply(pivot, turn), back);
}

/* The stream leaves the middle of the chip's rear and arcs out
   to the lifted side (local +Z) until it meets a wall. The puddle stays where
   the last stream landed. */
static void draw_pee(const Parts *p, const Bao *b, Matrix world,
                     double now)
{
    static Vector3 pool;
    Vector3 from = Vector3Transform(p->tail, world);
    Vector3 behind = Vector3Transform(
        Vector3Add(p->tail, (Vector3){0, 0, 1}), world);
    Vector3 dir = Vector3Normalize(
        (Vector3){behind.x - from.x, 0, behind.z - from.z});
    float reach = 1e6f, rise;
    Vector3 to;
    int i, n = 28;
    if (dir.x < -1e-3f)
        reach = fminf(reach, (from.x - 0.3f) / -dir.x);
    if (dir.z < -1e-3f)
        reach = fminf(reach, (from.z - 0.3f) / -dir.z);
    reach = fminf(reach, 12);
    to = (Vector3){
        from.x + dir.x * reach, 1.2f, from.z + dir.z * reach};
    rise = fminf(3, reach * 0.35f);
    if (b->mode == PEEING && b->lift > 0.8)
        pool = (Vector3){
            to.x - dir.x * 0.8f, 0.04f, to.z - dir.z * 0.8f};
    if (b->puddle > 0) {
        float r = (float)(0.8 + 2.4 * sqrt(b->puddle));
        Color c = PEE;
        c.a = (unsigned char)(150 * fmin(1, b->puddle * 4));
        DrawCylinder(pool, r, r, 0.02f, 32, c);
    }
    if (b->mode != PEEING || b->lift < 0.8)
        return;
    for (i = 0; i < n; i++) {
        float s = (float)fmod((double)i / n + now * 1.6, 1);
        Vector3 at = Vector3Lerp(from, to, s);
        at.y += (float)(sin(k_pi * s)) * rise;
        DrawSphereEx(at, 0.18f, 4, 6, PEE);
    }
}

static const double CAM_Y = 50;
static const double CAM_TX = 27;
static const double CAM_TY = 9;
static const double CAM_TZ = 27;

static float smooth3(float a, float b, float x)
{
    float t;
    if (x <= a)
        return 0;
    if (x >= b)
        return 1;
    t = (x - a) / (b - a);
    return t * t * (3 - 2 * t);
}

/* Yellow !. The rod is wider at the top. The ball sits under
   it, with a gap, a hair out from the head on the camera's
   right. Flip it if that side would sink into a wall. The
   bend matches the vertex shader. */
static void draw_mark(
    const Parts *p, const Bao *b, Matrix world, double now)
{
    float s = (float)b->mark;
    float dx, dz, len, ux, uz, lx, lz, reach, dist;
    float sx, sz, t, bend, sway;
    float rod_h, top_r, bot_r, ball_r, gap;
    Vector3 q, at, ball, rod_bot, rod_top;
    Color ink = {255, 204, 32, 255};
    if (s < 0.02f)
        return;
    q = p->head;
    t = smooth3(2, 7, q.y);
    t = t * t;
    bend = (float)b->look;
    sway = bend * 0.6f * (float)sin(now * 1.3);
    q.x -= bend * t * 2.2f;
    q.y -= bend * t * 1.0f;
    q.z += sway * t;
    at = Vector3Transform(q, world);
    dx = (float)(CAM_TX - CAM_X);
    dz = (float)(CAM_TZ - CAM_Z);
    len = sqrtf(dx * dx + dz * dz);
    ux = dz / len;
    uz = -dx / len;
    local_side(
        ux, uz, (float)(-b->heading + b->spin), &lx, &lz);
    ball_r = 1.15f * s;
    reach = head_reach(p->head_rx, p->head_rz, lx, lz);
    dist = reach + ball_r + 0.2f;
    sx = ux * dist;
    sz = uz * dist;
    at.x += sx;
    at.z += sz;
    if (at.x < 2.0f || at.z < 2.0f) {
        at.x -= 2 * sx;
        at.z -= 2 * sz;
    }
    rod_h = 6.4f * s;
    top_r = 1.25f * s;
    bot_r = 0.48f * s;
    gap = 0.7f * s;
    ball = (Vector3){at.x, at.y - 1.2f * s, at.z};
    rod_bot = (Vector3){at.x, ball.y + ball_r + gap, at.z};
    rod_top = (Vector3){at.x, rod_bot.y + rod_h, at.z};
    rlDisableShader();
    DrawCylinderEx(rod_bot, rod_top, bot_r, top_r, 18, ink);
    DrawSphere(ball, ball_r, ink);
}

static void draw_bao(const Parts *p, const Bao *b, double squash,
                     double now)
{
    double grounded = b->y > 0 ? 0 : 1;
    double stride = b->gait * grounded;
    float bob = (float)(fabs(sin(b->phase)) * 0.25 * stride);
    float wide = (float)(1 + (1 - squash) * 0.5);
    float shadow = (float)(4.2 / (1 + b->y * 0.08));
    /* Peeing, like a dog: the rear rises about the front feet,
       and the chip leans onto its far pins (-Z), away from the
       lifted near side. */
    Matrix pose = MatrixMultiply(
        turn_at(
            p->sit, MatrixRotateZ((float)(-0.12 * b->lift)),
            p->sit_back),
        turn_at(
            p->tilt, MatrixRotateX((float)(-0.2 * b->lift)),
            p->tilt_back));
    Matrix world = MatrixMultiply(
        MatrixMultiply(
            MatrixMultiply(MatrixScale(wide, (float)squash, wide), pose),
            MatrixRotateY((float)(-b->heading + b->spin))),
        MatrixTranslate(
            (float)b->x, p->floor_y + (float)b->y + bob, (float)b->z));
    Matrix off_a = leg_offset(b->phase, stride);
    Matrix off_b = leg_offset(b->phase + k_pi, stride);
    int i;
    draw_pee(p, b, world, now);
    DrawCylinder(
        (Vector3){(float)b->x, 0.05f, (float)b->z},
        shadow, shadow, 0.02f, 24, (Color){90, 0, 20, 110});
    set_float(p, p->bend_loc, (float)b->look);
    set_float(p, p->sway_loc, (float)(b->look * 0.6 * sin(now * 1.3)));
    draw_part(p->body, MatrixIdentity(), world);
    set_float(p, p->bend_loc, 0);
    set_float(p, p->sway_loc, 0);
    draw_part(p->legs_a, off_a, world);
    draw_part(p->legs_b, off_b, world);
    for (i = 0; i < HIND_N; i++) {
        /* Out sideways and up: a dog's hind leg at a post. */
        float a = (float)(HIND_SWING[i] * b->lift);
        Matrix raise = turn_at(
            p->hip[i],
            MatrixRotateX(-a),
            p->hip_back[i]);
        draw_part(
            p->hind[i],
            MatrixMultiply(HIND_IN_A[i] ? off_a : off_b, raise), world);
    }
    draw_mark(p, b, world, now);
}

static void present(Texture2D tex, int turn, int fw, int fh)
{
    Place pl = scene_place(turn, fw, fh);
    DrawTexturePro(
        tex, (Rectangle){0, 0, (float)tex.width, -(float)tex.height},
        (Rectangle){(float)pl.x, (float)pl.y, (float)pl.w, (float)pl.h},
        (Vector2){0, 0}, (float)pl.rot, WHITE);
}

static const char *wave_path(void)
{
    const char *v = getenv("DSL_BAO_WAVE");
    if (v != NULL && v[0] != '\0')
        return v;
    return "/tmp/dsl-bao-wave.sock";
}

static int open_wave(void)
{
    struct sockaddr_un addr;
    const char *path = wave_path();
    size_t n = strlen(path);
    int fd, flags;
    if (n >= sizeof addr.sun_path)
        return -1;
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0)
        return -1;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, n + 1);
    unlink(path);
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        fprintf(stderr, "dsl-bao-3d: wave socket failed\n");
        return -1;
    }
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static int take_wave(int fd)
{
    char buf[8];
    int got = 0;
    if (fd < 0)
        return 0;
    while (recv(fd, buf, sizeof buf, 0) > 0)
        got = 1;
    return got;
}

static int run(void)
{
    const char *dir = asset("DSL_BAO_MODELS", DSL_BAO_MODELS);
    const char *font_path = asset("DSL_BAO_FONT", DSL_BAO_FONT);
    const char *qr_path = asset("DSL_BAO_QR", DSL_BAO_QR);
    const char *sound_dir = asset("DSL_BAO_SOUNDS", DSL_BAO_SOUNDS);
    const char *shot = getenv("DSL_BAO_SHOT");
    int turn, fw, fh, lw, lh, sound_count, down = 0, frame = 0, i;
    int wave_fd = -1;
    Model floor, wall_x, wall_z;
    Parts p;
    Shader lit;
    Texture2D qr;
    Font font;
    RenderTexture2D scene;
    Sound sounds[4];
    Camera3D cam;
    BoundingBox box;
    Bao b = bao_at(34, 26, 2.4);
    float cap, foot, hit_y, hit_r;
    double squash = 1, last_tap = -1;
    float one = 1, zero = 0;
    int shade_loc;

    if (!FileExists(font_path) || !FileExists(qr_path)) {
        fprintf(stderr, "dsl-bao-3d: font or qr missing\n");
        return 1;
    }
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    /* InitWindow(0, 0) copies an unset display size, then
       the nearest-mode search picks the smallest mode. */
    InitWindow(2560, 1440, "dsl-bao-3d");
    if (!IsWindowReady())
        return fail("display did not open");
    HideCursor();
    SetTargetFPS(60);
    fw = GetScreenWidth();
    fh = GetScreenHeight();
    fprintf(stderr, "dsl-bao-3d: mode %dx%d\n", fw, fh);
    turn = screen_turn();
    lw = fh;
    lh = fw;

    lit = load_lit();
    if (!IsShaderValid(lit)
        || !load_model(dir, "bao-3d-body.glb", &p.body)
        || !load_model(dir, "bao-3d-legs-a.glb", &p.legs_a)
        || !load_model(dir, "bao-3d-legs-b.glb", &p.legs_b)
        || !load_model(dir, "bao-3d-hind-1.glb", &p.hind[0])
        || !load_model(dir, "bao-3d-hind-2.glb", &p.hind[1])
        || !load_model(dir, "bao-3d-hind-3.glb", &p.hind[2])) {
        CloseWindow();
        return fail("model or shader failed");
    }
    shade_loc = GetShaderLocation(lit, "roomShade");
    p.lit = lit;
    p.bend_loc = GetShaderLocation(lit, "bend");
    p.sway_loc = GetShaderLocation(lit, "sway");
    use_shader(&p.body, lit);
    use_shader(&p.legs_a, lit);
    use_shader(&p.legs_b, lit);
    for (i = 0; i < HIND_N; i++)
        use_shader(&p.hind[i], lit);
    floor = room_part(
        lit, (Vector3){ROOM, 1, ROOM},
        (Vector3){ROOM / 2, -0.5f, ROOM / 2});
    wall_x = room_part(
        lit, (Vector3){1, WALL_H, ROOM},
        (Vector3){-0.5f, WALL_H / 2, ROOM / 2});
    wall_z = room_part(
        lit, (Vector3){ROOM, WALL_H, 1},
        (Vector3){ROOM / 2, WALL_H / 2, -0.5f});
    box = GetModelBoundingBox(p.legs_a);
    p.floor_y = -box.min.y;
    /* Tilt about the far pins' feet. */
    p.tilt = MatrixTranslate(0, p.floor_y, -box.min.z);
    p.tilt_back = MatrixInvert(p.tilt);
    /* Rear up about the front feet. */
    p.sit = MatrixTranslate(-box.max.x, p.floor_y, 0);
    p.sit_back = MatrixInvert(p.sit);
    /* Hip: inner top of each hind pin. */
    for (i = 0; i < HIND_N; i++) {
        box = GetModelBoundingBox(p.hind[i]);
        p.hip[i] = MatrixTranslate(
            -(box.min.x + box.max.x) / 2, -box.max.y, -box.min.z);
        p.hip_back[i] = MatrixInvert(p.hip[i]);
    }
    /* Tail: middle of the chip's rear face. The chip is the body
       mesh that sits lowest. */
    box = GetMeshBoundingBox(p.body.meshes[0]);
    for (i = 1; i < p.body.meshCount; i++) {
        BoundingBox m = GetMeshBoundingBox(p.body.meshes[i]);
        if (m.max.y < box.max.y)
            box = m;
    }
    p.tail = (Vector3){
        box.min.x, (box.min.y + box.max.y) / 2,
        (box.min.z + box.max.z) / 2};
    box = GetModelBoundingBox(p.body);
    hit_y = p.floor_y + (box.min.y + box.max.y) / 2;
    hit_r = (box.max.x - box.min.x) * 0.6f;
    p.head = (Vector3){
        (box.min.x + box.max.x) / 2, box.max.y,
        (box.min.z + box.max.z) / 2};
    p.head_rx = (box.max.x - box.min.x) * 0.5f;
    p.head_rz = (box.max.z - box.min.z) * 0.5f;

    qr = LoadTexture(qr_path);
    font = LoadFontEx(font_path, 128, NULL, 0);
    SetTextureFilter(qr, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    scene = LoadRenderTexture(lw, lh);
    if (qr.id == 0 || font.texture.id == 0 || scene.id == 0) {
        CloseWindow();
        return fail("asset upload failed");
    }
    sound_count = load_sounds(sound_dir, sounds);
    cap = fit_caption(
        font, lw - lh * QR_FRACTION - 3 * fmaxf(16, lh * 0.02f),
        lh * QR_FRACTION);
    foot = fit_footer(font, lw / 2.0f, lh / 8.0f);

    /* ponytail: framing is by eye for a 9:16 portrait. */
    cam.position = (Vector3){(float)CAM_X, (float)CAM_Y, (float)CAM_Z};
    cam.target = (Vector3){
        (float)CAM_TX, (float)CAM_TY, (float)CAM_TZ};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 34;
    cam.projection = CAMERA_PERSPECTIVE;
    SetShaderValue(
        lit, lit.locs[SHADER_LOC_VECTOR_VIEW], &cam.position,
        SHADER_UNIFORM_VEC3);
    wave_fd = open_wave();

    for (;;) {
        double dt = GetFrameTime();
        double now = GetTime();
        Vector2 tap;
        int hit = 0;
        if (take_tap(turn, lw, lh, down, now, &last_tap, &tap)) {
            Ray ray = GetScreenToWorldRayEx(tap, cam, lw, lh);
            Vector3 c = {(float)b.x, hit_y + (float)b.y, (float)b.z};
            double flee;
            hit = GetRayCollisionSphere(ray, c, hit_r).hit;
            flee = atan2(b.z - ray.position.z, b.x - ray.position.x)
                + (GetRandomValue(-60, 60) * k_pi / 180);
            poke(&b, hit, flee);
            if (hit) {
                play_hit(sounds, sound_count);
                (void)take_wave(wave_fd);
            }
            squash = 0.7;
        }
        down = GetTouchPointCount() > 0;
        if (!hit && b.y == 0 && take_wave(wave_fd))
            notice(&b);
        walk(&b, dt, GetRandomValue(0, 999) / 1000.0,
             GetRandomValue(0, 999) / 1000.0);
        squash += (1 - squash) * fmin(1, dt * 8);

        BeginTextureMode(scene);
        ClearBackground(ROOM_RED);
        BeginMode3D(cam);
        SetShaderValue(lit, shade_loc, &one, SHADER_UNIFORM_FLOAT);
        DrawModel(floor, (Vector3){0, 0, 0}, 1, WHITE);
        DrawModel(wall_x, (Vector3){0, 0, 0}, 1, WHITE);
        DrawModel(wall_z, (Vector3){0, 0, 0}, 1, WHITE);
        SetShaderValue(lit, shade_loc, &zero, SHADER_UNIFORM_FLOAT);
        draw_bao(&p, &b, squash, now);
        EndMode3D();
        draw_overlay(font, qr, cap, foot, lw, lh);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        present(scene.texture, turn, fw, fh);
        EndDrawing();

        if (shot != NULL && ++frame == 120) {
            Image im = LoadImageFromTexture(scene.texture);
            ImageFlipVertical(&im);
            ExportImage(im, shot);
            if (wave_fd >= 0)
                close(wave_fd);
            CloseWindow();
            return 0;
        }
    }
}

#endif

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--check") == 0)
        return check();
#ifdef DSL_BAO_HEADLESS
    fprintf(stderr, "dsl-bao-3d: this build has no display\n");
    return 1;
#else
    (void)argv;
    return run();
#endif
}
