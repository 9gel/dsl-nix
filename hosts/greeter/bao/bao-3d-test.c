#include "bao-3d.h"

#include <math.h>
#include <stdio.h>

static int fail(const char *msg)
{
    fprintf(stderr, "bao-3d-test: %s\n", msg);
    return 1;
}

static void run_for(Bao *b, double seconds, double r1, double r2)
{
    double t;
    for (t = 0; t < seconds; t += 1.0 / 60)
        walk(b, 1.0 / 60, r1, r2);
}

static int tests(void)
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
    {
        float scx, scy, hop;
        mark_pose(0, &scx, &scy, &hop);
        if (!(scx < 0.35f) || fabsf(scx - scy) > 0.02f)
            return fail("mark starts small");
        if (!(hop < -0.5f))
            return fail("mark jump start");
        mark_pose(MARK_JUMP, &scx, &scy, &hop);
        if (fabsf(scx - 1) > 0.02f || fabsf(scy - 1) > 0.02f)
            return fail("mark full size");
        if (fabsf(hop) > 0.02f)
            return fail("mark landed");
        mark_pose(0.82f, &scx, &scy, &hop);
        if (!(scy > 1.1f) || !(scy > scx + 0.15f))
            return fail("mark stretches");
        if (fabsf(hop) > 0.02f)
            return fail("stretch stays put");
        mark_pose(1, &scx, &scy, &hop);
        if (fabsf(scx - 1) > 0.02f || fabsf(scy - 1) > 0.02f)
            return fail("mark settles");
        if (fabsf(hop) > 0.02f)
            return fail("mark hop ends");
    }
    b = bao_at(30, 30, 0);
    b.pop = 0.7;
    notice(&b);
    if (b.pop != 0)
        return fail("pop resets");
    run_for(&b, 0.05, 0.5, 0.5);
    if (!(b.pop > 0.05 && b.pop < 0.3))
        return fail("mark pops");
    run_for(&b, 1, 0.5, 0.5);
    if (b.pop != 1 || b.mark != 1)
        return fail("mark pop ends");

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
    {
        Hud h = hud_place(1440, 2560, 80);
        float bottom = h.qy + h.side;
        float x, y, w, hgt, bx, by, bw, bh;
        if (fabsf(h.side - 2560 * 0.2f) > 0.01f)
            return fail("qr size");
        if (!(h.qy > h.ty + 80) || !(h.qx > 720))
            return fail("qr");
        feed_place(bottom, 240, 320, &x, &y, &w, &hgt);
        if (fabsf(y - 10) > 0.01f || fabsf(x - 10) > 0.01f)
            return fail("video top");
        if (fabsf((y + hgt) - bottom) > 0.01f)
            return fail("video bottom");
        if (fabsf(w / hgt - 0.75f) > 0.001f)
            return fail("aspect");
        feed_box(w, hgt, 0.25f, 0.25f, 0.75f, 0.75f,
            &bx, &by, &bw, &bh);
        if (fabsf(bx - 0.25f * w) > 0.01f
            || fabsf(by - 0.25f * hgt) > 0.01f
            || fabsf(bw - 0.5f * w) > 0.01f
            || fabsf(bh - 0.5f * hgt) > 0.01f)
            return fail("hand");
    }
    printf("ok\n");
    return 0;
}

int main(void)
{
    return tests();
}
