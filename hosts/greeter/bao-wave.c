/* Webcam watcher for the 3D bao.

   Reads a USB camera on the CPU. A palm model finds a hand.
   A wave is that palm reversing twice on one axis. A person
   walking past has no palm, or a palm that only translates.
   On a wave, sends one datagram to /tmp/dsl-bao-wave.sock.
   dsl-bao-3d owns the display and binds that socket. The
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

#if !defined(DSL_BAO_HEADLESS) && !defined(DSL_BAO_PALM_TEST)
#include <errno.h>
#include <fcntl.h>
#include <jpeglib.h>
#include <linux/videodev2.h>
#include <setjmp.h>
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
#define WAVE_REVERSALS 2
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
   second at 8 fps. A sample with no palm holds the last
   position so it cannot fake a reversal. */
static int wave_push(WaveHist *h, float x, float y, float mass)
{
    float xs[WAVE_N], ys[WAVE_N];
    int hot = 0, k, along_x;
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
        float major, minor;
        along_x = sxn >= syn;
        major = along_x ? sxn : syn;
        minor = along_x ? syn : sxn;
        /* A wave is one axis. A blob that wanders both ways
           is someone shifting, not a hand. */
        if (major < WAVE_MIN_SPAN || minor * 2.0f > major)
            return 0;
        if (reversals(along_x ? xs : ys, WAVE_N) < WAVE_REVERSALS)
            return 0;
    }
    wave_clear(h);
    return 1;
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

/* box is PALM_N * PALM_C, x,y,w,h first. score is PALM_N
   logits. 1 and the frame fraction of the best palm. */
static int palm_center(
    const float *box, const float *score,
    int w, int h, float *x, float *y, float *conf)
{
    int i, best = 0;
    float best_logit = score[0];
    float scale, pad_x, pad_y, dx, dy, ax, ay, cx, cy;
    for (i = 1; i < PALM_N; i++) {
        if (score[i] > best_logit) {
            best = i;
            best_logit = score[i];
        }
    }
    *conf = 1.0f / (1.0f + expf(-best_logit));
    if (*conf < WAVE_MIN_MASS)
        return 0;
    dx = box[best * PALM_C] / (float)PALM_S;
    dy = box[best * PALM_C + 1] / (float)PALM_S;
    anchor_at(best, &ax, &ay);
    palm_geom(w, h, &scale, &pad_x, &pad_y);
    cx = (dx + ax) * scale - pad_x / (PALM_S / scale);
    cy = (dy + ay) * scale - pad_y / (PALM_S / scale);
    *x = w > 0 ? cx / (float)w : 0;
    *y = h > 0 ? cy / (float)h : 0;
    if (*x < 0)
        *x = 0;
    if (*x > 1)
        *x = 1;
    if (*y < 0)
        *y = 0;
    if (*y > 1)
        *y = 1;
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
    /* One swipe turns around once. A hello turns twice. */
    static const float swipe[] = {
        0.28f, 0.62f, 0.28f, 0.28f, 0.28f, 0.28f, 0.28f, 0.28f};
    static const float hello[] = {
        0.28f, 0.62f, 0.28f, 0.62f, 0.62f, 0.62f, 0.62f, 0.62f};
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
    if (!push_all(&h, hello, NULL, 0.80f))
        return fail("hello");
    if (push_all(&h, wave, NULL, 0))
        return fail("nobody");
    if (!push_all(&h, NULL, wave, 0.80f))
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
    {
        static float box[PALM_N * PALM_C];
        static float score[PALM_N];
        float x, y, conf, ax, ay;
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
        if (palm_center(box, score, PALM_S, PALM_S,
                &x, &y, &conf))
            return fail("empty");
        score[0] = 4;
        if (!palm_center(box, score, PALM_S, PALM_S,
                &x, &y, &conf))
            return fail("palm");
        if (conf < 0.9f || !near(x, 0.020833f)
            || !near(y, 0.020833f))
            return fail("spot");
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

/* mass is the palm score, or 0 when the frame has no palm. */
static void palm_run(
    PalmRt *p, const unsigned char *rgb, int w, int h,
    float *x, float *y, float *mass)
{
    OrtValue *out[2] = { NULL, NULL };
    const char *const in_name[] = { "input_1" };
    const char *const out_name[] = {
        "Identity", "Identity_1"
    };
    const OrtValue *const in_val[] = { p->input };
    float *box = NULL, *score = NULL;
    float conf = 0;
    *x = 0;
    *y = 0;
    *mass = 0;
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
    if (palm_center(box, score, w, h, x, y, &conf))
        *mass = conf;
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

static int watch(void)
{
    Cam cam;
    Frame frame;
    PalmRt palm;
    WaveHist hist;
    int cool = 0, said = 0;
    memset(&cam, 0, sizeof cam);
    memset(&frame, 0, sizeof frame);
    memset(&palm, 0, sizeof palm);
    memset(&hist, 0, sizeof hist);
    cam.fd = -1;
    if (!palm_open(&palm, DSL_BAO_PALM))
        return fail("no palm model");
    for (;;) {
        double t0, left;
        float x = 0, y = 0, mass = 0;
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
        }
        t0 = mono();
        got = cam_grab(&cam, &frame);
        if (got < 0) {
            cam_close(&cam);
            continue;
        }
        if (got > 0) {
            palm_run(
                &palm, frame.rgb, frame.w, frame.h,
                &x, &y, &mass);
            if (cool > 0)
                cool--;
            else if (wave_push(&hist, x, y, mass)) {
                wave_send();
                fprintf(stderr, "dsl-bao-wave: palm\n");
                cool = WAVE_COOL;
            }
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
    float x, y, mass;
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
    palm_run(&palm, rgb, w, h, &x, &y, &mass);
    printf("%.4f %.4f %.4f\n", mass, x, y);
    free(rgb);
    return mass >= WAVE_MIN_MASS ? 0 : 2;
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
