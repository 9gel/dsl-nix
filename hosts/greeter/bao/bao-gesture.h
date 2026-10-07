#ifndef DSL_BAO_GESTURE_H
#define DSL_BAO_GESTURE_H

#define WAVE_N 32
#define WAVE_MIN_MASS 0.50f
#define WAVE_MIN_SPAN 0.16f
#define WAVE_MIN_STEP 0.05f
#define WAVE_REVERSALS 3
#define WAVE_COOL 80
#define PALM_S 192
#define PALM_N 2016
#define PALM_C 18
/* Below the wave gate, so a weak frame still draws. */
#define PALM_TRACK_MIN 0.30f
#define PALM_RAW 48
#define PALM_OUT 4
/* Missed frames to keep the last box. 5 is about 0.6 s. */
#define PALM_KEEP 5
/* Same hand if the center moves no farther than this. */
#define PALM_NEAR 0.20f
#define PALM_IOU 0.30f
/* Count a pixel when any channel moves more than this. */
#define MOVE_DIFF 28
#define MOVE_SKIP 16
/* Frames to keep the picture up. 64 is about eight
   seconds at 8 fps. */
#define FEED_SHOW 64

typedef struct {
    float x[WAVE_N];
    float y[WAVE_N];
    float mass[WAVE_N];
    int n;
    int i;
} WaveHist;

typedef struct {
    float x, y, conf;
    float x0, y0, x1, y1;
} Palm;

typedef struct {
    Palm box;
    int age;
    int on;
} Track;

void wave_clear(WaveHist *h);
int wave_push(WaveHist *h, float x, float y, float mass);
int frame_moved(
    const unsigned char *a, const unsigned char *b, int n);
float wave_mass(int moving, float palm);
int feed_show(int moving, int *linger);
void rot90(
    const unsigned char *src, int w, int h, int turn,
    unsigned char *dst);
void mirror_h(unsigned char *rgb, int w, int h);
void preview_size(int w, int h, int *pw, int *ph);
void anchor_at(int i, float *ax, float *ay);
void palm_geom(
    int w, int h, float *scale, float *pad_x, float *pad_y);
int palm_center(
    const float *box, const float *score,
    int w, int h, Palm *o);
int palm_list(
    const float *box, const float *score,
    int w, int h, Palm *out, int max);
void palm_follow(Track *t, const Palm *c, int n);

#endif
