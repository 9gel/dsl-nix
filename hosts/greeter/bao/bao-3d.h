#ifndef DSL_BAO_3D_H
#define DSL_BAO_3D_H

enum { WALKING, LOOKING, SEEKING, PEEING, NOTICING };

typedef struct {
    double x, z, heading, speed, turn, wander;
    double y, vy, spin, spin_to, phase;
    int mode;
    double act, look, lift, puddle, gait, aim, mark, pop;
} Bao;

typedef struct {
    double x, y;
} Vec;

typedef struct {
    double x, y, w, h, rot;
} Place;

typedef struct {
    float margin, side;
    float tx, ty;
    float qx, qy;
} Hud;

extern const double k_pi;
extern const double EDGE_LO;
extern const double EDGE_HI;
extern const double CORNER;
extern const double PEE_X;
extern const double PEE_Z;
extern const double WALK;
extern const double RUN;
extern const double TURN_MAX;
extern const double JUMP_UP;
extern const double MISS_JUMP;
extern const double DT_MAX;
extern const double P_LOOK;
extern const double LOOK_TIME;
extern const double PEE_TIME;
extern const double SEEK_MAX;
extern const double FACE_VIEW;
extern const double FACE_PEE;
extern const double CAM_X;
extern const double CAM_Z;
extern const double NOTICE_LOOK;
extern const float MARK_JUMP;

Bao bao_at(double x, double z, double heading);
void set_mode(Bao *b, int mode);
double wrap_angle(double a);
void walk(Bao *b, double dt, double r1, double r2);
void poke(Bao *b, int hit, double flee);
void notice(Bao *b);
int norm_turn(int turn);
Vec logical_to_fb(
    int turn, int lw, int lh, double x, double y);
Vec fb_to_logical(
    int turn, int lw, int lh, double x, double y);
Place scene_place(int turn, int fw, int fh);
Vec place_point(Place pl, double u, double v);
void local_side(
    float ux, float uz, float yaw, float *lx, float *lz);
float head_reach(float rx, float rz, float lx, float lz);
void mark_pose(float t, float *sx, float *sy, float *hop);
Hud hud_place(int lw, int lh, float text_h);
void feed_place(
    float qr_bottom, int fw, int fh,
    float *x, float *y, float *w, float *h);
void feed_box(
    float pw, float ph,
    float x0, float y0, float x1, float y1,
    float *bx, float *by, float *bw, float *bh);

#endif
