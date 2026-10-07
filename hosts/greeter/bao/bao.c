/* Dim Sum Labs bao on the Pi display.

   The panel is landscape and mounted sideways. The scene is
   drawn portrait, then turned onto the mode. DSL_SCREEN_TURN
   1 is that turn. Use 3 if the picture is still sideways.
   Touch uses the inverse.
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bao.h"

#ifndef DSL_BAO_HEADLESS
#include "raylib.h"
#endif

#ifndef DSL_SCREEN_TURN
#define DSL_SCREEN_TURN 1
#endif
#ifndef DSL_BAO_LOGO
#define DSL_BAO_LOGO ""
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
const double CRUISE = 450.0;
const double MAX_SPEED = 1700.0;
static const double DAMP = 1.35;
const double JUMP_UP = 1400.0;
const double MISS_JUMP = 780.0;
static const double POKE_HIT = 700.0;
static const double POKE_MISS = 280.0;
static const double SPIN = 8.0;
const double HOP_TIME = 1.15;
static const double GRAVITY = 1600.0;
static const double DT_MAX = 1.0 / 20.0;
static const double LOGO_FRACTION = 0.40;
static const double SPIN_DAMP = 3.0;
static const double QR_FRACTION = 0.2;

const char *sound_names[] = {
    "boing",
    "ouch",
    "dont-touch-me",
    "stop-it",
};
const int sound_n = 4;

const char *sound_at(int index)
{
    return sound_names[index % sound_n];
}

void logo_span(
    double screen_w, double screen_h,
    double image_w, double image_h,
    int *out_w, int *out_h)
{
    double longest = image_w > image_h ? image_w : image_h;
    double shorter = screen_w < screen_h ? screen_w : screen_h;
    double scale;
    if (longest <= 0) {
        *out_w = 1;
        *out_h = 1;
        return;
    }
    scale = shorter * LOGO_FRACTION / longest;
    *out_w = (int)(image_w * scale);
    *out_h = (int)(image_h * scale);
    if (*out_w < 1)
        *out_w = 1;
    if (*out_h < 1)
        *out_h = 1;
}

Vec clamp_speed(double vx, double vy)
{
    double speed = hypot(vx, vy);
    Vec out;
    if (speed <= MAX_SPEED || speed == 0) {
        out.x = vx;
        out.y = vy;
        return out;
    }
    out.x = vx * MAX_SPEED / speed;
    out.y = vy * MAX_SPEED / speed;
    return out;
}

Vec damp(double vx, double vy, double dt)
{
    double speed = hypot(vx, vy);
    double excess, gain, scale;
    Vec out;
    if (speed < 1) {
        out.x = CRUISE;
        out.y = 0;
        return out;
    }
    if (speed > CRUISE) {
        excess = speed - CRUISE;
        speed = CRUISE + excess * exp(-DAMP * dt);
    } else {
        gain = 1 - exp(-DAMP * dt);
        speed = speed + (CRUISE - speed) * gain;
    }
    scale = speed / hypot(vx, vy);
    out.x = vx * scale;
    out.y = vy * scale;
    return out;
}

static int contains(const Body *body, double tx, double ty)
{
    return fabs(tx - body->x) <= body->w / 2
        && fabs(ty - body->y) <= body->h / 2;
}

int poke(Body *body, double tx, double ty)
{
    double dx = body->x - tx;
    double dy = body->y - ty;
    double dist = hypot(dx, dy);
    int hit = contains(body, tx, ty);
    double shove = hit ? POKE_HIT : POKE_MISS;
    Vec capped;
    if (dist < 1)
        dist = 1;
    body->vx += shove * dx / dist;
    body->vy = hit ? -JUMP_UP : -MISS_JUMP;
    body->hop = hit ? HOP_TIME : HOP_TIME * 0.6;
    body->spin += dx < 0 ? -SPIN : SPIN;
    capped = clamp_speed(body->vx, body->vy);
    body->vx = capped.x;
    body->vy = capped.y;
    return hit;
}

static int bounce(Body *body, double sw, double sh, double top)
{
    double half_w = body->w / 2;
    double half_h = body->h / 2;
    double ceiling = top + half_h;
    int hit = 0;
    if (body->x < half_w) {
        body->x = half_w;
        body->vx = fabs(body->vx);
        hit = 1;
    } else if (body->x > sw - half_w) {
        body->x = sw - half_w;
        body->vx = -fabs(body->vx);
        hit = 1;
    }
    if (body->y < ceiling) {
        body->y = ceiling;
        body->vy = fabs(body->vy);
        hit = 1;
    } else if (body->y > sh - half_h) {
        body->y = sh - half_h;
        body->vy = -fabs(body->vy);
        hit = 1;
    }
    return hit;
}

int step(
    Body *body, double sw, double sh, double dt, double top)
{
    Vec capped;
    int hit;
    if (dt < 0)
        dt = 0;
    if (dt > DT_MAX)
        dt = DT_MAX;
    if (body->hop > 0) {
        body->vy += GRAVITY * dt;
        body->hop -= dt;
        if (body->hop < 0)
            body->hop = 0;
    }
    body->x += body->vx * dt;
    body->y += body->vy * dt;
    hit = bounce(body, sw, sh, top);
    if (body->hop == 0) {
        capped = damp(body->vx, body->vy, dt);
        body->vx = capped.x;
        body->vy = capped.y;
    }
    capped = clamp_speed(body->vx, body->vy);
    body->vx = capped.x;
    body->vy = capped.y;
    body->spin *= exp(-SPIN_DAMP * dt);
    return hit;
}

static void rotated_bounds(
    double cx, double cy, double width, double height,
    double spin, int *left, int *top, int *right, int *bottom)
{
    double angle = fabs(spin);
    double cosine = fabs(cos(angle));
    double sine = fabs(sin(angle));
    double wide = width * cosine + height * sine;
    double tall = width * sine + height * cosine;
    *left = (int)floor(cx - wide / 2);
    *top = (int)floor(cy - tall / 2);
    *right = (int)ceil(cx + wide / 2);
    *bottom = (int)ceil(cy + tall / 2);
}

Frame frame_of(const Body *body, double squash, double now)
{
    double lift = 0;
    double bob, sy_f;
    int width, height, shadow_w, shadow_h;
    int left, top, right, bottom;
    int sleft, stop, sright, sbottom;
    Frame fr;
    if (body->hop > 0) {
        lift = body->hop / HOP_TIME;
        if (lift > 1)
            lift = 1;
    }
    bob = sin(now * 2.1) * 7 * (1 - lift);
    width = (int)(body->w * (1 + (1 - squash) * 0.5));
    height = (int)(body->h * squash);
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;
    fr.cx = (int)body->x;
    fr.cy = (int)(body->y + bob);
    fr.width = width;
    fr.height = height;
    shadow_w = (int)(body->w * (0.62 - 0.22 * lift));
    shadow_h = (int)(body->h * (0.14 - 0.05 * lift));
    if (shadow_w < 8)
        shadow_w = 8;
    if (shadow_h < 4)
        shadow_h = 4;
    fr.shadow_w = shadow_w;
    fr.shadow_h = shadow_h;
    fr.alpha = (int)(90 * (1 - 0.65 * lift));
    fr.sx = (int)body->x;
    sy_f = body->y + body->h * 0.42 + lift * 36;
    fr.sy = (int)sy_f;
    rotated_bounds(
        fr.cx, fr.cy, width, height, body->spin,
        &left, &top, &right, &bottom);
    rotated_bounds(
        fr.sx, fr.sy, shadow_w, shadow_h, 0,
        &sleft, &stop, &sright, &sbottom);
    fr.box_l = (left < sleft ? left : sleft) - 6;
    fr.box_t = (top < stop ? top : stop) - 6;
    fr.box_r = (right > sright ? right : sright) + 6;
    fr.box_b = (bottom > sbottom ? bottom : sbottom) + 6;
    return fr;
}

void header_box(
    int screen_w, int screen_h,
    int *side, int *qx, int *qy, int *play_top)
{
    int margin = (int)(screen_h * 0.02);
    if (margin < 16)
        margin = 16;
    *side = (int)(screen_h * QR_FRACTION);
    if (*side < 1)
        *side = 1;
    *qx = screen_w - margin - *side;
    *qy = margin;
    *play_top = margin + *side;
}

void footer_box(
    int screen_w, int screen_h,
    int *margin, int *width, int *right)
{
    *margin = (int)(screen_h * 0.02);
    if (*margin < 16)
        *margin = 16;
    *width = screen_w / 2;
    if (*width < 1)
        *width = 1;
    *right = screen_w - *margin;
}

int norm_turn(int turn)
{
    return turn == 3 ? 3 : 1;
}

void logical_size(int turn, int fw, int fh, int *lw, int *lh)
{
    if (turn == 1 || turn == 3) {
        *lw = fh;
        *lh = fw;
    } else {
        *lw = fw;
        *lh = fh;
    }
}

Vec logical_to_fb(
    int turn, int lw, int lh, double x, double y)
{
    Vec p;
    if (turn == 1) {
        p.x = y;
        p.y = (double)lw - x;
    } else if (turn == 3) {
        p.x = (double)lh - y;
        p.y = x;
    } else {
        p.x = x;
        p.y = y;
    }
    return p;
}

Vec fb_to_logical(
    int turn, int lw, int lh, double x, double y)
{
    Vec p;
    if (turn == 1) {
        p.x = (double)lw - y;
        p.y = x;
    } else if (turn == 3) {
        p.x = y;
        p.y = (double)lh - x;
    } else {
        p.x = x;
        p.y = y;
    }
    return p;
}

/* Dest rect of the portrait texture on the landscape mode.
   Corners match logical_to_fb. */
Place scene_place(int turn, int fw, int fh)
{
    Place p;
    if (turn == 1) {
        p.x = 0;
        p.y = fh;
        p.w = fh;
        p.h = fw;
        p.rot = -90;
    } else if (turn == 3) {
        p.x = fw;
        p.y = 0;
        p.w = fh;
        p.h = fw;
        p.rot = 90;
    } else {
        p.x = 0;
        p.y = 0;
        p.w = fw;
        p.h = fh;
        p.rot = 0;
    }
    return p;
}

static Vec rot_cw(Vec p, Vec pivot, double deg)
{
    double rad = deg * k_pi / 180.0;
    double c = cos(rad);
    double s = sin(rad);
    double dx = p.x - pivot.x;
    double dy = p.y - pivot.y;
    Vec o;
    o.x = pivot.x + dx * c - dy * s;
    o.y = pivot.y + dx * s + dy * c;
    return o;
}

Vec place_point(Place place, double u, double v)
{
    Vec p;
    p.x = place.x + u;
    p.y = place.y + v;
    return rot_cw(p, (Vec){place.x, place.y}, place.rot);
}


#ifndef DSL_BAO_HEADLESS

static const char *caption_lines[] = {
    "Join us on Telegram!",
    "Scan the code on the right.",
};
static const char FOOTER[] = "Don't touch the bao!";

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

static int require_file(const char *path, const char *label)
{
    if (path[0] == '\0' || !FileExists(path)) {
        fprintf(stderr, "dsl-bao: %s missing\n", label);
        return 0;
    }
    return 1;
}

static float fit_caption(Font font, float max_w, float max_h)
{
    float size = max_h * 0.38f;
    if (size < 12)
        size = 12;
    while (size > 12) {
        float spacing = size / 10.0f;
        float wide = 0;
        float block = 0;
        int i;
        for (i = 0; i < 2; i++) {
            Vector2 m = MeasureTextEx(
                font, caption_lines[i], size, spacing);
            if (m.x > wide)
                wide = m.x;
            block += m.y;
        }
        if (wide <= max_w && block <= max_h)
            return size;
        size -= 2;
    }
    return size;
}

static float fit_footer(Font font, float target_w, float max_h)
{
    float size = 12;
    float chosen = 12;
    while (size < max_h) {
        Vector2 m;
        size += 2;
        m = MeasureTextEx(font, FOOTER, size, size / 10.0f);
        if (m.x > target_w || m.y > max_h)
            return chosen;
        chosen = size;
    }
    return chosen;
}

static void draw_caption(
    Font font, float size, float left, float top, float height)
{
    float spacing = size / 10.0f;
    Vector2 a = MeasureTextEx(
        font, caption_lines[0], size, spacing);
    Vector2 b = MeasureTextEx(
        font, caption_lines[1], size, spacing);
    float gap = a.y / 8.0f;
    float y;
    if (gap < 4)
        gap = 4;
    y = top + (height - (a.y + gap + b.y)) / 2.0f;
    DrawTextEx(
        font, caption_lines[0], (Vector2){left, y},
        size, spacing, (Color){255, 248, 244, 255});
    y += a.y + gap;
    DrawTextEx(
        font, caption_lines[1], (Vector2){left, y},
        size, spacing, (Color){255, 248, 244, 255});
}

static void draw_footer(
    Font font, float size, int screen_w, int screen_h)
{
    int margin, width, right;
    float spacing = size / 10.0f;
    Vector2 m = MeasureTextEx(font, FOOTER, size, spacing);
    footer_box(screen_w, screen_h, &margin, &width, &right);
    DrawTextEx(
        font, FOOTER,
        (Vector2){
            (float)right - m.x,
            (float)screen_h - (float)margin - m.y,
        },
        size, spacing, (Color){255, 248, 244, 255});
    (void)width;
}

static void present(Texture2D tex, int turn, int fw, int fh)
{
    Place place = scene_place(turn, fw, fh);
    Rectangle src = {0, 0, (float)tex.width, -(float)tex.height};
    Rectangle dst = {
        (float)place.x, (float)place.y,
        (float)place.w, (float)place.h,
    };
    DrawTexturePro(
        tex, src, dst, (Vector2){0, 0}, (float)place.rot, WHITE);
}

static int load_sounds(const char *dir, Sound *out)
{
    int n = 0;
    int i;
    if (dir[0] == '\0')
        return 0;
    InitAudioDevice();
    for (i = 0; i < sound_n; i++) {
        char path[512];
        int wrote = snprintf(
            path, sizeof path, "%s/%s.wav", dir, sound_names[i]);
        if (wrote < 0 || (size_t)wrote >= sizeof path)
            continue;
        if (!FileExists(path)) {
            fprintf(stderr, "dsl-bao: missing %s\n", path);
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
    if (count <= 0)
        return;
    if (!IsAudioDeviceReady())
        InitAudioDevice();
    if (!IsAudioDeviceReady())
        return;
    for (i = 0; i < count; i++)
        StopSound(sounds[i]);
    PlaySound(sounds[GetRandomValue(0, count - 1)]);
}

static int take_tap(
    int turn, int lw, int lh, int down,
    double now, double *last, double *tx, double *ty)
{
    int tap = 0;
    Vector2 raw = {0, 0};
    Vec p;
    if (GetTouchPointCount() > 0 && !down) {
        raw = GetTouchPosition(0);
        tap = 1;
    } else if (GetTouchPointCount() == 0
               && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        raw = GetMousePosition();
        tap = 1;
    }
    if (!tap)
        return 0;
    if (*last >= 0 && now - *last < 0.12)
        return 0;
    p = fb_to_logical(turn, lw, lh, raw.x, raw.y);
    *tx = p.x;
    *ty = p.y;
    *last = now;
    return 1;
}

static int run(void)
{
    const char *logo_path = asset("DSL_BAO_LOGO", DSL_BAO_LOGO);
    const char *font_path = asset("DSL_BAO_FONT", DSL_BAO_FONT);
    const char *qr_path = asset("DSL_BAO_QR", DSL_BAO_QR);
    const char *sound_dir = asset("DSL_BAO_SOUNDS", DSL_BAO_SOUNDS);
    const Color bg = {26, 18, 16, 255};
    int turn, fw, fh, lw, lh, span_w, span_h;
    int side, qx, qy, play_top, sound_count, down;
    Texture2D logo, qr;
    Font font;
    RenderTexture2D scene;
    Sound sounds[4];
    Body body;
    double squash = 1;
    double last_tap = -1;
    float cap, foot;

    if (!require_file(logo_path, "logo")
        || !require_file(font_path, "font")
        || !require_file(qr_path, "qr"))
        return 1;

    /* InitWindow(0, 0) copies an unset display size, then
       the nearest-mode search picks the smallest mode. */
    InitWindow(2560, 1440, "dsl-bao");
    if (!IsWindowReady()) {
        fprintf(stderr, "dsl-bao: display did not open\n");
        return 1;
    }
    HideCursor();
    SetTargetFPS(60);
    fw = GetScreenWidth();
    fh = GetScreenHeight();
    fprintf(stderr, "dsl-bao: mode %dx%d\n", fw, fh);
    turn = screen_turn();
    logical_size(turn, fw, fh, &lw, &lh);
    if (lw < 16 || lh < 16) {
        fprintf(stderr, "dsl-bao: mode %dx%d\n", fw, fh);
        CloseWindow();
        return 1;
    }

    logo = LoadTexture(logo_path);
    qr = LoadTexture(qr_path);
    font = LoadFontEx(font_path, 128, NULL, 0);
    if (logo.id == 0 || qr.id == 0 || font.texture.id == 0) {
        fprintf(stderr, "dsl-bao: asset upload failed\n");
        CloseWindow();
        return 1;
    }
    SetTextureFilter(logo, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(qr, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    scene = LoadRenderTexture(lw, lh);
    if (scene.id == 0) {
        fprintf(stderr, "dsl-bao: scene texture failed\n");
        CloseWindow();
        return 1;
    }
    sound_count = load_sounds(sound_dir, sounds);
    logo_span(lw, lh, logo.width, logo.height, &span_w, &span_h);
    header_box(lw, lh, &side, &qx, &qy, &play_top);
    body = (Body){
        lw / 2.0,
        play_top + (lh - play_top) / 2.0,
        CRUISE * 0.72,
        CRUISE * 0.70,
        span_w,
        span_h,
        0,
        0,
    };
    down = 0;
    cap = fit_caption(font, (float)(qx - 2 * qy), (float)side);
    foot = fit_footer(font, (float)(lw / 2), (float)(lh / 8));

    for (;;) {
        double dt = GetFrameTime();
        double now = GetTime();
        double tx = 0;
        double ty = 0;
        double recover;
        Frame fr;
        if (take_tap(
                turn, lw, lh, down, now, &last_tap, &tx, &ty)) {
            if (poke(&body, tx, ty))
                play_hit(sounds, sound_count);
            squash = 0.62;
        }
        down = GetTouchPointCount() > 0;
        if (step(&body, lw, lh, dt, play_top) && squash > 0.85)
            squash = 0.78;
        recover = dt * 8;
        if (recover > 1)
            recover = 1;
        squash = squash + (1 - squash) * recover;
        fr = frame_of(&body, squash, now);
        BeginTextureMode(scene);
        ClearBackground(bg);
        DrawTexturePro(
            qr,
            (Rectangle){0, 0, (float)qr.width, (float)qr.height},
            (Rectangle){(float)qx, (float)qy, (float)side, (float)side},
            (Vector2){0, 0}, 0, WHITE);
        draw_caption(font, cap, (float)qy, (float)qy, (float)side);
        draw_footer(font, foot, lw, lh);
        DrawEllipse(
            fr.sx, fr.sy, fr.shadow_w / 2.0f, fr.shadow_h / 2.0f,
            (Color){0, 0, 0, (unsigned char)fr.alpha});
        DrawTexturePro(
            logo,
            (Rectangle){0, 0, (float)logo.width, (float)logo.height},
            (Rectangle){
                (float)fr.cx, (float)fr.cy,
                (float)fr.width, (float)fr.height,
            },
            (Vector2){fr.width / 2.0f, fr.height / 2.0f},
            (float)(body.spin * (180.0 / k_pi)),
            WHITE);
        EndTextureMode();

        BeginDrawing();
        ClearBackground(bg);
        present(scene.texture, turn, fw, fh);
        EndDrawing();
    }
}

#endif

#ifndef DSL_BAO_NO_MAIN
int main(int argc, char **argv)
{
#ifdef DSL_BAO_HEADLESS
    fprintf(stderr, "dsl-bao: this build has no display\n");
    return 1;
#else
    (void)argc;
    (void)argv;
    return run();
#endif
}
#endif
