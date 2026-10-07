#include "bao-gesture.h"

#include <stdio.h>
#include <string.h>

static int checked;

static int fail(const char *msg)
{
    fprintf(stderr, "bao-wave-test: %s\n", msg);
    return 1;
}

/* One count per check, even when the check sits in a loop. */
static void hit(int line)
{
    static int lines[160];
    int i;

    for (i = 0; i < checked; i++)
        if (lines[i] == line)
            return;
    lines[checked++] = line;
}

static int near(float a, float b)
{
    float d = a - b;
    if (d < 0)
        d = -d;
    return d < 0.001f;
}

/* n is the pattern length. The rest of the window is a
   still hand: same place, no palm score. A short wave
   has to pass on its own. */
static int push_all(
    WaveHist *h, const float *x, const float *y,
    int n, float mass)
{
    int i, got = 0;
    float lx = 0.5f, ly = 0.5f;
    wave_clear(h);
    for (i = 0; i < WAVE_N; i++) {
        float m = 0;
        if (i < n) {
            if (x != NULL)
                lx = x[i];
            if (y != NULL)
                ly = y[i];
            m = mass;
        }
        got = wave_push(h, lx, ly, m);
    }
    return got;
}

static Palm palm_at(float x, float y, float conf)
{
    Palm p;
    p.x = x;
    p.y = y;
    p.conf = conf;
    p.x0 = x - 0.08f;
    p.y0 = y - 0.10f;
    p.x1 = x + 0.08f;
    p.y1 = y + 0.10f;
    return p;
}

/* Two hands must not swap the box. One hand must survive
   a short gap, then drop if it stays gone. */
static int track_cases(void)
{
    Track t;
    Palm hands[2];
    Palm back;
    int i;
    memset(&t, 0, sizeof t);
    hands[0] = palm_at(0.20f, 0.50f, 0.80f);
    hands[1] = palm_at(0.80f, 0.50f, 0.95f);
    palm_follow(&t, hands, 2);
    if (!t.on || !near(t.box.x, 0.80f))
        return fail("lock");
    hit(__LINE__);
    hands[0].conf = 0.99f;
    hands[1].conf = 0.60f;
    palm_follow(&t, hands, 2);
    if (!near(t.box.x, 0.80f) || t.age != 0)
        return fail("stick");
    hit(__LINE__);
    hands[1].x = 0.84f;
    palm_follow(&t, hands, 2);
    if (!near(t.box.x, 0.84f))
        return fail("follow");
    hit(__LINE__);
    for (i = 0; i < PALM_KEEP; i++) {
        palm_follow(&t, NULL, 0);
        if (!t.on || !near(t.box.x, 0.84f))
            return fail("coast");
        hit(__LINE__);
    }
    palm_follow(&t, NULL, 0);
    if (t.on)
        return fail("drop");
    hit(__LINE__);
    back = palm_at(0.20f, 0.50f, 0.70f);
    palm_follow(&t, &back, 1);
    palm_follow(&t, NULL, 0);
    palm_follow(&t, NULL, 0);
    back.x = 0.24f;
    palm_follow(&t, &back, 1);
    if (!t.on || !near(t.box.x, 0.24f) || t.age != 0)
        return fail("return");
    hit(__LINE__);
    /* The other hand is visible, but not for long enough
       to steal the box. */
    hands[0] = palm_at(0.70f, 0.50f, 0.99f);
    palm_follow(&t, hands, 1);
    if (!near(t.box.x, 0.24f) || t.age != 1)
        return fail("hold");
    hit(__LINE__);
    return 0;
}

static int palm_cases(void)
{
    static float box[PALM_N * PALM_C];
    static float score[PALM_N];
    Palm out[PALM_OUT];
    int n, i;
    for (i = 0; i < PALM_N; i++)
        score[i] = -8;
    memset(box, 0, sizeof box);
    if (palm_list(box, score, PALM_S, PALM_S, out, PALM_OUT))
        return fail("list-empty");
    hit(__LINE__);
    score[0] = -0.40f;
    n = palm_list(box, score, PALM_S, PALM_S, out, PALM_OUT);
    if (n != 1 || out[0].conf < PALM_TRACK_MIN)
        return fail("list-weak");
    hit(__LINE__);
    if (palm_center(box, score, PALM_S, PALM_S, &out[0]))
        return fail("weak-wave");
    hit(__LINE__);
    score[0] = 4;
    score[2] = 3;
    box[2] = 0.80f * (float)PALM_S;
    box[3] = 0.80f * (float)PALM_S;
    box[2 * PALM_C + 2] = 0.80f * (float)PALM_S;
    box[2 * PALM_C + 3] = 0.80f * (float)PALM_S;
    n = palm_list(box, score, PALM_S, PALM_S, out, PALM_OUT);
    if (n != 1 || !near(out[0].x, 0.020833f))
        return fail("list-one");
    hit(__LINE__);
    memset(box, 0, sizeof box);
    score[2] = -8;
    score[PALM_N - 1] = 3;
    box[2] = 0.05f * (float)PALM_S;
    box[3] = 0.05f * (float)PALM_S;
    box[(PALM_N - 1) * PALM_C + 2] = 0.05f * (float)PALM_S;
    box[(PALM_N - 1) * PALM_C + 3] = 0.05f * (float)PALM_S;
    n = palm_list(box, score, PALM_S, PALM_S, out, PALM_OUT);
    if (n != 2)
        return fail("list-two");
    hit(__LINE__);
    if (!near(out[0].x, 0.020833f)
        || out[1].x < 0.90f)
        return fail("list-order");
    hit(__LINE__);
    return 0;
}

static int tests(void)
{
    WaveHist h;
    int i;
    static const float wave[] = {
        0.28f, 0.62f, 0.28f, 0.62f, 0.28f, 0.62f, 0.28f, 0.62f};
    static const float flick[] = {
        0.46f, 0.58f, 0.46f, 0.58f, 0.46f, 0.58f, 0.46f, 0.58f};
    /* One swipe turns once. Two turns is across, back,
       forward. A wave needs the last back as well. */
    static const float swipe[] = {
        0.28f, 0.62f, 0.28f, 0.28f, 0.28f, 0.28f, 0.28f, 0.28f};
    static const float hello[] = {
        0.28f, 0.62f, 0.28f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f};
    static const float full[] = {
        0.25f, 0.70f, 0.25f, 0.70f, 0.25f, 0.25f, 0.25f, 0.25f};
    static const float walk[] = {
        0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f};
    static const float jitter[] = {
        0.50f, 0.52f, 0.49f, 0.51f, 0.50f, 0.52f, 0.48f, 0.51f};
    float mid[WAVE_N];
    memset(&h, 0, sizeof h);
    for (i = 0; i < WAVE_N; i++) {
        float x = (i & 1) ? 0.62f : 0.28f;
        int got = wave_push(&h, x, 0.5f, 0.80f);
        if (got != (i == WAVE_N - 1))
            return fail("wave");
        hit(__LINE__);
    }
    if (wave_push(&h, 0.40f, 0.5f, 0.80f))
        return fail("cleared");
    hit(__LINE__);
    if (push_all(&h, walk, NULL, 8, 0.80f))
        return fail("walk");
    hit(__LINE__);
    if (push_all(&h, jitter, NULL, 8, 0.80f))
        return fail("jitter");
    hit(__LINE__);
    if (push_all(&h, flick, NULL, 8, 0.80f))
        return fail("flick");
    hit(__LINE__);
    if (push_all(&h, swipe, NULL, 8, 0.80f))
        return fail("swipe");
    hit(__LINE__);
    if (push_all(&h, hello, NULL, 8, 0.80f))
        return fail("hello");
    hit(__LINE__);
    if (!push_all(&h, full, NULL, 8, 0.80f))
        return fail("full");
    hit(__LINE__);
    if (push_all(&h, wave, NULL, 8, 0))
        return fail("nobody");
    hit(__LINE__);
    if (push_all(&h, NULL, full, 8, 0.80f))
        return fail("sideways");
    hit(__LINE__);
    wave_clear(&h);
    for (i = 0; i < 8; i++)
        if (wave_push(&h, 0.25f, 0.5f, 0))
            return fail("before");
    hit(__LINE__);
    for (i = 0; i < 24; i++) {
        int leg = i / 6;
        float u = (float)(i % 6) / 5.0f;
        float x = (leg & 1)
            ? 0.70f - 0.45f * u
            : 0.25f + 0.45f * u;
        int got = wave_push(&h, x, 0.5f, 0.80f);
        if (got != (i == 23))
            return fail("slow");
        hit(__LINE__);
    }
    for (i = 0; i < WAVE_N; i++)
        mid[i] = 0.50f;
    wave_clear(&h);
    for (i = 0; i < WAVE_N; i++) {
        float x = i == 3 ? 0.99f : mid[i];
        float mass = i == 3 ? 0 : 0.80f;
        if (wave_push(&h, x, 0.5f, mass))
            return fail("still");
        hit(__LINE__);
    }
    if (wave_mass(0, 0.90f) != 0
        || !near(wave_mass(1, 0.90f), 0.90f))
        return fail("gate");
    hit(__LINE__);
    {
        unsigned char a[960], b[960];
        int s;
        memset(a, 40, sizeof a);
        memset(b, 40, sizeof b);
        if (frame_moved(a, b, 960))
            return fail("stillpix");
        hit(__LINE__);
        b[0] = 255;
        b[1] = 255;
        b[2] = 255;
        if (frame_moved(a, b, 960))
            return fail("speck");
        hit(__LINE__);
        for (s = 0; s < 10; s++)
            b[s * MOVE_SKIP * 3] = 255;
        if (!frame_moved(a, b, 960))
            return fail("block");
        hit(__LINE__);
    }
    {
        int pw, ph;
        preview_size(320, 240, &pw, &ph);
        if (pw != 160 || ph != 120)
            return fail("preview");
        hit(__LINE__);
        preview_size(160, 120, &pw, &ph);
        if (pw != 160 || ph != 120)
            return fail("preview1");
        hit(__LINE__);
    }
    {
        static float box[PALM_N * PALM_C];
        static float score[PALM_N];
        float ax, ay;
        Palm o;
        int i;
        anchor_at(0, &ax, &ay);
        if (!near(ax, 0.020833f) || !near(ay, 0.020833f))
            return fail("anchor0");
        hit(__LINE__);
        anchor_at(2, &ax, &ay);
        if (!near(ax, 0.0625f) || !near(ay, 0.020833f))
            return fail("anchor2");
        hit(__LINE__);
        anchor_at(48, &ax, &ay);
        if (!near(ax, 0.020833f) || !near(ay, 0.0625f))
            return fail("anchor48");
        hit(__LINE__);
        anchor_at(PALM_N - 1, &ax, &ay);
        if (!near(ax, 0.958333f) || !near(ay, 0.958333f))
            return fail("anchorN");
        hit(__LINE__);
        for (i = 0; i < PALM_N; i++)
            score[i] = -8;
        memset(box, 0, sizeof box);
        if (palm_center(box, score, PALM_S, PALM_S, &o))
            return fail("empty");
        hit(__LINE__);
        score[0] = 4;
        if (!palm_center(box, score, PALM_S, PALM_S, &o))
            return fail("palm");
        hit(__LINE__);
        if (o.conf < 0.9f || !near(o.x, 0.020833f)
            || !near(o.y, 0.020833f))
            return fail("spot");
        hit(__LINE__);
        box[2] = 0.10f * (float)PALM_S;
        box[3] = 0.20f * (float)PALM_S;
        if (!palm_center(box, score, PALM_S, PALM_S, &o))
            return fail("box");
        hit(__LINE__);
        if (!near(o.x0, -0.029167f)
            || !near(o.y0, -0.079167f)
            || !near(o.x1, 0.070833f)
            || !near(o.y1, 0.120833f))
            return fail("box");
        hit(__LINE__);
    }
    {
        unsigned char src[2 * 3 * 3], dst[3 * 2 * 3];
        int i;
        for (i = 0; i < 6; i++) {
            src[i * 3] = (unsigned char)(i + 1);
            src[i * 3 + 1] = 0;
            src[i * 3 + 2] = 0;
        }
        rot90(src, 2, 3, 1, dst);
        if (dst[0] != 5 || dst[3] != 3 || dst[6] != 1
            || dst[9] != 6 || dst[12] != 4 || dst[15] != 2)
            return fail("rot90");
        hit(__LINE__);
        rot90(src, 2, 3, 3, dst);
        if (dst[0] != 2 || dst[3] != 4 || dst[6] != 6
            || dst[9] != 1 || dst[12] != 3 || dst[15] != 5)
            return fail("rot270");
        hit(__LINE__);
    }
    {
        int linger = 0, i, n = 0;
        if (!feed_show(1, &linger) || linger != FEED_SHOW)
            return fail("show");
        hit(__LINE__);
        for (i = 0; i < FEED_SHOW; i++)
            n += feed_show(0, &linger);
        if (n != FEED_SHOW || linger != 0
            || feed_show(0, &linger))
            return fail("linger");
        hit(__LINE__);
    }
    {
        unsigned char px[3 * 3] = {
            1, 0, 0, 2, 0, 0, 3, 0, 0};
        mirror_h(px, 3, 1);
        if (px[0] != 3 || px[3] != 2 || px[6] != 1)
            return fail("mirror");
        hit(__LINE__);
    }
    if (track_cases() || palm_cases())
        return 1;
    printf("wave %d\n", checked);
    return 0;
}

int main(void)
{
    return tests();
}
