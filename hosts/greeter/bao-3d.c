/* Dim Sum Labs 3D bao on the Pi display.

   The bao chip from model_for_dslan.blend paces the corner of a
   red room. A tap on it makes it jump, spin and yell. The
   screen turn, touch, QR, captions and sounds follow bao.c.
   --check needs no display.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DSL_BAO_HEADLESS
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#endif

#ifndef DSL_SCREEN_TURN
#define DSL_SCREEN_TURN 1
#endif
#ifndef DSL_BAO_MODELS
#define DSL_BAO_MODELS ""
#endif
#ifndef DSL_BAO_FONT
#define DSL_BAO_FONT ""
#endif
#ifndef DSL_BAO_QR
#define DSL_BAO_QR ""
#endif
#ifndef DSL_BAO_SOUNDS
#define DSL_BAO_SOUNDS ""
#endif

static const double k_pi = 3.14159265358979323846;
/* Room units are Blender units. The bao is about 9 wide. The
   corner is the origin; walls stand on x = 0 and z = 0. The bao
   paces EDGE_LO..EDGE_HI on both floor axes. */
static const double EDGE_LO = 9.0;
static const double EDGE_HI = 50.0;
static const double AVOID = 8.0;
static const double WALK = 7.0;
static const double RUN = 26.0;
static const double RELAX = 1.2;
static const double TURN_MAX = 1.1;
static const double STRIDE = 3.2;
static const double JUMP_UP = 34.0;
static const double MISS_JUMP = 12.0;
static const double GRAVITY = 90.0;
static const double SPIN_EASE = 5.0;
static const double DT_MAX = 1.0 / 20.0;

typedef struct {
    double x, z, heading, speed, turn, wander;
    double y, vy, spin, spin_to, phase;
} Bao;

typedef struct {
    double x, y;
} Vec;

typedef struct {
    double x, y, w, h, rot;
} Place;

static double wrap_angle(double a)
{
    while (a > k_pi)
        a -= 2 * k_pi;
    while (a < -k_pi)
        a += 2 * k_pi;
    return a;
}

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* One tick. rnd is uniform in [0, 1): a new wander turn is drawn
   from it when the old one runs out. Near a wall the bao turns
   for the middle of its patch instead. */
static void walk(Bao *b, double dt, double rnd)
{
    double mid = (EDGE_LO + EDGE_HI) / 2;
    double near = fmin(
        fmin(b->x - EDGE_LO, EDGE_HI - b->x),
        fmin(b->z - EDGE_LO, EDGE_HI - b->z));
    double gain;
    dt = clampd(dt, 0, DT_MAX);
    b->wander -= dt;
    if (b->wander <= 0) {
        b->turn = (rnd * 2 - 1) * TURN_MAX;
        b->wander = 1.2 + rnd * 2.5;
    }
    if (near < AVOID) {
        double want = atan2(mid - b->z, mid - b->x);
        double off = wrap_angle(want - b->heading);
        b->turn = off > 0 ? TURN_MAX * 2 : -TURN_MAX * 2;
        if (fabs(off) < 0.3)
            b->turn = off / 0.3 * TURN_MAX;
    }
    b->heading = wrap_angle(b->heading + b->turn * dt);
    gain = 1 - exp(-RELAX * dt);
    b->speed += (WALK - b->speed) * gain;
    b->x += cos(b->heading) * b->speed * dt;
    b->z += sin(b->heading) * b->speed * dt;
    b->x = clampd(b->x, EDGE_LO, EDGE_HI);
    b->z = clampd(b->z, EDGE_LO, EDGE_HI);
    if (b->y > 0 || b->vy > 0) {
        b->vy -= GRAVITY * dt;
        b->y += b->vy * dt;
        if (b->y <= 0) {
            b->y = 0;
            b->vy = 0;
        }
    } else {
        b->phase = fmod(
            b->phase + b->speed * dt / STRIDE * 2 * k_pi, 2 * k_pi);
    }
    b->spin += (b->spin_to - b->spin) * (1 - exp(-SPIN_EASE * dt));
}

/* A hit jumps, spins once and bolts along flee. A miss hops. */
static void poke(Bao *b, int hit, double flee)
{
    if (hit) {
        b->vy = JUMP_UP;
        b->spin_to += flee > b->heading ? 2 * k_pi : -2 * k_pi;
        b->heading = wrap_angle(flee);
        b->speed = RUN;
        b->wander = 1.5;
        b->turn = 0;
    } else if (b->y == 0) {
        b->vy = MISS_JUMP;
    }
}

static int norm_turn(int turn)
{
    return turn == 3 ? 3 : 1;
}

static Vec logical_to_fb(int turn, int lw, int lh, double x, double y)
{
    Vec p;
    if (turn == 1) {
        p.x = y;
        p.y = (double)lw - x;
    } else {
        p.x = (double)lh - y;
        p.y = x;
    }
    return p;
}

static Vec fb_to_logical(int turn, int lw, int lh, double x, double y)
{
    Vec p;
    if (turn == 1) {
        p.x = (double)lw - y;
        p.y = x;
    } else {
        p.x = y;
        p.y = (double)lh - x;
    }
    return p;
}

/* Dest rect of the portrait texture on the landscape mode.
   Corners match logical_to_fb. check() locks that. */
static Place scene_place(int turn, int fw, int fh)
{
    if (turn == 1)
        return (Place){0, fh, fh, fw, -90};
    return (Place){fw, 0, fh, fw, 90};
}

static Vec place_point(Place pl, double u, double v)
{
    double rad = pl.rot * k_pi / 180.0;
    Vec o;
    o.x = pl.x + u * cos(rad) - v * sin(rad);
    o.y = pl.y + u * sin(rad) + v * cos(rad);
    return o;
}

static int fail(const char *msg)
{
    fprintf(stderr, "dsl-bao-3d: %s\n", msg);
    return 1;
}

static int check(void)
{
    Bao b = {30, 30, 0, WALK, 0, 0, 0, 0, 0, 0, 0};
    double t, spin, peak = 0;
    int i, turn;

    for (i = 0; i < 20000; i++) {
        walk(&b, 1.0 / 60, (double)((i * 7919) % 1000) / 1000);
        if (b.x < EDGE_LO || b.x > EDGE_HI)
            return fail("x out of patch");
        if (b.z < EDGE_LO || b.z > EDGE_HI)
            return fail("z out of patch");
    }
    if (fabs(b.speed - WALK) > 1e-6)
        return fail("walk speed");

    /* Pinned at a wall, facing it: it must turn away. */
    b = (Bao){EDGE_LO, 30, k_pi, WALK, 0, 10, 0, 0, 0, 0, 0};
    for (i = 0; i < 180; i++)
        walk(&b, 1.0 / 60, 0.5);
    if (!(b.x > EDGE_LO + 1))
        return fail("wall turn");

    b = (Bao){30, 30, 0, WALK, 0, 10, 0, 0, 0, 0, 0};
    poke(&b, 1, 1.0);
    if (b.vy != JUMP_UP || b.speed != RUN || b.heading != 1.0)
        return fail("hit");
    spin = b.spin_to;
    if (fabs(spin) != 2 * k_pi)
        return fail("hit spin");
    for (t = 0; t < 3; t += 1.0 / 60) {
        walk(&b, 1.0 / 60, 0.5);
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

    b = (Bao){30, 30, 0, WALK, 0, 10, 0, 0, 0, 0, 0};
    walk(&b, 10, 0.5);
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
    printf("ok\n");
    return 0;
}

#ifndef DSL_BAO_HEADLESS

static const float ROOM = 400;
static const float WALL_H = 90;
static const float QR_FRACTION = 0.2f;
static const char *sound_names[] = {
    "boing",
    "ouch",
    "dont-touch-me",
    "stop-it",
};
static const int sound_n = 4;
static const char *caption_lines[] = {
    "Join us on Telegram!",
    "Scan the code on the right.",
};
static const char FOOTER[] = "Don't touch the bao!";
static const Color INK = {255, 248, 244, 255};
static const Color ROOM_RED = {255, 31, 68, 255};

/* Lambert sun plus sky. With roomShade 1, a cheap corner shade
   from world position: room surfaces darken near the floor and
   walls they do not lie on. colSpecular.r is shine, from the
   glTF metallic factor: a sun highlight, a rim glint and red
   bounce light from the floor. */
static const char *VS =
    "attribute vec3 vertexPosition;\n"
    "attribute vec3 vertexNormal;\n"
    "uniform mat4 mvp;\n"
    "uniform mat4 matModel;\n"
    "uniform mat4 matNormal;\n"
    "varying vec3 fragNormal;\n"
    "varying vec3 fragPos;\n"
    "void main() {\n"
    "  fragPos = (matModel * vec4(vertexPosition, 1.0)).xyz;\n"
    "  fragNormal = (matNormal * vec4(vertexNormal, 0.0)).xyz;\n"
    "  gl_Position = mvp * vec4(vertexPosition, 1.0);\n"
    "}\n";
static const char *FS =
    "uniform vec4 colDiffuse;\n"
    "uniform float roomShade;\n"
    "uniform vec4 colSpecular;\n"
    "uniform vec3 viewPos;\n"
    "varying vec3 fragNormal;\n"
    "varying vec3 fragPos;\n"
    "void main() {\n"
    "  vec3 n = normalize(fragNormal);\n"
    "  vec3 sun = normalize(vec3(0.55, 1.0, 0.3));\n"
    "  float lit = 0.62 + 0.08 * n.y + 0.35 * max(dot(n, sun), 0.0);\n"
    "  vec3 p = fragPos + step(0.5, n) * 1000.0;\n"
    "  float d = min(min(p.x, p.y), p.z);\n"
    "  float ao = 1.0 - roomShade * 0.28"
    " * (1.0 - smoothstep(0.0, 14.0, d));\n"
    "  vec3 v = normalize(viewPos - fragPos);\n"
    "  float shine = colSpecular.r;\n"
    "  float spec = pow(max(dot(n, normalize(sun + v)), 0.0), 48.0);\n"
    "  float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);\n"
    "  vec3 bounce = vec3(1.0, 0.12, 0.27) * max(-n.y, 0.0);\n"
    "  vec3 col = colDiffuse.rgb * lit * ao"
    " + shine * (0.9 * spec + 0.35 * rim + 0.3 * bounce);\n"
    "  gl_FragColor = vec4(col, 1.0);\n"
    "}\n";

static Shader load_lit(void)
{
    const char *es_head = "#version 100\nprecision mediump float;\n";
    const char *vs_head =
        "#version 330\n#define attribute in\n#define varying out\n";
    const char *fs_head =
        "#version 330\n#define varying in\n"
        "out vec4 outColor;\n#define gl_FragColor outColor\n";
    int es = rlGetVersion() == RL_OPENGL_ES_20;
    char vs[2048], fs[2048];
    Shader sh;
    snprintf(vs, sizeof vs, "%s%s", es ? es_head : vs_head, VS);
    snprintf(fs, sizeof fs, "%s%s", es ? es_head : fs_head, FS);
    sh = LoadShaderFromMemory(vs, fs);
    sh.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(sh, "matModel");
    sh.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(sh, "matNormal");
    sh.locs[SHADER_LOC_COLOR_SPECULAR] =
        GetShaderLocation(sh, "colSpecular");
    sh.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(sh, "viewPos");
    return sh;
}

/* Also turns the glTF metallic factor into shine. Default
   materials, like the room's, have 0. */
static void use_shader(Model *m, Shader sh)
{
    int i;
    for (i = 0; i < m->materialCount; i++) {
        MaterialMap *maps = m->materials[i].maps;
        float metal = Clamp(maps[MATERIAL_MAP_METALNESS].value, 0, 1);
        unsigned char s = (unsigned char)(metal * 255);
        m->materials[i].shader = sh;
        maps[MATERIAL_MAP_SPECULAR].color = (Color){s, s, s, 255};
    }
}

static Model room_part(Shader sh, Vector3 size, Vector3 at)
{
    Model m = LoadModelFromMesh(GenMeshCube(size.x, size.y, size.z));
    m.transform = MatrixTranslate(at.x, at.y, at.z);
    m.materials[0].maps[MATERIAL_MAP_DIFFUSE].color = ROOM_RED;
    use_shader(&m, sh);
    return m;
}

static const char *asset(const char *key, const char *fallback)
{
    const char *v = getenv(key);
    if (v != NULL && v[0] != '\0')
        return v;
    return fallback;
}

static int screen_turn(void)
{
    const char *v = getenv("DSL_SCREEN_TURN");
    if (v != NULL && v[0] != '\0')
        return norm_turn(atoi(v));
    return norm_turn(DSL_SCREEN_TURN);
}

static int load_model(const char *dir, const char *name, Model *out)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (!FileExists(path)) {
        fprintf(stderr, "dsl-bao-3d: missing %s\n", path);
        return 0;
    }
    *out = LoadModel(path);
    return out->meshCount > 0;
}

static float fit_caption(Font font, float max_w, float max_h)
{
    float size = max_h * 0.38f;
    while (size > 12) {
        float wide = 0, block = 0;
        int i;
        for (i = 0; i < 2; i++) {
            Vector2 m = MeasureTextEx(
                font, caption_lines[i], size, size / 10);
            wide = fmaxf(wide, m.x);
            block += m.y;
        }
        if (wide <= max_w && block <= max_h)
            return size;
        size -= 2;
    }
    return 12;
}

static float fit_footer(Font font, float target_w, float max_h)
{
    float size = 12;
    for (;;) {
        Vector2 m = MeasureTextEx(font, FOOTER, size + 2, (size + 2) / 10);
        if (m.x > target_w || m.y > max_h)
            return size;
        size += 2;
    }
}

static void draw_overlay(
    Font font, Texture2D qr, float cap, float foot, int lw, int lh)
{
    int margin = (int)fmax(16, lh * 0.02);
    int side = (int)(lh * QR_FRACTION);
    float qx = (float)(lw - margin - side);
    Vector2 a = MeasureTextEx(font, caption_lines[0], cap, cap / 10);
    Vector2 b = MeasureTextEx(font, caption_lines[1], cap, cap / 10);
    Vector2 f = MeasureTextEx(font, FOOTER, foot, foot / 10);
    float gap = fmaxf(4, a.y / 8);
    float y = margin + (side - (a.y + gap + b.y)) / 2;
    DrawTexturePro(
        qr, (Rectangle){0, 0, (float)qr.width, (float)qr.height},
        (Rectangle){qx, (float)margin, (float)side, (float)side},
        (Vector2){0, 0}, 0, WHITE);
    DrawTextEx(
        font, caption_lines[0], (Vector2){(float)margin, y},
        cap, cap / 10, INK);
    DrawTextEx(
        font, caption_lines[1], (Vector2){(float)margin, y + a.y + gap},
        cap, cap / 10, INK);
    DrawTextEx(
        font, FOOTER,
        (Vector2){lw - margin - f.x, lh - margin - f.y},
        foot, foot / 10, INK);
}

static int load_sounds(const char *dir, Sound *out)
{
    int n = 0, i;
    if (dir[0] == '\0')
        return 0;
    InitAudioDevice();
    for (i = 0; i < sound_n; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.wav", dir, sound_names[i]);
        if (!FileExists(path)) {
            fprintf(stderr, "dsl-bao-3d: missing %s\n", path);
            continue;
        }
        out[n] = LoadSound(path);
        if (out[n].frameCount > 0)
            n++;
    }
    return n;
}

static void play_hit(Sound *sounds, int count)
{
    int i;
    if (count <= 0 || !IsAudioDeviceReady())
        return;
    for (i = 0; i < count; i++)
        StopSound(sounds[i]);
    PlaySound(sounds[GetRandomValue(0, count - 1)]);
}

static int take_tap(
    int turn, int lw, int lh, int down,
    double now, double *last, Vector2 *at)
{
    Vector2 raw;
    Vec p;
    if (GetTouchPointCount() > 0 && !down)
        raw = GetTouchPosition(0);
    else if (GetTouchPointCount() == 0
             && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        raw = GetMousePosition();
    else
        return 0;
    if (*last >= 0 && now - *last < 0.12)
        return 0;
    p = fb_to_logical(turn, lw, lh, raw.x, raw.y);
    *at = (Vector2){(float)p.x, (float)p.y};
    *last = now;
    return 1;
}

/* Legs trot: a group swings forward while lifted, then plants
   and pushes back. A and B run half a cycle apart. */
static Matrix leg_offset(double phase, double lift)
{
    return MatrixTranslate(
        (float)(sin(phase) * 0.45),
        (float)(fmax(0, cos(phase)) * 0.55 * lift),
        0);
}

static void draw_bao(
    Model body, Model legs_a, Model legs_b,
    const Bao *b, float floor_y, double squash)
{
    double grounded = b->y > 0 ? 0 : 1;
    float bob = (float)(fabs(sin(b->phase)) * 0.25 * grounded);
    Vector3 at = {(float)b->x, floor_y + (float)b->y + bob, (float)b->z};
    Vector3 up = {0, 1, 0};
    float deg = (float)((-b->heading + b->spin) * 180 / k_pi);
    float wide = (float)(1 + (1 - squash) * 0.5);
    Vector3 scale = {wide, (float)squash, wide};
    float shadow = (float)(4.2 / (1 + b->y * 0.08));
    DrawCylinder(
        (Vector3){at.x, 0.05f, at.z}, shadow, shadow, 0.02f, 24,
        (Color){90, 0, 20, 110});
    legs_a.transform = leg_offset(b->phase, grounded);
    legs_b.transform = leg_offset(b->phase + k_pi, grounded);
    DrawModelEx(body, at, up, deg, scale, WHITE);
    DrawModelEx(legs_a, at, up, deg, scale, WHITE);
    DrawModelEx(legs_b, at, up, deg, scale, WHITE);
}

static void present(Texture2D tex, int turn, int fw, int fh)
{
    Place pl = scene_place(turn, fw, fh);
    DrawTexturePro(
        tex, (Rectangle){0, 0, (float)tex.width, -(float)tex.height},
        (Rectangle){(float)pl.x, (float)pl.y, (float)pl.w, (float)pl.h},
        (Vector2){0, 0}, (float)pl.rot, WHITE);
}

static int run(void)
{
    const char *dir = asset("DSL_BAO_MODELS", DSL_BAO_MODELS);
    const char *font_path = asset("DSL_BAO_FONT", DSL_BAO_FONT);
    const char *qr_path = asset("DSL_BAO_QR", DSL_BAO_QR);
    const char *sound_dir = asset("DSL_BAO_SOUNDS", DSL_BAO_SOUNDS);
    const char *shot = getenv("DSL_BAO_SHOT");
    int turn, fw, fh, lw, lh, sound_count, down = 0, frame = 0;
    Model body, legs_a, legs_b, floor, wall_x, wall_z;
    Shader lit;
    Texture2D qr;
    Font font;
    RenderTexture2D scene;
    Sound sounds[4];
    Camera3D cam;
    BoundingBox box;
    Bao b = {34, 26, 2.4, WALK, 0, 0, 0, 0, 0, 0, 0};
    float floor_y, cap, foot, hit_y, hit_r;
    double squash = 1, last_tap = -1;
    float one = 1, zero = 0;
    int shade_loc;

    if (!FileExists(font_path) || !FileExists(qr_path)) {
        fprintf(stderr, "dsl-bao-3d: font or qr missing\n");
        return 1;
    }
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    /* InitWindow(0, 0) copies an unset display size, then
       the nearest-mode search picks the smallest mode. */
    InitWindow(2560, 1440, "dsl-bao-3d");
    if (!IsWindowReady())
        return fail("display did not open");
    HideCursor();
    SetTargetFPS(60);
    fw = GetScreenWidth();
    fh = GetScreenHeight();
    fprintf(stderr, "dsl-bao-3d: mode %dx%d\n", fw, fh);
    turn = screen_turn();
    lw = fh;
    lh = fw;

    lit = load_lit();
    if (!IsShaderValid(lit)
        || !load_model(dir, "bao-3d-body.glb", &body)
        || !load_model(dir, "bao-3d-legs-a.glb", &legs_a)
        || !load_model(dir, "bao-3d-legs-b.glb", &legs_b)) {
        CloseWindow();
        return fail("model or shader failed");
    }
    shade_loc = GetShaderLocation(lit, "roomShade");
    use_shader(&body, lit);
    use_shader(&legs_a, lit);
    use_shader(&legs_b, lit);
    floor = room_part(
        lit, (Vector3){ROOM, 1, ROOM},
        (Vector3){ROOM / 2, -0.5f, ROOM / 2});
    wall_x = room_part(
        lit, (Vector3){1, WALL_H, ROOM},
        (Vector3){-0.5f, WALL_H / 2, ROOM / 2});
    wall_z = room_part(
        lit, (Vector3){ROOM, WALL_H, 1},
        (Vector3){ROOM / 2, WALL_H / 2, -0.5f});
    box = GetModelBoundingBox(legs_a);
    floor_y = -box.min.y;
    box = GetModelBoundingBox(body);
    hit_y = floor_y + (box.min.y + box.max.y) / 2;
    hit_r = (box.max.x - box.min.x) * 0.6f;

    qr = LoadTexture(qr_path);
    font = LoadFontEx(font_path, 128, NULL, 0);
    SetTextureFilter(qr, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    scene = LoadRenderTexture(lw, lh);
    if (qr.id == 0 || font.texture.id == 0 || scene.id == 0) {
        CloseWindow();
        return fail("asset upload failed");
    }
    sound_count = load_sounds(sound_dir, sounds);
    cap = fit_caption(
        font, lw - lh * QR_FRACTION - 3 * fmaxf(16, lh * 0.02f),
        lh * QR_FRACTION);
    foot = fit_footer(font, lw / 2.0f, lh / 8.0f);

    /* ponytail: framing is by eye for a 9:16 portrait. */
    cam.position = (Vector3){84, 50, 84};
    cam.target = (Vector3){27, 9, 27};
    cam.up = (Vector3){0, 1, 0};
    cam.fovy = 34;
    cam.projection = CAMERA_PERSPECTIVE;
    SetShaderValue(
        lit, lit.locs[SHADER_LOC_VECTOR_VIEW], &cam.position,
        SHADER_UNIFORM_VEC3);

    for (;;) {
        double dt = GetFrameTime();
        double now = GetTime();
        Vector2 tap;
        if (take_tap(turn, lw, lh, down, now, &last_tap, &tap)) {
            Ray ray = GetScreenToWorldRayEx(tap, cam, lw, lh);
            Vector3 c = {(float)b.x, hit_y + (float)b.y, (float)b.z};
            int hit = GetRayCollisionSphere(ray, c, hit_r).hit;
            double flee = atan2(b.z - ray.position.z, b.x - ray.position.x)
                + (GetRandomValue(-60, 60) * k_pi / 180);
            poke(&b, hit, flee);
            if (hit)
                play_hit(sounds, sound_count);
            squash = 0.7;
        }
        down = GetTouchPointCount() > 0;
        walk(&b, dt, GetRandomValue(0, 999) / 1000.0);
        squash += (1 - squash) * fmin(1, dt * 8);

        BeginTextureMode(scene);
        ClearBackground(ROOM_RED);
        BeginMode3D(cam);
        SetShaderValue(lit, shade_loc, &one, SHADER_UNIFORM_FLOAT);
        DrawModel(floor, (Vector3){0, 0, 0}, 1, WHITE);
        DrawModel(wall_x, (Vector3){0, 0, 0}, 1, WHITE);
        DrawModel(wall_z, (Vector3){0, 0, 0}, 1, WHITE);
        SetShaderValue(lit, shade_loc, &zero, SHADER_UNIFORM_FLOAT);
        draw_bao(body, legs_a, legs_b, &b, floor_y, squash);
        EndMode3D();
        draw_overlay(font, qr, cap, foot, lw, lh);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(BLACK);
        present(scene.texture, turn, fw, fh);
        EndDrawing();

        if (shot != NULL && ++frame == 120) {
            Image im = LoadImageFromTexture(scene.texture);
            ImageFlipVertical(&im);
            ExportImage(im, shot);
            CloseWindow();
            return 0;
        }
    }
}

#endif

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "--check") == 0)
        return check();
#ifdef DSL_BAO_HEADLESS
    fprintf(stderr, "dsl-bao-3d: this build has no display\n");
    return 1;
#else
    (void)argv;
    return run();
#endif
}
