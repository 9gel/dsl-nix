/* Wave and palm decisions. The watcher calls this.
   The tests are in bao-wave-test.c. */
#include "bao-gesture.h"

#include <math.h>
#include <stddef.h>

#include "bao-feed.h"

void wave_clear(WaveHist *h)
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

/* 1 when this sample completes a wave. The window is four
   seconds at 8 fps, so a slow wave of about three seconds
   still fits. Across, back, across, back is three reversals
   on x. A sample with no palm holds the last position so
   it cannot fake a reversal. */
int wave_push(WaveHist *h, float x, float y, float mass)
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
    /* Five palm frames, not most of the window. A fast
       wave is about a second; the window is longer so a
       slow one still fits. */
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

int frame_moved(
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

float wave_mass(int moving, float palm)
{
    return moving ? palm : 0;
}

/* Frames to keep the picture up after the last motion.
   64 is about eight seconds at 8 fps. */

int feed_show(int moving, int *linger)
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
void rot90(
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
void mirror_h(unsigned char *rgb, int w, int h)
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
void preview_size(int w, int h, int *pw, int *ph)
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
void anchor_at(int i, float *ax, float *ay)
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
/* Map one raw box axis back to frame pixels. */
static float palm_px(
    float raw, float anchor, float scale, float pad)
{
    return (raw / (float)PALM_S + anchor) * scale
        - pad * scale / (float)PALM_S;
}

static float palm_logit(float logit)
{
    return 1.0f / (1.0f + expf(-logit));
}

/* Box corners are not clamped. x0 < 0 can be a real box. */
static void palm_fill(
    const float *box, int index, float conf,
    int w, int h, Palm *o)
{
    float scale, pad_x, pad_y, ax, ay;
    float rx, ry, rw, rh, cx, cy;
    const float *raw = box + index * PALM_C;
    rx = raw[0];
    ry = raw[1];
    rw = raw[2];
    rh = raw[3];
    anchor_at(index, &ax, &ay);
    palm_geom(w, h, &scale, &pad_x, &pad_y);
    cx = palm_px(rx, ax, scale, pad_x);
    cy = palm_px(ry, ay, scale, pad_y);
    o->conf = conf;
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
}

/* 1 and the frame fraction of the single best palm.
   Used by the geometry check. Tracking uses palm_list. */
int palm_center(
    const float *box, const float *score,
    int w, int h, Palm *o)
{
    int i, best = 0;
    float best_logit = score[0];
    float conf;
    o->x0 = o->y0 = o->x1 = o->y1 = -1;
    o->conf = 0;
    for (i = 1; i < PALM_N; i++) {
        if (score[i] > best_logit) {
            best = i;
            best_logit = score[i];
        }
    }
    conf = palm_logit(best_logit);
    if (conf < WAVE_MIN_MASS)
        return 0;
    palm_fill(box, best, conf, w, h, o);
    return 1;
}

static float palm_iou(const Palm *a, const Palm *b)
{
    float x0 = a->x0 > b->x0 ? a->x0 : b->x0;
    float y0 = a->y0 > b->y0 ? a->y0 : b->y0;
    float x1 = a->x1 < b->x1 ? a->x1 : b->x1;
    float y1 = a->y1 < b->y1 ? a->y1 : b->y1;
    float iw = x1 - x0;
    float ih = y1 - y0;
    float inter, ua, ub, uni;
    if (iw <= 0 || ih <= 0)
        return 0;
    inter = iw * ih;
    ua = (a->x1 - a->x0) * (a->y1 - a->y0);
    ub = (b->x1 - b->x0) * (b->y1 - b->y0);
    if (ua < 0)
        ua = 0;
    if (ub < 0)
        ub = 0;
    uni = ua + ub - inter;
    if (uni <= 0)
        return 0;
    return inter / uni;
}

typedef struct {
    int i;
    float conf;
} PalmHit;

static void hit_put(
    PalmHit *hit, int *n, int index, float conf)
{
    int at;
    if (*n >= PALM_RAW && conf <= hit[PALM_RAW - 1].conf)
        return;
    if (*n < PALM_RAW)
        at = (*n)++;
    else
        at = PALM_RAW - 1;
    while (at > 0 && conf > hit[at - 1].conf) {
        hit[at] = hit[at - 1];
        at--;
    }
    hit[at].i = index;
    hit[at].conf = conf;
}

/* Palms above PALM_TRACK_MIN, highest first, overlapping
   boxes of one hand collapsed. out holds at most max. */
int palm_list(
    const float *box, const float *score,
    int w, int h, Palm *out, int max)
{
    PalmHit hit[PALM_RAW];
    Palm decoded[PALM_RAW];
    int n = 0, i, kept = 0;
    if (max > PALM_OUT)
        max = PALM_OUT;
    if (max < 1)
        return 0;
    for (i = 0; i < PALM_N; i++) {
        float conf;
        if (score[i] < -1.0f)
            continue;
        conf = palm_logit(score[i]);
        if (conf < PALM_TRACK_MIN)
            continue;
        hit_put(hit, &n, i, conf);
    }
    for (i = 0; i < n; i++)
        palm_fill(
            box, hit[i].i, hit[i].conf,
            w, h, &decoded[i]);
    for (i = 0; i < n && kept < max; i++) {
        int k, drop = 0;
        for (k = 0; k < kept; k++) {
            if (palm_iou(&decoded[i], &out[k]) >= PALM_IOU)
                drop = 1;
        }
        if (!drop)
            out[kept++] = decoded[i];
    }
    return kept;
}

/* Stay on the hand we have. A miss, or only a far hand,
   keeps the last box for PALM_KEEP frames, then switches
   or drops. */
void palm_follow(Track *t, const Palm *c, int n)
{
    int i, near_i = -1, best = 0;
    float near_d = PALM_NEAR;
    for (i = 0; i < n; i++) {
        float dx, dy, d;
        if (c[i].conf > c[best].conf)
            best = i;
        if (!t->on)
            continue;
        dx = c[i].x - t->box.x;
        dy = c[i].y - t->box.y;
        d = sqrtf(dx * dx + dy * dy);
        if (d <= near_d) {
            near_d = d;
            near_i = i;
        }
    }
    if (near_i >= 0) {
        t->box = c[near_i];
        t->age = 0;
        t->on = 1;
        return;
    }
    if (t->on && t->age < PALM_KEEP) {
        t->age++;
        return;
    }
    if (n <= 0) {
        t->on = 0;
        return;
    }
    t->box = c[best];
    t->age = 0;
    t->on = 1;
}
