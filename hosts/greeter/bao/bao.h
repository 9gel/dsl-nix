#ifndef DSL_BAO_H
#define DSL_BAO_H

typedef struct {
    double x, y, vx, vy, w, h, spin, hop;
} Body;

typedef struct {
    int cx, cy, width, height;
    int sx, sy, shadow_w, shadow_h, alpha;
    int box_l, box_t, box_r, box_b;
} Frame;

typedef struct {
    double x, y;
} Vec;

typedef struct {
    double x, y, w, h, rot;
} Place;

extern const double CRUISE;
extern const double MAX_SPEED;
extern const double JUMP_UP;
extern const double MISS_JUMP;
extern const double HOP_TIME;
extern const char *sound_names[];
extern const int sound_n;

const char *sound_at(int index);
void logo_span(
    double screen_w, double screen_h,
    double image_w, double image_h,
    int *out_w, int *out_h);
Vec clamp_speed(double vx, double vy);
Vec damp(double vx, double vy, double dt);
int poke(Body *body, double tx, double ty);
int step(
    Body *body, double sw, double sh, double dt, double top);
Frame frame_of(const Body *body, double squash, double now);
void header_box(
    int screen_w, int screen_h,
    int *side, int *qx, int *qy, int *play_top);
void footer_box(
    int screen_w, int screen_h,
    int *margin, int *width, int *right);
int norm_turn(int turn);
void logical_size(
    int turn, int fw, int fh, int *lw, int *lh);
Vec logical_to_fb(
    int turn, int lw, int lh, double x, double y);
Vec fb_to_logical(
    int turn, int lw, int lh, double x, double y);
Place scene_place(int turn, int fw, int fh);
Vec place_point(Place place, double u, double v);

#endif
