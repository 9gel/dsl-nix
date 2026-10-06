/* Webcam watcher for the 3D bao.

   Reads a USB camera on the CPU, at a small size and 8 fps.
   A wave is one blob that reverses twice, travels a good
   part of the frame, and stays on one axis. A person
   walking past does not reverse. On a wave, sends one
   datagram to /tmp/dsl-bao-wave.sock. dsl-bao-3d owns the
   display and is the only one that binds that socket.

   ponytail: two reversals on one axis. A hand model if
   walk-bys get greeted again. --check needs no camera.
*/

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DSL_BAO_HEADLESS
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

#define WAVE_N 8
#define WAVE_MIN_MASS 0.01f
#define WAVE_MAX_MASS 0.35f
#define WAVE_MIN_SPAN 0.16f
#define WAVE_MIN_STEP 0.05f
#define WAVE_REVERSALS 2
#define DIFF_MIN 36
#define WAVE_COOL 80

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
   second at 8 fps. A still or flashing sample holds the last
   position so it cannot fake a reversal. */
static int wave_push(WaveHist *h, float x, float y, float mass)
{
    float xs[WAVE_N], ys[WAVE_N];
    int hot = 0, k, along_x;
    int wild = mass < WAVE_MIN_MASS || mass > WAVE_MAX_MASS;
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
        if (h->mass[at] >= WAVE_MIN_MASS
            && h->mass[at] <= WAVE_MAX_MASS)
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
        int got = wave_push(&h, wave[i], 0.5f, 0.05f);
        if (got != (i == WAVE_N - 1))
            return fail("wave");
    }
    if (wave_push(&h, 0.40f, 0.5f, 0.05f))
        return fail("cleared");
    if (push_all(&h, walk, NULL, 0.05f))
        return fail("walk");
    if (push_all(&h, jitter, NULL, 0.05f))
        return fail("jitter");
    if (push_all(&h, flick, NULL, 0.05f))
        return fail("flick");
    if (push_all(&h, swipe, NULL, 0.05f))
        return fail("swipe");
    if (!push_all(&h, hello, NULL, 0.05f))
        return fail("hello");
    if (push_all(&h, wave, NULL, 0.9f))
        return fail("flash");
    if (!push_all(&h, NULL, wave, 0.05f))
        return fail("sideways");
    for (i = 0; i < WAVE_N; i++)
        mid[i] = 0.50f;
    wave_clear(&h);
    for (i = 0; i < WAVE_N; i++) {
        float x = i == 3 ? 0.99f : mid[i];
        float mass = i == 3 ? 0 : 0.05f;
        if (wave_push(&h, x, 0.5f, mass))
            return fail("still");
    }
    printf("ok\n");
    return 0;
}

#ifndef DSL_BAO_HEADLESS

enum { CAM_BUFS = 2 };

typedef struct {
    unsigned char *a, *b;
    int w, h, n, which;
} Gray;

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

static unsigned char *gray_prev(Gray *g)
{
    return g->which ? g->b : g->a;
}

static unsigned char *gray_cur(Gray *g)
{
    return g->which ? g->a : g->b;
}

static void gray_swap(Gray *g)
{
    g->which ^= 1;
}

static int gray_fit(Gray *g, int w, int h)
{
    int n = w * h;
    unsigned char *a, *b;
    if (w < 16 || h < 12 || n > 160 * 120)
        return 0;
    if (g->n == n) {
        g->w = w;
        g->h = h;
        return 1;
    }
    a = calloc((size_t)n, 1);
    b = calloc((size_t)n, 1);
    if (a == NULL || b == NULL) {
        free(a);
        free(b);
        return 0;
    }
    free(g->a);
    free(g->b);
    g->a = a;
    g->b = b;
    g->n = n;
    g->w = w;
    g->h = h;
    g->which = 0;
    return 1;
}

static void centroid(Gray *g, float *x, float *y, float *mass)
{
    const unsigned char *prev = gray_prev(g);
    const unsigned char *cur = gray_cur(g);
    int i, n = g->w * g->h, hot = 0;
    long sx = 0, sy = 0;
    for (i = 0; i < n; i++) {
        int d = (int)cur[i] - (int)prev[i];
        if (d < 0)
            d = -d;
        if (d <= DIFF_MIN)
            continue;
        sx += i % g->w;
        sy += i / g->w;
        hot++;
    }
    *mass = n > 0 ? (float)hot / (float)n : 0;
    if (hot == 0) {
        *x = 0;
        *y = 0;
        return;
    }
    *x = (float)sx / (float)hot / (float)(g->w - 1);
    *y = (float)sy / (float)hot / (float)(g->h - 1);
}

static int copy_gray(
    Gray *g, struct jpeg_decompress_struct *cinfo,
    unsigned char *volatile *rowp)
{
    int skip = 1, dw, dh, x, y, ow, oh;
    unsigned char *row, *dst;
    ow = (int)cinfo->output_width;
    oh = (int)cinfo->output_height;
    if (ow < 16 || oh < 12)
        return 0;
    while (ow / skip > 160 || oh / skip > 120)
        skip++;
    dw = ow / skip;
    dh = oh / skip;
    if (!gray_fit(g, dw, dh))
        return 0;
    row = malloc((size_t)ow);
    if (row == NULL)
        return 0;
    *rowp = row;
    dst = gray_cur(g);
    for (y = 0; y < oh; y++) {
        JSAMPROW line = row;
        int dy;
        if (jpeg_read_scanlines(cinfo, &line, 1) != 1)
            return 0;
        if (y % skip != 0)
            continue;
        dy = y / skip;
        if (dy >= dh)
            continue;
        for (x = 0; x < dw; x++)
            dst[dy * dw + x] = row[x * skip];
    }
    return 1;
}

static int decode_mjpg(
    Gray *g, const unsigned char *jpg, unsigned len)
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
    cinfo->out_color_space = JCS_GRAYSCALE;
    if (cinfo->image_width > 640 || cinfo->image_height > 480) {
        cinfo->scale_num = 1;
        cinfo->scale_denom = 8;
    }
    jpeg_start_decompress(cinfo);
    ok = copy_gray(g, cinfo, &row);
    jpeg_finish_decompress(cinfo);
    jpeg_destroy_decompress(cinfo);
    free(cinfo);
    free((void *)row);
    return ok;
}

static int take_yuyv(
    Gray *g, const unsigned char *p, int len,
    int sw, int sh, int stride)
{
    int skip = 1, dw, dh, x, y;
    unsigned char *dst;
    if (stride < sw * 2)
        stride = sw * 2;
    if (sw < 16 || sh < 12 || len < stride * sh)
        return 0;
    while (sw / skip > 160 || sh / skip > 120)
        skip++;
    dw = sw / skip;
    dh = sh / skip;
    if (!gray_fit(g, dw, dh))
        return 0;
    dst = gray_cur(g);
    for (y = 0; y < dh; y++) {
        const unsigned char *src =
            p + (size_t)(y * skip) * (size_t)stride;
        for (x = 0; x < dw; x++)
            dst[y * dw + x] = src[x * skip * 2];
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

/* 1 = a frame is in the gray buffer. 0 = skip. -1 = reopen. */
static int cam_grab(Cam *c, Gray *g)
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
            got = decode_mjpg(g, p, len);
        else
            got = take_yuyv(
                g, p, (int)len, c->w, c->h, c->stride);
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
    Gray gray;
    WaveHist hist;
    int cool = 0, said = 0, prime = 1;
    memset(&cam, 0, sizeof cam);
    memset(&gray, 0, sizeof gray);
    memset(&hist, 0, sizeof hist);
    cam.fd = -1;
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
            prime = 1;
            wave_clear(&hist);
        }
        t0 = mono();
        got = cam_grab(&cam, &gray);
        if (got < 0) {
            cam_close(&cam);
            continue;
        }
        if (got > 0 && prime) {
            /* First frame has no previous picture. Keep it
               only so the next diff is real. */
            gray_swap(&gray);
            prime = 0;
        } else if (got > 0) {
            centroid(&gray, &x, &y, &mass);
            gray_swap(&gray);
            if (cool > 0)
                cool--;
            else if (wave_push(&hist, x, y, mass)) {
                wave_send();
                cool = WAVE_COOL;
            }
        }
        left = 0.125 - (mono() - t0);
        sleep_s(left);
    }
}

#endif

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--check") == 0)
        return check();
#ifndef DSL_BAO_HEADLESS
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
