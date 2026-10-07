/* Webcam watcher for the 3D bao.

   Reads a USB camera on the CPU. A palm model finds a hand.
   A wave is that palm going across, back, across and back,
   roughly horizontally. A still
   frame does not count: the palm score is kept only when
   the picture changed. Walking past has no palm, or one
   that only translates. On a wave, sends one datagram to
   /tmp/dsl-bao-wave.sock. The same frames go into shared
   memory so the bao can show the hand. The panel is mounted
   sideways and the camera shares that mount, so each grab
   is turned upright and mirrored before the palm model
   sees it. The
   model runs on the CPU. The bao keeps the display GPU.

   ponytail: one palm, the highest score, no second-hand
   NMS. A face that bobs can still greet. A landmark model
   if that happens. --check needs no camera and no model.
*/

#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bao-feed.h"

#if !defined(DSL_BAO_HEADLESS) && !defined(DSL_BAO_PALM_TEST)
#include <errno.h>
#include <fcntl.h>
#include <jpeglib.h>
#include <linux/videodev2.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#endif

#if !defined(DSL_BAO_HEADLESS) || defined(DSL_BAO_PALM_TEST)
#include <onnxruntime_c_api.h>
#endif

#define WAVE_N 8
#define WAVE_MIN_MASS 0.50f
#define WAVE_MIN_SPAN 0.16f
#define WAVE_MIN_STEP 0.05f
#define WAVE_REVERSALS 3
#define WAVE_COOL 80
#define PALM_S 192
#define PALM_N 2016
#define PALM_C 18

typedef struct {
    float x[WAVE_N];
    float y[WAVE_N];
    float mass[WAVE_N];
    int n;
    int i;
} WaveHist;

static void wave_clear(WaveHist *h)
{
    h->n = 0;
    h->i = 0;
}

static int reversals(const float *v, int n)
{
    int count = 0, prev = 0, k;
    for (k = 1; k < n; k++) {
        float d = v[k] - v[k - 1];
        int s = d > WAVE_MIN_STEP ? 1 : d < -WAVE_MIN_STEP ? -1 : 0;
        if (s != 0 && prev != 0 && s != prev)
            count++;
        if (s != 0)
            prev = s;
    }
    return count;
}

static float span_of(const float *v, int n)
{
    float lo = v[0], hi = v[0];
    int k;
    for (k = 1; k < n; k++) {
        if (v[k] < lo)
            lo = v[k];
        if (v[k] > hi)
            hi = v[k];
    }
    return hi - lo;
}

/* 1 when this sample completes a wave. The window is one
   second at 8 fps. Across, back, across, back is three
   reversals on x. A sample with no palm holds the last
   position so it cannot fake a reversal. */
static int wave_push(WaveHist *h, float x, float y, float mass)
{
    float xs[WAVE_N], ys[WAVE_N];
    int hot = 0, k;
    int wild = mass < WAVE_MIN_MASS;
    if (wild && h->n > 0) {
        int last = (h->i + WAVE_N - 1) % WAVE_N;
        x = h->x[last];
        y = h->y[last];
    }
    h->x[h->i] = x;
    h->y[h->i] = y;
    h->mass[h->i] = mass;
    h->i = (h->i + 1) % WAVE_N;
    if (h->n < WAVE_N)
        h->n++;
    if (h->n < WAVE_N)
        return 0;
    for (k = 0; k < WAVE_N; k++) {
        int at = (h->i + k) % WAVE_N;
        xs[k] = h->x[at];
        ys[k] = h->y[at];
        if (h->mass[at] >= WAVE_MIN_MASS)
            hot++;
    }
    if (hot < 5)
        return 0;
    {
        float sxn = span_of(xs, WAVE_N);
        float syn = span_of(ys, WAVE_N);
        /* Horizontal only. A rise and fall is not a wave,
           and neither is a hand that wanders both ways. */
        if (sxn < WAVE_MIN_SPAN || syn * 2.0f > sxn)
            return 0;
        if (reversals(xs, WAVE_N) < WAVE_REVERSALS)
            return 0;
    }
    wave_clear(h);
    return 1;
}

/* Still frames score as a palm whose box hops between
   anchors. That hop is enough to fake two reversals.
   Count a pixel when any channel moves more than this. */
#define MOVE_DIFF 28
#define MOVE_SKIP 16

static int frame_moved(
    const unsigned char *a, const unsigned char *b, int n)
{
    int i, seen = 0, hot = 0;
    if (a == NULL || b == NULL || n < 3)
        return 0;
    for (i = 0; i + 2 < n; i += MOVE_SKIP * 3) {
        int c, hit = 0;
        for (c = 0; c < 3; c++) {
            int d = (int)a[i + c] - (int)b[i + c];
            if (d < 0)
                d = -d;
            if (d > MOVE_DIFF)
                hit = 1;
        }
        seen++;
        hot += hit;
    }
    /* ponytail: 8% of samples. Raise it if JPEG flicker
       keeps the preview up on a still room. */
    return seen > 0 && hot * 100 >= seen * 8;
}

static float wave_mass(int moving, float palm)
{
    return moving ? palm : 0;
}

/* Frames to keep the picture up after the last motion.
   64 is about eight seconds at 8 fps. */
#define FEED_SHOW 64

static int feed_show(int moving, int *linger)
{
    if (moving)
        *linger = FEED_SHOW;
    if (*linger <= 0)
        return 0;
    if (!moving)
        (*linger)--;
    return 1;
}

/* Turn 3 stands the grab up: the webcam's top is the
   viewer's left. Turn 1 is the other quarter turn.
   dst is h by w. */
static void rot90(
    const unsigned char *src, int w, int h, int turn,
    unsigned char *dst)
{
    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            int ox, oy;
            const unsigned char *s =
                src + ((size_t)y * (size_t)w + (size_t)x) * 3;
            unsigned char *d;
            if (turn == 1) {
                ox = (h - 1) - y;
                oy = x;
            } else {
                ox = y;
                oy = (w - 1) - x;
            }
            d = dst + ((size_t)oy * (size_t)h + (size_t)ox) * 3;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
}

/* Left-right mirror, so the preview matches a mirror. */
static void mirror_h(unsigned char *rgb, int w, int h)
{
    int y, x;
    if (w < 2)
        return;
    for (y = 0; y < h; y++) {
        unsigned char *row =
            rgb + ((size_t)y * (size_t)w) * 3;
        for (x = 0; x < w / 2; x++) {
            int r = w - 1 - x;
            int c;
            for (c = 0; c < 3; c++) {
                unsigned char t = row[x * 3 + c];
                row[x * 3 + c] = row[r * 3 + c];
                row[r * 3 + c] = t;
            }
        }
    }
}

/* Longest edge of the preview is BAO_FEED_W. */
static void preview_size(int w, int h, int *pw, int *ph)
{
    int d = 1;
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    while (w / d > BAO_FEED_W || h / d > BAO_FEED_H)
        d++;
    *pw = w / d;
    *ph = h / d;
    if (*pw < 1)
        *pw = 1;
    if (*ph < 1)
        *ph = 1;
}

/* MediaPipe SSD anchors: one 24x24 layer, then three
   12x12 layers. Two anchors share each cell. */
static void anchor_at(int i, float *ax, float *ay)
{
    int cells, n, x, y;
    if (i < 24 * 24 * 2) {
        cells = 24;
        n = i / 2;
    } else {
        cells = 12;
        n = ((i - 24 * 24 * 2) / 2) % (12 * 12);
    }
    x = n % cells;
    y = n / cells;
    *ax = ((float)x + 0.5f) / (float)cells;
    *ay = ((float)y + 0.5f) / (float)cells;
}

static void palm_geom(
    int w, int h, float *scale, float *pad_x, float *pad_y)
{
    float ratio;
    int nw, nh;
    *scale = (float)(w > h ? w : h);
    ratio = (float)PALM_S / *scale;
    nw = (int)((float)w * ratio);
    nh = (int)((float)h * ratio);
    if (nw > PALM_S)
        nw = PALM_S;
    if (nh > PALM_S)
        nh = PALM_S;
    if (nw < 1)
        nw = 1;
    if (nh < 1)
        nh = 1;
    *pad_x = (float)((PALM_S - nw) / 2);
    *pad_y = (float)((PALM_S - nh) / 2);
}

static int near(float a, float b)
{
    float d = a - b;
    if (d < 0)
        d = -d;
    return d < 0.001f;
}

typedef struct {
    float x, y, conf;
    float x0, y0, x1, y1;
} Palm;

/* Map one raw box axis back to frame pixels. */
static float palm_px(
    float raw, float anchor, float scale, float pad)
{
    return (raw / (float)PALM_S + anchor) * scale
        - pad * scale / (float)PALM_S;
}

/* box is PALM_N * PALM_C, x,y,w,h first. score is PALM_N
   logits. 1 and the frame fraction of the best palm.
   The box corners are not clamped. x0 < 0 means none. */
static int palm_center(
    const float *box, const float *score,
    int w, int h, Palm *o)
{
    int i, best = 0;
    float best_logit = score[0];
    float scale, pad_x, pad_y, ax, ay;
    float rx, ry, rw, rh, cx, cy;
    o->x0 = o->y0 = o->x1 = o->y1 = -1;
    o->conf = 0;
    for (i = 1; i < PALM_N; i++) {
        if (score[i] > best_logit) {
            best = i;
            best_logit = score[i];
        }
    }
    o->conf = 1.0f / (1.0f + expf(-best_logit));
    if (o->conf < WAVE_MIN_MASS)
        return 0;
    rx = box[best * PALM_C];
    ry = box[best * PALM_C + 1];
    rw = box[best * PALM_C + 2];
    rh = box[best * PALM_C + 3];
    anchor_at(best, &ax, &ay);
    palm_geom(w, h, &scale, &pad_x, &pad_y);
    cx = palm_px(rx, ax, scale, pad_x);
    cy = palm_px(ry, ay, scale, pad_y);
    o->x = w > 0 ? cx / (float)w : 0;
    o->y = h > 0 ? cy / (float)h : 0;
    if (o->x < 0)
        o->x = 0;
    if (o->x > 1)
        o->x = 1;
    if (o->y < 0)
        o->y = 0;
    if (o->y > 1)
        o->y = 1;
    o->x0 = w > 0
        ? palm_px(rx - rw * 0.5f, ax, scale, pad_x) / (float)w
        : 0;
    o->y0 = h > 0
        ? palm_px(ry - rh * 0.5f, ay, scale, pad_y) / (float)h
        : 0;
    o->x1 = w > 0
        ? palm_px(rx + rw * 0.5f, ax, scale, pad_x) / (float)w
        : 0;
    o->y1 = h > 0
        ? palm_px(ry + rh * 0.5f, ay, scale, pad_y) / (float)h
        : 0;
    return 1;
}

static int fail(const char *msg)
{
    fprintf(stderr, "dsl-bao-wave: %s\n", msg);
    return 1;
}

static int push_all(
    WaveHist *h, const float *x, const float *y, float mass)
{
    int i, got = 0;
    wave_clear(h);
    for (i = 0; i < WAVE_N; i++)
        got = wave_push(
            h, x ? x[i] : 0.5f, y ? y[i] : 0.5f, mass);
    return got;
}

static int check(void)
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
        int got = wave_push(&h, wave[i], 0.5f, 0.80f);
        if (got != (i == WAVE_N - 1))
            return fail("wave");
    }
    if (wave_push(&h, 0.40f, 0.5f, 0.80f))
        return fail("cleared");
    if (push_all(&h, walk, NULL, 0.80f))
        return fail("walk");
    if (push_all(&h, jitter, NULL, 0.80f))
        return fail("jitter");
    if (push_all(&h, flick, NULL, 0.80f))
        return fail("flick");
    if (push_all(&h, swipe, NULL, 0.80f))
        return fail("swipe");
    if (push_all(&h, hello, NULL, 0.80f))
        return fail("hello");
    if (!push_all(&h, full, NULL, 0.80f))
        return fail("full");
    if (push_all(&h, wave, NULL, 0))
        return fail("nobody");
    if (push_all(&h, NULL, full, 0.80f))
        return fail("sideways");
    for (i = 0; i < WAVE_N; i++)
        mid[i] = 0.50f;
    wave_clear(&h);
    for (i = 0; i < WAVE_N; i++) {
        float x = i == 3 ? 0.99f : mid[i];
        float mass = i == 3 ? 0 : 0.80f;
        if (wave_push(&h, x, 0.5f, mass))
            return fail("still");
    }
    if (wave_mass(0, 0.90f) != 0
        || !near(wave_mass(1, 0.90f), 0.90f))
        return fail("gate");
    {
        unsigned char a[960], b[960];
        int s;
        memset(a, 40, sizeof a);
        memset(b, 40, sizeof b);
        if (frame_moved(a, b, 960))
            return fail("stillpix");
        b[0] = 255;
        b[1] = 255;
        b[2] = 255;
        if (frame_moved(a, b, 960))
            return fail("speck");
        for (s = 0; s < 10; s++)
            b[s * MOVE_SKIP * 3] = 255;
        if (!frame_moved(a, b, 960))
            return fail("block");
    }
    {
        int pw, ph;
        preview_size(320, 240, &pw, &ph);
        if (pw != 160 || ph != 120)
            return fail("preview");
        preview_size(160, 120, &pw, &ph);
        if (pw != 160 || ph != 120)
            return fail("preview1");
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
        anchor_at(2, &ax, &ay);
        if (!near(ax, 0.0625f) || !near(ay, 0.020833f))
            return fail("anchor2");
        anchor_at(48, &ax, &ay);
        if (!near(ax, 0.020833f) || !near(ay, 0.0625f))
            return fail("anchor48");
        anchor_at(PALM_N - 1, &ax, &ay);
        if (!near(ax, 0.958333f) || !near(ay, 0.958333f))
            return fail("anchorN");
        for (i = 0; i < PALM_N; i++)
            score[i] = -8;
        memset(box, 0, sizeof box);
        if (palm_center(box, score, PALM_S, PALM_S, &o))
            return fail("empty");
        score[0] = 4;
        if (!palm_center(box, score, PALM_S, PALM_S, &o))
            return fail("palm");
        if (o.conf < 0.9f || !near(o.x, 0.020833f)
            || !near(o.y, 0.020833f))
            return fail("spot");
        box[2] = 0.10f * (float)PALM_S;
        box[3] = 0.20f * (float)PALM_S;
        if (!palm_center(box, score, PALM_S, PALM_S, &o))
            return fail("box");
        if (!near(o.x0, -0.029167f)
            || !near(o.y0, -0.079167f)
            || !near(o.x1, 0.070833f)
            || !near(o.y1, 0.120833f))
            return fail("box");
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
        rot90(src, 2, 3, 3, dst);
        if (dst[0] != 2 || dst[3] != 4 || dst[6] != 6
            || dst[9] != 1 || dst[12] != 3 || dst[15] != 5)
            return fail("rot270");
    }
    {
        int linger = 0, i, n = 0;
        if (!feed_show(1, &linger) || linger != FEED_SHOW)
            return fail("show");
        for (i = 0; i < FEED_SHOW; i++)
            n += feed_show(0, &linger);
        if (n != FEED_SHOW || linger != 0
            || feed_show(0, &linger))
            return fail("linger");
    }
    {
        unsigned char px[3 * 3] = {
            1, 0, 0, 2, 0, 0, 3, 0, 0};
        mirror_h(px, 3, 1);
        if (px[0] != 3 || px[3] != 2 || px[6] != 1)
            return fail("mirror");
    }
    printf("ok\n");
    return 0;
}

#if !defined(DSL_BAO_HEADLESS) || defined(DSL_BAO_PALM_TEST)

#ifndef DSL_BAO_PALM
#define DSL_BAO_PALM "palm.onnx"
#endif

typedef struct {
    const OrtApi *api;
    OrtEnv *env;
    OrtSession *ses;
    OrtMemoryInfo *mem;
    OrtValue *input;
    float *in;
} PalmRt;

static int ort_ok(const OrtApi *api, OrtStatus *st)
{
    if (st == NULL)
        return 1;
    fprintf(stderr, "dsl-bao-wave: %s\n",
        api->GetErrorMessage(st));
    api->ReleaseStatus(st);
    return 0;
}

static void letterbox(
    const unsigned char *rgb, int w, int h, float *out)
{
    float scale, pad_x, pad_y, ratio;
    int nw, nh, y, x;
    palm_geom(w, h, &scale, &pad_x, &pad_y);
    ratio = (float)PALM_S / scale;
    nw = (int)((float)w * ratio);
    nh = (int)((float)h * ratio);
    if (nw > PALM_S)
        nw = PALM_S;
    if (nh > PALM_S)
        nh = PALM_S;
    if (nw < 1)
        nw = 1;
    if (nh < 1)
        nh = 1;
    memset(out, 0, (size_t)PALM_S * PALM_S * 3 * sizeof(float));
    for (y = 0; y < nh; y++) {
        float sy = ((float)y + 0.5f) * (float)h / (float)nh;
        int y0, y1;
        float fy;
        sy -= 0.5f;
        if (sy < 0)
            sy = 0;
        if (sy > (float)(h - 1))
            sy = (float)(h - 1);
        y0 = (int)sy;
        y1 = y0 + 1 < h ? y0 + 1 : y0;
        fy = sy - (float)y0;
        for (x = 0; x < nw; x++) {
            float sx =
                ((float)x + 0.5f) * (float)w / (float)nw;
            int x0, x1, c;
            float fx;
            float *d;
            sx -= 0.5f;
            if (sx < 0)
                sx = 0;
            if (sx > (float)(w - 1))
                sx = (float)(w - 1);
            x0 = (int)sx;
            x1 = x0 + 1 < w ? x0 + 1 : x0;
            fx = sx - (float)x0;
            d = out + (((int)pad_y + y) * PALM_S
                + ((int)pad_x + x)) * 3;
            for (c = 0; c < 3; c++) {
                float v00 = rgb[(y0 * w + x0) * 3 + c];
                float v01 = rgb[(y0 * w + x1) * 3 + c];
                float v10 = rgb[(y1 * w + x0) * 3 + c];
                float v11 = rgb[(y1 * w + x1) * 3 + c];
                float v = v00 * (1 - fx) * (1 - fy)
                    + v01 * fx * (1 - fy)
                    + v10 * (1 - fx) * fy
                    + v11 * fx * fy;
                d[c] = v / 255.0f;
            }
        }
    }
}

static int palm_open(PalmRt *p, const char *path)
{
    OrtSessionOptions *opt = NULL;
    int64_t dims[4];
    size_t nbytes;
    const OrtApiBase *base = OrtGetApiBase();
    memset(p, 0, sizeof *p);
    if (base == NULL)
        return 0;
    p->api = base->GetApi(ORT_API_VERSION);
    if (p->api == NULL)
        return 0;
    if (!ort_ok(p->api, p->api->CreateEnv(
            ORT_LOGGING_LEVEL_ERROR, "dsl-bao-wave",
            &p->env)))
        return 0;
    if (!ort_ok(p->api, p->api->CreateSessionOptions(&opt)))
        return 0;
    if (!ort_ok(p->api,
            p->api->SetIntraOpNumThreads(opt, 2))) {
        p->api->ReleaseSessionOptions(opt);
        return 0;
    }
    if (!ort_ok(p->api,
            p->api->SetSessionGraphOptimizationLevel(
                opt, ORT_ENABLE_ALL))) {
        p->api->ReleaseSessionOptions(opt);
        return 0;
    }
    if (!ort_ok(p->api, p->api->CreateSession(
            p->env, path, opt, &p->ses))) {
        p->api->ReleaseSessionOptions(opt);
        return 0;
    }
    p->api->ReleaseSessionOptions(opt);
    if (!ort_ok(p->api, p->api->CreateCpuMemoryInfo(
            OrtArenaAllocator, OrtMemTypeDefault, &p->mem)))
        return 0;
    nbytes = (size_t)PALM_S * PALM_S * 3 * sizeof(float);
    p->in = calloc(1, nbytes);
    if (p->in == NULL)
        return 0;
    dims[0] = 1;
    dims[1] = PALM_S;
    dims[2] = PALM_S;
    dims[3] = 3;
    if (!ort_ok(p->api,
            p->api->CreateTensorWithDataAsOrtValue(
                p->mem, p->in, nbytes, dims, 4,
                ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                &p->input)))
        return 0;
    fprintf(stderr, "dsl-bao-wave: palm model\n");
    return 1;
}

/* conf is the palm score, or 0 when the frame has no palm. */
static void palm_run(
    PalmRt *p, const unsigned char *rgb, int w, int h,
    Palm *o)
{
    OrtValue *out[2] = { NULL, NULL };
    const char *const in_name[] = { "input_1" };
    const char *const out_name[] = {
        "Identity", "Identity_1"
    };
    const OrtValue *const in_val[] = { p->input };
    float *box = NULL, *score = NULL;
    o->x = 0;
    o->y = 0;
    o->conf = 0;
    o->x0 = o->y0 = o->x1 = o->y1 = -1;
    letterbox(rgb, w, h, p->in);
    if (!ort_ok(p->api, p->api->Run(
            p->ses, NULL, in_name, in_val, 1,
            out_name, 2, out)))
        return;
    if (!ort_ok(p->api, p->api->GetTensorMutableData(
            out[0], (void **)&box))
        || !ort_ok(p->api, p->api->GetTensorMutableData(
            out[1], (void **)&score))) {
        p->api->ReleaseValue(out[0]);
        p->api->ReleaseValue(out[1]);
        return;
    }
    if (!palm_center(box, score, w, h, o)) {
        o->conf = 0;
        o->x0 = o->y0 = o->x1 = o->y1 = -1;
    }
    p->api->ReleaseValue(out[0]);
    p->api->ReleaseValue(out[1]);
}

#endif

#if !defined(DSL_BAO_HEADLESS) && !defined(DSL_BAO_PALM_TEST)

enum { CAM_BUFS = 2 };

typedef struct {
    unsigned char *rgb;
    int w, h, n;
} Frame;

typedef struct {
    void *p;
    size_t n;
} Map;

typedef struct {
    int fd;
    int w, h, stride;
    unsigned pix;
    Map map[CAM_BUFS];
    int nbuf;
} Cam;

struct jdec {
    struct jpeg_error_mgr base;
    jmp_buf jmp;
};

static void jdie(j_common_ptr c)
{
    struct jdec *e = (struct jdec *)c->err;
    longjmp(e->jmp, 1);
}

static int frame_fit(Frame *f, int w, int h)
{
    int n = w * h;
    unsigned char *rgb;
    if (w < 16 || h < 12 || w > 640 || h > 480)
        return 0;
    if (f->n == n && f->w == w && f->h == h)
        return 1;
    rgb = malloc((size_t)n * 3);
    if (rgb == NULL)
        return 0;
    free(f->rgb);
    f->rgb = rgb;
    f->n = n;
    f->w = w;
    f->h = h;
    return 1;
}

static int copy_rgb(
    Frame *f, struct jpeg_decompress_struct *cinfo,
    unsigned char *volatile *rowp)
{
    int w, h, y;
    unsigned char *row;
    w = (int)cinfo->output_width;
    h = (int)cinfo->output_height;
    if (!frame_fit(f, w, h))
        return 0;
    row = malloc((size_t)w * 3);
    if (row == NULL)
        return 0;
    *rowp = row;
    for (y = 0; y < h; y++) {
        JSAMPROW line = row;
        if (jpeg_read_scanlines(cinfo, &line, 1) != 1)
            return 0;
        memcpy(
            f->rgb + (size_t)y * (size_t)w * 3,
            row, (size_t)w * 3);
    }
    return 1;
}

static int decode_mjpg(
    Frame *f, const unsigned char *jpg, unsigned len)
{
    struct jpeg_decompress_struct *cinfo;
    struct jpeg_decompress_struct *volatile live = NULL;
    struct jdec err;
    unsigned char *volatile row = NULL;
    volatile int ready = 0;
    int ok = 0;
    if (len < 4)
        return 0;
    cinfo = calloc(1, sizeof *cinfo);
    if (cinfo == NULL)
        return 0;
    cinfo->err = jpeg_std_error(&err.base);
    err.base.error_exit = jdie;
    if (setjmp(err.jmp)) {
        if (ready && live != NULL)
            jpeg_destroy_decompress(
                (struct jpeg_decompress_struct *)live);
        free((void *)live);
        free((void *)row);
        return 0;
    }
    live = cinfo;
    jpeg_create_decompress(cinfo);
    ready = 1;
    jpeg_mem_src(cinfo, (unsigned char *)jpg, len);
    if (jpeg_read_header(cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(cinfo);
        free(cinfo);
        return 0;
    }
    cinfo->out_color_space = JCS_RGB;
    if (cinfo->image_width > 640
        || cinfo->image_height > 480) {
        cinfo->scale_num = 1;
        cinfo->scale_denom = 2;
        if (cinfo->image_width > 1280
            || cinfo->image_height > 960)
            cinfo->scale_denom = 4;
    }
    jpeg_start_decompress(cinfo);
    ok = copy_rgb(f, cinfo, &row);
    jpeg_finish_decompress(cinfo);
    jpeg_destroy_decompress(cinfo);
    free(cinfo);
    free((void *)row);
    return ok;
}

static int clamp_u8(int v)
{
    if (v < 0)
        return 0;
    if (v > 255)
        return 255;
    return v;
}

static int take_yuyv(
    Frame *f, const unsigned char *p, int len,
    int sw, int sh, int stride)
{
    int x, y;
    if (stride < sw * 2)
        stride = sw * 2;
    if (sw < 16 || sh < 12 || len < stride * sh)
        return 0;
    if (!frame_fit(f, sw, sh))
        return 0;
    for (y = 0; y < sh; y++) {
        const unsigned char *src =
            p + (size_t)y * (size_t)stride;
        for (x = 0; x < sw; x++) {
            int yy = src[x * 2];
            int u = src[(x & ~1) * 2 + 1];
            int v = src[(x & ~1) * 2 + 3];
            int d = u - 128;
            int e = v - 128;
            unsigned char *o = f->rgb
                + ((size_t)y * (size_t)sw + (size_t)x) * 3;
            o[0] = (unsigned char)clamp_u8(
                yy + ((359 * e) >> 8));
            o[1] = (unsigned char)clamp_u8(
                yy - ((88 * d + 183 * e) >> 8));
            o[2] = (unsigned char)clamp_u8(
                yy + ((454 * d) >> 8));
        }
    }
    return 1;
}

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r < 0 && errno == EINTR);
    return r;
}

static void cam_unmap(Cam *c)
{
    int i;
    for (i = 0; i < c->nbuf; i++) {
        if (c->map[i].p != NULL && c->map[i].p != MAP_FAILED)
            munmap(c->map[i].p, c->map[i].n);
        c->map[i].p = NULL;
    }
    c->nbuf = 0;
}

static void cam_close(Cam *c)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (c->fd < 0)
        return;
    xioctl(c->fd, VIDIOC_STREAMOFF, &type);
    cam_unmap(c);
    close(c->fd);
    c->fd = -1;
}

static int cam_mmap(int fd, Cam *c)
{
    struct v4l2_requestbuffers req;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int i;
    memset(&req, 0, sizeof req);
    req.count = CAM_BUFS;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 1)
        return 0;
    if (req.count > CAM_BUFS)
        req.count = CAM_BUFS;
    for (i = 0; i < (int)req.count; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof buf);
        buf.type = type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = (unsigned)i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0)
            return 0;
        c->map[i].n = buf.length;
        c->map[i].p = mmap(
            NULL, buf.length, PROT_READ | PROT_WRITE,
            MAP_SHARED, fd, (off_t)buf.m.offset);
        if (c->map[i].p == MAP_FAILED)
            return 0;
        c->nbuf++;
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0)
            return 0;
    }
    return xioctl(fd, VIDIOC_STREAMON, &type) == 0;
}

static int cam_fmt(int fd, unsigned pix, Cam *c)
{
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = 320;
    fmt.fmt.pix.height = 240;
    fmt.fmt.pix.pixelformat = pix;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0)
        return 0;
    if (fmt.fmt.pix.pixelformat != pix)
        return 0;
    if (fmt.fmt.pix.width < 16 || fmt.fmt.pix.height < 12)
        return 0;
    c->w = (int)fmt.fmt.pix.width;
    c->h = (int)fmt.fmt.pix.height;
    c->stride = (int)fmt.fmt.pix.bytesperline;
    c->pix = pix;
    return 1;
}

static int cam_open_path(Cam *c, const char *path)
{
    struct v4l2_capability cap;
    unsigned caps;
    int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return 0;
    memset(&cap, 0, sizeof cap);
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        close(fd);
        return 0;
    }
    caps = cap.capabilities;
    if (caps & V4L2_CAP_DEVICE_CAPS)
        caps = cap.device_caps;
    if (!(caps & V4L2_CAP_VIDEO_CAPTURE)) {
        close(fd);
        return 0;
    }
    if (!cam_fmt(fd, V4L2_PIX_FMT_MJPEG, c)
        && !cam_fmt(fd, V4L2_PIX_FMT_YUYV, c)) {
        close(fd);
        return 0;
    }
    if (!cam_mmap(fd, c)) {
        cam_unmap(c);
        close(fd);
        return 0;
    }
    c->fd = fd;
    fprintf(
        stderr, "dsl-bao-wave: %s %s %dx%d\n", path,
        c->pix == V4L2_PIX_FMT_MJPEG ? "mjpeg" : "yuyv",
        c->w, c->h);
    return 1;
}

static int cam_open(Cam *c)
{
    const char *env = getenv("DSL_BAO_CAMERA");
    char path[64];
    int i;
    memset(c, 0, sizeof *c);
    c->fd = -1;
    if (env != NULL && env[0] != '\0')
        return cam_open_path(c, env);
    for (i = 0; i < 10; i++) {
        snprintf(path, sizeof path, "/dev/video%d", i);
        if (cam_open_path(c, path))
            return 1;
    }
    return 0;
}

/* 1 = a frame is in the rgb buffer. 0 = skip. -1 = reopen. */
static int cam_grab(Cam *c, Frame *f)
{
    struct v4l2_buffer buf;
    fd_set fds;
    struct timeval tv;
    const unsigned char *p;
    unsigned len;
    int r, got = 0;
    FD_ZERO(&fds);
    FD_SET(c->fd, &fds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    r = select(c->fd + 1, &fds, NULL, NULL, &tv);
    if (r < 0)
        return errno == EINTR ? 0 : -1;
    if (r == 0)
        return 0;
    memset(&buf, 0, sizeof buf);
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    if (xioctl(c->fd, VIDIOC_DQBUF, &buf) < 0)
        return errno == EAGAIN ? 0 : -1;
    if (buf.index < (unsigned)c->nbuf) {
        p = c->map[buf.index].p;
        len = buf.bytesused;
        if (len == 0)
            len = (unsigned)c->map[buf.index].n;
        if (c->pix == V4L2_PIX_FMT_MJPEG)
            got = decode_mjpg(f, p, len);
        else
            got = take_yuyv(
                f, p, (int)len, c->w, c->h, c->stride);
    }
    if (xioctl(c->fd, VIDIOC_QBUF, &buf) < 0)
        return -1;
    return got ? 1 : 0;
}

static const char *wave_path(void)
{
    const char *v = getenv("DSL_BAO_WAVE");
    if (v != NULL && v[0] != '\0')
        return v;
    return "/tmp/dsl-bao-wave.sock";
}

static void wave_send(void)
{
    struct sockaddr_un addr;
    const char *path = wave_path();
    size_t n = strlen(path);
    int fd;
    if (n >= sizeof addr.sun_path)
        return;
    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, path, n + 1);
    (void)sendto(
        fd, "1", 1, 0, (struct sockaddr *)&addr, sizeof addr);
    close(fd);
}

/* About two seconds at 8 fps, long enough to read "wave". */
#define FEED_HOLD 16

static void preview_rgb(
    const unsigned char *src, int w, int h,
    unsigned char *dst, int pw, int ph)
{
    int y, x;
    for (y = 0; y < ph; y++) {
        int sy = (y * h) / ph;
        const unsigned char *row =
            src + (size_t)sy * (size_t)w * 3;
        unsigned char *out = dst + (size_t)y * (size_t)pw * 3;
        for (x = 0; x < pw; x++) {
            int sx = (x * w) / pw;
            const unsigned char *s = row + sx * 3;
            unsigned char *d = out + x * 3;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
        }
    }
}

static int keep_prev(
    unsigned char **prev, int *pn,
    const unsigned char *rgb, int n)
{
    if (*pn != n) {
        unsigned char *p = malloc((size_t)n);
        if (p == NULL)
            return 0;
        free(*prev);
        *prev = p;
        *pn = n;
    }
    memcpy(*prev, rgb, (size_t)n);
    return 1;
}

static BaoFeed *feed_open(void)
{
    int fd = shm_open(
        BAO_FEED_NAME, O_CREAT | O_RDWR, 0600);
    BaoFeed *f;
    if (fd < 0)
        return NULL;
    if (ftruncate(fd, (off_t)sizeof(BaoFeed)) < 0) {
        close(fd);
        return NULL;
    }
    f = mmap(
        NULL, sizeof(BaoFeed), PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, 0);
    close(fd);
    if (f == MAP_FAILED)
        return NULL;
    atomic_store_explicit(
        (atomic_uint *)&f->seq, 0, memory_order_relaxed);
    f->flags = 0;
    f->w = 0;
    f->h = 0;
    f->x0 = f->y0 = f->x1 = f->y1 = -1;
    return f;
}

static void feed_publish(
    BaoFeed *f, int w, int h, const unsigned char *rgb,
    float x0, float y0, float x1, float y1,
    float score, unsigned flags)
{
    atomic_uint *seq;
    uint32_t s;
    int pw = 0, ph = 0;
    static unsigned char pix[BAO_FEED_W * BAO_FEED_H * 3];
    if (f == NULL)
        return;
    seq = (atomic_uint *)&f->seq;
    s = atomic_load_explicit(seq, memory_order_relaxed);
    if (s & 1u)
        s++;
    atomic_store_explicit(seq, s + 1, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    if (rgb != NULL && w > 0 && h > 0) {
        preview_size(w, h, &pw, &ph);
        preview_rgb(rgb, w, h, pix, pw, ph);
        f->w = pw;
        f->h = ph;
        memcpy(f->rgb, pix, (size_t)pw * (size_t)ph * 3);
    }
    f->x0 = x0;
    f->y0 = y0;
    f->x1 = x1;
    f->y1 = y1;
    f->score = score;
    f->flags = flags;
    atomic_store_explicit(seq, s + 2, memory_order_release);
}

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sleep_s(double s)
{
    struct timespec req;
    if (s <= 0)
        return;
    req.tv_sec = (time_t)s;
    req.tv_nsec = (long)((s - (double)req.tv_sec) * 1e9);
    nanosleep(&req, NULL);
}

static int camera_turn(void)
{
    const char *v = getenv("DSL_BAO_CAMERA_TURN");
    int t = 3;
    if (v != NULL && v[0] != '\0')
        t = atoi(v);
    return t == 3 ? 3 : 1;
}

static int frame_upright(const Frame *src, Frame *dst, int turn)
{
    int n;
    if (src->rgb == NULL || src->w < 1 || src->h < 1)
        return 0;
    n = src->w * src->h;
    if (dst->n != n) {
        unsigned char *p = malloc((size_t)n * 3);
        if (p == NULL)
            return 0;
        free(dst->rgb);
        dst->rgb = p;
        dst->n = n;
    }
    dst->w = src->h;
    dst->h = src->w;
    rot90(src->rgb, src->w, src->h, turn, dst->rgb);
    mirror_h(dst->rgb, dst->w, dst->h);
    return 1;
}

static int watch(void)
{
    Cam cam;
    Frame frame, upright;
    PalmRt palm;
    WaveHist hist;
    BaoFeed *feed;
    int turn;
    unsigned char *prev = NULL;
    int prev_n = 0;
    int cool = 0, said = 0, hold = 0, have = 0, linger = 0;
    float bx0 = -1, by0 = -1, bx1 = -1, by1 = -1;
    memset(&cam, 0, sizeof cam);
    memset(&frame, 0, sizeof frame);
    memset(&upright, 0, sizeof upright);
    memset(&palm, 0, sizeof palm);
    memset(&hist, 0, sizeof hist);
    cam.fd = -1;
    turn = camera_turn();
    feed = feed_open();
    if (feed == NULL)
        fprintf(stderr, "dsl-bao-wave: no preview\n");
    if (!palm_open(&palm, DSL_BAO_PALM))
        return fail("no palm model");
    for (;;) {
        double t0, left;
        int got;
        if (cam.fd < 0) {
            if (!cam_open(&cam)) {
                if (!said) {
                    fprintf(stderr, "dsl-bao-wave: no camera\n");
                    said = 1;
                }
                sleep_s(2);
                continue;
            }
            said = 0;
            wave_clear(&hist);
            prev_n = 0;
        }
        t0 = mono();
        got = cam_grab(&cam, &frame);
        if (got < 0) {
            cam_close(&cam);
            continue;
        }
        if (got > 0) {
            Palm seen = {0};
            int n, moving = 0, show;
            unsigned flags = 0;
            float mass;
            if (!frame_upright(&frame, &upright, turn))
                continue;
            n = upright.w * upright.h * 3;
            if (prev != NULL && prev_n == n)
                moving = frame_moved(prev, upright.rgb, n);
            show = feed_show(moving, &linger);
            seen.x0 = seen.y0 = seen.x1 = seen.y1 = -1;
            if (show)
                palm_run(
                    &palm, upright.rgb, upright.w, upright.h,
                    &seen);
            mass = wave_mass(moving, seen.conf);
            if (cool > 0)
                cool--;
            else if (wave_push(
                    &hist, seen.x, seen.y, mass)) {
                wave_send();
                fprintf(stderr, "dsl-bao-wave: palm\n");
                cool = WAVE_COOL;
                hold = FEED_HOLD;
            }
            if (show)
                flags |= BAO_FEED_MOVE;
            if (seen.conf >= WAVE_MIN_MASS) {
                bx0 = seen.x0;
                by0 = seen.y0;
                bx1 = seen.x1;
                by1 = seen.y1;
                have = 1;
                flags |= BAO_FEED_PALM;
            }
            if (hold > 0) {
                flags |= BAO_FEED_WAVE;
                if ((flags & BAO_FEED_PALM) == 0 && have) {
                    seen.x0 = bx0;
                    seen.y0 = by0;
                    seen.x1 = bx1;
                    seen.y1 = by1;
                    flags |= BAO_FEED_PALM;
                }
                hold--;
            }
            feed_publish(
                feed, upright.w, upright.h,
                (flags & BAO_FEED_MOVE) ? upright.rgb : NULL,
                seen.x0, seen.y0, seen.x1, seen.y1,
                mass, flags);
            (void)keep_prev(&prev, &prev_n, upright.rgb, n);
        }
        left = 0.125 - (mono() - t0);
        sleep_s(left);
    }
}

#endif

#ifdef DSL_BAO_PALM_TEST
int main(int argc, char **argv)
{
    PalmRt palm;
    unsigned char *rgb;
    FILE *fp;
    int w, h;
    long n;
    Palm seen;
    if (check() != 0)
        return 1;
    if (argc != 5)
        return fail("usage: model rgb w h");
    w = atoi(argv[3]);
    h = atoi(argv[4]);
    if (w < 1 || h < 1)
        return fail("size");
    n = (long)w * (long)h * 3;
    rgb = malloc((size_t)n);
    fp = fopen(argv[2], "rb");
    if (rgb == NULL || fp == NULL)
        return fail("rgb");
    if (fread(rgb, 1, (size_t)n, fp) != (size_t)n) {
        fclose(fp);
        return fail("short");
    }
    fclose(fp);
    if (!palm_open(&palm, argv[1]))
        return fail("open");
    palm_run(&palm, rgb, w, h, &seen);
    printf("%.4f %.4f %.4f\n", seen.conf, seen.x, seen.y);
    free(rgb);
    return seen.conf >= WAVE_MIN_MASS ? 0 : 2;
}
#else
int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--check") == 0)
        return check();
#if !defined(DSL_BAO_HEADLESS)
    (void)argc;
    (void)argv;
    return watch();
#else
    (void)argc;
    (void)argv;
    fprintf(stderr, "dsl-bao-wave: no camera on this os\n");
    return 1;
#endif
}
#endif
