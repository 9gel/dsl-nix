#include "bao.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fail(const char *msg)
{
    fprintf(stderr, "bao-test: %s\n", msg);
    return 1;
}

static int tests(void)
{
    int sw, sh, side, qx, qy, play_top, margin, width, right;
    int lw, lh, i;
    double vy, us[4], vs[4];
    Vec capped, slowed, back;
    Body body, missed, spinning, capped_body, still;
    Frame fr;
    Place place;

    logo_span(1000, 800, 85, 85, &sw, &sh);
    if (sw != 320 || sh != 320)
        return fail("logo_span");
    logo_span(100, 100, 0, 0, &sw, &sh);
    if (sw != 1 || sh != 1)
        return fail("logo_span zero");

    capped = clamp_speed(3000, 4000);
    if (hypot(capped.x, capped.y) > MAX_SPEED + 1e-6)
        return fail("clamp_speed");

    slowed = damp(1000, 0, 1);
    if (!(CRUISE < slowed.x && slowed.x < 1000) || slowed.y != 0)
        return fail("damp fast");
    slowed = damp(10, 0, 1);
    if (!(10 < slowed.x && slowed.x <= CRUISE))
        return fail("damp slow");

    body = (Body){5, 100, -200, 0, 20, 20, 0, 0};
    if (!step(&body, 200, 200, 0.016, 0) || body.x != 10)
        return fail("left wall");
    if (!(body.vx > 0))
        return fail("left wall vx");

    body = (Body){100, 195, 0, 400, 20, 20, 0, 0};
    step(&body, 200, 200, 0.016, 0);
    if (body.y != 190 || !(body.vy < 0))
        return fail("floor");

    body = (Body){15, 100, -MAX_SPEED, 0, 20, 20, 0, 0};
    step(&body, 200, 200, 10, 0);
    if (body.x < 10 || body.x > 190)
        return fail("dt cap");

    body = (Body){100, 100, 0, 0, 40, 40, 0, 0};
    if (!poke(&body, 115, 100) || !(body.vx < 0))
        return fail("poke hit");
    if (body.vy != -JUMP_UP || !(body.spin < 0))
        return fail("poke jump");
    if (body.hop != HOP_TIME)
        return fail("poke hop");
    body.x = 1000;
    body.y = 1000;
    vy = body.vy;
    step(&body, 4000, 4000, 0.1, 0);
    if (!(body.y < 1000) || !(vy < body.vy && body.vy < 0))
        return fail("gravity");

    missed = (Body){100, 100, 0, 0, 40, 40, 0, 0};
    if (poke(&missed, 0, 0) || missed.vy != -MISS_JUMP)
        return fail("poke miss");
    if (!(missed.vy > -JUMP_UP))
        return fail("miss shorter");

    spinning = (Body){200, 200, 30, 0, 20, 20, 4, 0};
    step(&spinning, 800, 800, 0.05, 0);
    if (!(spinning.spin < 4))
        return fail("spin damp");

    header_box(1440, 2560, &side, &qx, &qy, &play_top);
    if (side != 512 || qy != 51 || qx != 1440 - 51 - 512)
        return fail("header");
    if (play_top != qy + side)
        return fail("play top");

    footer_box(1440, 2560, &margin, &width, &right);
    if (margin != 51 || width != 720 || right != 1440 - 51)
        return fail("footer");

    for (i = 0; i < 4; i++) {
        if (strcmp(sound_at(i), sound_names[i]) != 0)
            return fail("sound order");
    }
    if (strcmp(sound_at(4), "boing") != 0)
        return fail("sound wrap");

    capped_body = (Body){100, 40, 0, -400, 20, 20, 0, 0};
    step(&capped_body, 400, 800, 0.05, 100);
    if (capped_body.y != 110 || !(capped_body.vy > 0))
        return fail("ceiling");

    still = (Body){100, 100, 0, 0, 40, 40, 0, 0};
    fr = frame_of(&still, 1, 0);
    if (fr.cx != 100 || fr.cy != 100 || fr.width != 40)
        return fail("logo frame");
    if (fr.height != 40)
        return fail("logo frame h");
    if (fr.sx != 100 || fr.sy != 116 || fr.shadow_w != 24)
        return fail("shadow");
    if (fr.shadow_h != 5)
        return fail("shadow h");
    if (fr.box_l != 74 || fr.box_t != 74)
        return fail("box origin");
    if (fr.box_r != 126 || fr.box_b != 126)
        return fail("box extent");

    for (i = 0; i < 2; i++) {
        int turn = i == 0 ? 1 : 3;
        logical_size(turn, 2560, 1440, &lw, &lh);
        if (lw != 1440 || lh != 2560)
            return fail("logical size");
        back = fb_to_logical(
            turn, lw, lh,
            logical_to_fb(turn, lw, lh, 100, 200).x,
            logical_to_fb(turn, lw, lh, 100, 200).y);
        if (fabs(back.x - 100) > 1e-6 || fabs(back.y - 200) > 1e-6)
            return fail("touch roundtrip");
        us[0] = 0;
        vs[0] = 0;
        us[1] = lw;
        vs[1] = 0;
        us[2] = 0;
        vs[2] = lh;
        us[3] = lw;
        vs[3] = lh;
        place = scene_place(turn, 2560, 1440);
        for (sw = 0; sw < 4; sw++) {
            Vec got = place_point(place, us[sw], vs[sw]);
            Vec want = logical_to_fb(
                turn, lw, lh, us[sw], vs[sw]);
            if (fabs(got.x - want.x) > 0.05
                || fabs(got.y - want.y) > 0.05) {
                fprintf(
                    stderr,
                    "place turn %d corner %d\n", turn, sw);
                return 1;
            }
        }
    }
    if (norm_turn(1) != 1 || norm_turn(3) != 3)
        return fail("turn");
    if (norm_turn(0) != 1)
        return fail("turn default");
    printf("ok\n");
    return 0;
}

int main(void)
{
    return tests();
}
