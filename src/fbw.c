/*
 * fly-bat-worm: screensaver of the Fly, the Bat and the Worm, from "Anathem".
 *
 * Three creatures with one sense each come upon the same thing. The one picture of
 * it is shown as each of them has it, and then what all three share: its shape.
 *
 *   fly-bat-worm            full screen on every display; any key or mouse movement quits
 *   fly-bat-worm --window   in a window; Esc or Q quits
 *   FlyBatWorm.scr /s       Windows screensaver conventions
 */
#include <SDL.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures (drawn with Gemini, prepared by tools/make_sprites.py) are built
 * into the program as PNG and JPEG files and decoded at start-up. */
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS /* else Windows builds need libwinpthread-1.dll */
#include "stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef _WIN32
#define ART_SECTION ".section .rdata,\"dr\"\n"
#else
#define ART_SECTION ".section .rodata\n"
#endif
#define ART(name, file) \
    __asm__(ART_SECTION ".global art_" #name "\nart_" #name ":\n.incbin \"assets/art/" file "\"\n" \
            ".global art_" #name "_end\nart_" #name "_end:\n.text\n"); \
    extern const unsigned char art_##name[], art_##name##_end[];
ART(bg, "bg.jpg")
ART(banner, "banner.png")
ART(glow, "glow.jpg")
ART(scene, "scene.jpg")
ART(edges, "edges.jpg")
ART(relief, "relief.jpg")
ART(fly, "fly.png")
ART(bat, "bat.png")
ART(worm, "worm.png")

enum { SP_BG, SP_BANNER, SP_GLOW, SP_SCENE, SP_EDGES, SP_RELIEF, SP_FLY, SP_BAT, SP_WORM, NSPRITE };
static struct { const unsigned char *file, *end; int w, h; unsigned char *px; } sprite[NSPRITE] = {
    {art_bg, art_bg_end},
    {art_banner, art_banner_end},
    {art_glow, art_glow_end},
    {art_scene, art_scene_end},
    {art_edges, art_edges_end},
    {art_relief, art_relief_end},
    {art_fly, art_fly_end},
    {art_bat, art_bat_end},
    {art_worm, art_worm_end},
};
static SDL_Texture **cur_tex; /* the pictures, for the renderer being drawn to */

static void load_sprites(void)
{
    for (int i = 0; i < NSPRITE; i++) {
        int n;
        sprite[i].px = stbi_load_from_memory(sprite[i].file, (int)(sprite[i].end - sprite[i].file), &sprite[i].w, &sprite[i].h, &n, 4);
        if (!sprite[i].px) { sprite[i].w = sprite[i].h = 1; }
    }
}
static void make_textures(SDL_Renderer *r, SDL_Texture **tex)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    for (int i = 0; i < NSPRITE; i++) {
        tex[i] = NULL;
        if (!sprite[i].px) continue;
        tex[i] = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, sprite[i].w, sprite[i].h);
        if (!tex[i]) continue;
        SDL_UpdateTexture(tex[i], NULL, sprite[i].px, sprite[i].w * 4);
        /* the glow and the outlines are light on black, and are added to what is behind them */
        SDL_SetTextureBlendMode(tex[i], i == SP_GLOW || i == SP_EDGES ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    }
}

/* ---- options ---- */
static int opt_window = 0;
static double opt_speed = 1.0;
static uint64_t opt_seed = 0;
static const char *opt_snapshot = NULL;
static double opt_time = 30;
static int opt_w = 1920, opt_h = 1200;

/* ---- colours ---- */
typedef struct { Uint8 r, g, b; } Rgb;
static const Rgb BG = {8, 8, 20}, INK = {46, 32, 22}, CHORD = {150, 44, 30}, PAPER = {253, 246, 226};
static const Rgb THREAD = {170, 180, 230}, GOLD = {250, 214, 120}, BLACK = {0, 0, 0};

/* ---- geometry buffer ---- */
static SDL_Vertex *vb;
static int *ib;
static int nv, ni, vcap, icap;

static void vb_need(int v, int i)
{
    if (nv + v > vcap) { vcap = (nv + v) * 2; vb = realloc(vb, sizeof *vb * vcap); }
    if (ni + i > icap) { icap = (ni + i) * 2; ib = realloc(ib, sizeof *ib * icap); }
}
static inline SDL_Color col(Rgb c, double a)
{
    SDL_Color o = {c.r, c.g, c.b, (Uint8)(a < 0 ? 0 : a > 1 ? 255 : a * 255)};
    return o;
}
static inline void vert(double x, double y, SDL_Color c)
{
    vb[nv].position.x = (float)x; vb[nv].position.y = (float)y;
    vb[nv].color = c; vb[nv].tex_coord.x = vb[nv].tex_coord.y = 0;
    nv++;
}
static void quad4(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3,
                  SDL_Color c0, SDL_Color c1, SDL_Color c2, SDL_Color c3)
{
    vb_need(4, 6);
    int b = nv;
    vert(x0, y0, c0); vert(x1, y1, c1); vert(x2, y2, c2); vert(x3, y3, c3);
    ib[ni++] = b; ib[ni++] = b + 1; ib[ni++] = b + 2; ib[ni++] = b; ib[ni++] = b + 2; ib[ni++] = b + 3;
}
static void quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c)
{
    quad4(x0, y0, x1, y1, x2, y2, x3, y3, c, c, c, c);
}
static void rect(double x, double y, double w, double h, SDL_Color c) { quad(x, y, x + w, y, x + w, y + h, x, y + h, c); }
static void disc(double cx, double cy, double r, SDL_Color centre, SDL_Color rim)
{
    int n = r < 4 ? 10 : r < 30 ? 28 : 56;
    vb_need(n + 1, 3 * n);
    int b = nv;
    vert(cx, cy, centre);
    for (int i = 0; i < n; i++) vert(cx + r * cos(i * 2 * M_PI / n), cy + r * sin(i * 2 * M_PI / n), rim);
    for (int i = 0; i < n; i++) { ib[ni++] = b; ib[ni++] = b + 1 + i; ib[ni++] = b + 1 + (i + 1) % n; }
}
/* a stroke with round ends */
static void line(double x0, double y0, double x1, double y1, double w, SDL_Color c)
{
    double dx = x1 - x0, dy = y1 - y0, l = hypot(dx, dy);
    if (l > 1e-9) {
        double nx = -dy / l * w / 2, ny = dx / l * w / 2;
        quad(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
    }
    if (w > 2.5) { disc(x0, y0, w / 2, c, c); disc(x1, y1, w / 2, c, c); }
}
__attribute__((unused)) static void ring(double cx, double cy, double r, double w, SDL_Color c)
{
    for (int i = 0; i < 64; i++) {
        double a = i * M_PI / 32, b = (i + 1) * M_PI / 32;
        line(cx + cos(a) * r, cy + sin(a) * r, cx + cos(b) * r, cy + sin(b) * r, w, c);
    }
}
static void flush(SDL_Renderer *r)
{
    if (ni) SDL_RenderGeometry(r, NULL, vb, nv, ib, ni);
    nv = ni = 0;
}

static SDL_Renderer *ren; /* where the current frame is being drawn */

/* a picture stretched over a rectangle; sx0..sx1 picks a slice of it across (0..1) */
static void pic_part(int which, double x, double y, double w, double h, double sx0, double sx1, double tilt, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    SDL_Rect src = {(int)(sx0 * sprite[which].w), 0, (int)((sx1 - sx0) * sprite[which].w), sprite[which].h};
    SDL_FRect dst = {(float)x, (float)y, (float)w, (float)h};
    SDL_FPoint pivot = {(float)(w / 2), (float)h};
    SDL_RenderCopyExF(ren, cur_tex[which], &src, &dst, tilt, &pivot, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}
/* a picture `tall` high, standing with the point (ax, ay) of it (0..1 across and down) at (x, y) */
__attribute__((unused)) static void pic(int which, double x, double y, double tall, double ax, double ay)
{
    double w = tall * sprite[which].w / sprite[which].h;
    pic_part(which, x - w * ax, y - tall * ay, w, tall, 0, 1, 0, 0);
}

/* ---- lettering: pen strokes on a 4 x 6 grid, drawn with a round nib ---- */
static const char *GLYPH[] = {
    "A06 02 20 42 46|04 44", "B00 06 36 45 44 33 03|33 42 41 30 00", "C41 30 10 01 05 16 36 45",
    "D00 06 26 44 42 20 00", "E40 00 06 46|03 33", "F40 00 06|03 33", "G41 30 10 01 05 16 36 45 43 23",
    "H00 06|40 46|03 43", "I20 26|10 30|16 36", "J40 45 36 16 05", "K00 06|40 03 46", "L00 06 46",
    "M06 00 23 40 46", "N06 00 46 40", "O10 30 41 45 36 16 05 01 10", "P06 00 30 41 42 33 03",
    "Q10 30 41 45 36 16 05 01 10|24 46", "R06 00 30 41 42 33 03|23 46", "S41 30 10 01 02 13 33 44 45 36 16 05",
    "T00 40|20 26", "U00 05 16 36 45 40", "V00 26 40", "W00 16 23 36 40", "X00 46|40 06", "Y00 23 40|23 26",
    "Z00 40 06 46", "010 30 41 45 36 16 05 01 10", "111 20 26|16 36", "201 10 30 41 42 06 46",
    "301 10 30 41 42 33 23|33 44 45 36 16 05", "436 30 04 44", "540 00 03 33 44 45 36 16 05",
    "630 10 01 05 16 36 45 44 33 03", "700 40 16", "813 02 01 10 30 41 42 33 13 04 05 16 36 45 44 33",
    "943 13 02 01 10 30 41 45 36 16", ",25 26 17", ".26 26", ":22 22|25 25", "'20 21", "?01 10 30 41 42 24|26 26",
    "-13 33", "!20 24|26 26", "+03 43|21 25", "=02 42|04 44",
};
static const char *glyph(char ch)
{
    for (unsigned i = 0; i < sizeof GLYPH / sizeof *GLYPH; i++)
        if (GLYPH[i][0] == ch) return GLYPH[i] + 1;
    return NULL;
}
static double text_width(const char *str, double s) { return strlen(str) * 6.2 * s - 2.2 * s; }
/* text centred on cx, top at y; s is one grid step, so letters are 6s tall */
static void text(const char *str, double cx, double y, double s, double weight, SDL_Color c)
{
    double x = cx - text_width(str, s) / 2;
    for (; *str; str++, x += 6.2 * s) {
        const char *g = glyph(*str);
        if (!g) continue;
        int have = 0; double lx = 0, ly = 0;
        for (; *g; g++) {
            if (*g == '|') { have = 0; continue; }
            if (*g == ' ') continue;
            double gx = x + (g[0] - '0') * s, gy = y + (g[1] - '0') * s;
            g++;
            if (have) line(lx, ly, gx, gy, s * weight, c);
            disc(gx, gy, s * weight * (have && lx == gx && ly == gy ? 0.62 : 0.5), c, c); /* the nib is round */
            lx = gx; ly = gy; have = 1;
        }
    }
}
/* ---- the three ----
 *
 * A Fly that has only sight, a Bat that has only hearing and a Worm that has only
 * touch come upon the same thing: a clockwork trap in a cave. The one painting of
 * it is shown three ways, side by side.
 *
 *   The Fly sees it as a mosaic, one patch of colour to each facet of its eyes.
 *   The Bat hears its own cries come back: each cry is a ring going out from the
 *   Bat, and the outlines of things show where the ring is passing.
 *   The Worm knows only the surface it has crawled over.
 *
 * Then the three are brought together, and what is left that all of them have is
 * the shape of the thing. */
static double T, anim_t;
static uint64_t seed_state;
#define T_FLY 8.0
#define T_BAT 24.0
#define T_WORM 40.0
#define T_ALL 56.0
#define T_MERGE 68.0
#define ROUND 86.0

static uint64_t next_rand(void)
{
    seed_state ^= seed_state << 13; seed_state ^= seed_state >> 7; seed_state ^= seed_state << 17;
    return seed_state;
}
static double ramp(double t, double a, double b) { return t <= a ? 0 : t >= b ? 1 : (t - a) / (b - a); }
static double ease(double x) { return x * x * (3 - 2 * x); }

static void new_round(void) { T = 0; (void)next_rand(); }
static void update(double dt)
{
    anim_t += dt;
    T += dt * opt_speed;
    if (T > ROUND) new_round();
}

/* ---- drawing ---- */
static double unit;
static int sw, sh;
static double PX, PY, PS; /* the panel being drawn: its corner and its side */

static SDL_Vertex pv(double x, double y, Uint8 r, Uint8 g, Uint8 b, double a)
{
    SDL_Vertex v = {{(float)x, (float)y}, {r, g, b, (Uint8)(fmin(fmax(a, 0), 1) * 255)}, {(float)((x - PX) / PS), (float)((y - PY) / PS)}};
    return v; /* the picture is pinned to the panel, so a point of the screen always shows the same point of it */
}
static void geom(int which, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
    flush(ren);
    if (cur_tex && cur_tex[which]) SDL_RenderGeometry(ren, cur_tex[which], v, nv, idx, ni);
}
static void whole(int which, Uint8 r, Uint8 g, Uint8 b, double a)
{
    SDL_Vertex v[4] = {pv(PX, PY, r, g, b, a), pv(PX + PS, PY, r, g, b, a), pv(PX + PS, PY + PS, r, g, b, a), pv(PX, PY + PS, r, g, b, a)};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    geom(which, v, 4, idx, 6);
}
__attribute__((unused)) static void glow(double x, double y, double d, double alpha)
{
    flush(ren);
    if (!cur_tex || !cur_tex[SP_GLOW] || alpha <= 0) return;
    SDL_FRect dst = {(float)(x - d / 2), (float)(y - d / 2), (float)d, (float)d};
    SDL_SetTextureAlphaMod(cur_tex[SP_GLOW], (Uint8)(fmin(alpha, 1) * 255));
    SDL_RenderCopyF(ren, cur_tex[SP_GLOW], NULL, &dst);
    SDL_SetTextureAlphaMod(cur_tex[SP_GLOW], 255);
}
static void creature(int which, double x, double y, double wide, double deg, int flip)
{
    flush(ren);
    if (!cur_tex || !cur_tex[which]) return;
    double hgt = wide * sprite[which].h / sprite[which].w;
    SDL_FRect dst = {(float)(x - wide / 2), (float)(y - hgt / 2), (float)wide, (float)hgt};
    SDL_RenderCopyExF(ren, cur_tex[which], NULL, &dst, deg, NULL, flip ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

/* What the Fly sees: the picture broken into the facets of its eyes, each facet one
 * flat patch of colour. It starts coarse and sharpens as the Fly comes nearer. */
static void fly_view(double t)
{
    static SDL_Vertex v[7 * 2400];
    static int idx[18 * 2400];
    int n = 0, cells = 8 + (int)(26 * ease(ramp(t, 0, 9)));
    double hr = PS / cells / 1.732, flick = 0.5 + 0.5 * sin(anim_t * 3);
    for (int row = -1; row * 1.5 * hr < PS + hr; row++)
        for (int c = -1; c * 1.732 * hr < PS + hr && n < 2400; c++) {
            double cx = PX + (c + (row & 1) * 0.5) * 1.732 * hr, cy = PY + row * 1.5 * hr;
            double u = fmin(fmax((cx - PX) / PS, 0.01), 0.99), w = fmin(fmax((cy - PY) / PS, 0.01), 0.99);
            unsigned hsh = (unsigned)(row * 7919 + c * 104729);
            Uint8 g = (Uint8)(218 + 37 * (((hsh >> 3) & 7) / 7.0) * (0.6 + 0.4 * flick)); /* no two facets quite alike */
            for (int k = 0; k < 7; k++) {
                double a = (k - 1) * M_PI / 3 + M_PI / 6, x = k ? cx + cos(a) * hr * 0.93 : cx, y = k ? cy + sin(a) * hr * 0.93 : cy;
                /* all of a facet takes its colour from one spot of the picture (a hair's breadth
                 * of it, since a triangle with no size at all in the picture is not drawn reliably) */
                v[n * 7 + k] = (SDL_Vertex){{(float)x, (float)y}, {g, g, g, 255}, {(float)(u + (k ? cos(a) * 0.004 : 0)), (float)(w + (k ? sin(a) * 0.004 : 0))}};
            }
            for (int k = 0; k < 6; k++) { idx[n * 18 + k * 3] = n * 7; idx[n * 18 + k * 3 + 1] = n * 7 + 1 + k; idx[n * 18 + k * 3 + 2] = n * 7 + 1 + (k + 1) % 6; }
            n++;
        }
    geom(SP_SCENE, v, n * 7, idx, n * 18);
}

/* What the Bat hears: its cries go out as rings, and where a ring is passing, the
 * edges it strikes send it back. What has been heard lingers faintly. */
static void bat_view(double t, double bx, double by)
{
    whole(SP_EDGES, 120, 190, 230, 0.13 * ramp(t, 2, 8));
    for (int k = 0; k < 4; k++) {
        double age = fmod(t + k * 0.9, 3.6), r = age * PS * 0.42, band = PS * 0.16, a = (1 - age / 3.6);
        if (t < k * 0.9) continue;
        SDL_Vertex v[2 * 65];
        int idx[6 * 64];
        for (int i = 0; i <= 64; i++) {
            double an = i * M_PI / 32;
            v[2 * i] = pv(bx + cos(an) * fmax(r - band, 0), by + sin(an) * fmax(r - band, 0), 150, 225, 255, 0);
            v[2 * i + 1] = pv(bx + cos(an) * r, by + sin(an) * r, 150, 225, 255, a);
        }
        for (int i = 0; i < 64; i++) { int *q = idx + i * 6; q[0] = 2 * i; q[1] = 2 * i + 1; q[2] = 2 * i + 3; q[3] = 2 * i; q[4] = 2 * i + 3; q[5] = 2 * i + 2; }
        geom(SP_EDGES, v, 130, idx, 384);
        for (int i = 0; i < 64; i++) /* the cry itself, a thin ring */
            line(bx + cos(i * M_PI / 32) * r, by + sin(i * M_PI / 32) * r, bx + cos((i + 1) * M_PI / 32) * r, by + sin((i + 1) * M_PI / 32) * r, unit * 0.1, col(THREAD, 0.3 * a));
    }
    /* and the trap ticks */
    double tick = fmod(t, 1.0), tx = PX + PS * 0.8, ty = PY + PS * 0.52; /* where the clockwork is */
    for (int i = 0; i < 40; i++)
        line(tx + cos(i * M_PI / 20) * tick * PS * 0.16, ty + sin(i * M_PI / 20) * tick * PS * 0.16, tx + cos((i + 1) * M_PI / 20) * tick * PS * 0.16,
             ty + sin((i + 1) * M_PI / 20) * tick * PS * 0.16, unit * 0.16, col(GOLD, 0.7 * (1 - tick)));
    flush(ren);
}

/* The Worm's road: along the floor, up over the dome of the cage, down past the
 * clockwork and off along the floor again. */
static void worm_at(double s, double *x, double *y)
{
    static const double P[9][2] = {{0.02, 0.85}, {0.18, 0.84}, {0.22, 0.52}, {0.42, 0.24}, {0.62, 0.17}, {0.8, 0.38}, {0.9, 0.6}, {0.91, 0.83}, {0.99, 0.85}};
    double f = fmin(fmax(s, 0), 0.9999) * 8;
    int i = (int)f;
    f -= i;
    *x = PX + PS * (P[i][0] + (P[i + 1][0] - P[i][0]) * f); *y = PY + PS * (P[i][1] + (P[i + 1][1] - P[i][1]) * f);
}
/* What the Worm feels: only the surface it has been over. */
static void worm_view(double t, double *hx, double *hy, double *hdeg)
{
    double s = ramp(t, 0.5, 14.5), rho = PS * 0.095;
    for (int k = 0; k <= 70; k++) {
        double q = k / 70.0, x, y;
        if (q > s) break;
        worm_at(q, &x, &y);
        SDL_Vertex v[18];
        int idx[48];
        v[0] = pv(x, y, 255, 236, 205, 0.55);
        for (int i = 0; i <= 16; i++) v[1 + i] = pv(x + cos(i * M_PI / 8) * rho, y + sin(i * M_PI / 8) * rho, 255, 236, 205, 0);
        for (int i = 0; i < 16; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = 1 + i; idx[i * 3 + 2] = 2 + i; }
        geom(SP_RELIEF, v, 18, idx, 48);
    }
    double x0, y0, x1, y1;
    worm_at(s, &x1, &y1); worm_at(s - 0.02, &x0, &y0);
    *hx = x1; *hy = y1; *hdeg = atan2(y1 - y0, x1 - x0) * 180 / M_PI;
    /* the ticking, felt through the stone */
    double tick = fmod(t, 1.0);
    for (int i = -3; i <= 3; i++)
        line(x1 + i * unit * 0.9, y1 + rho * 0.5, x1 + i * unit * 0.9, y1 + rho * 0.5 + unit * (0.5 + 0.8 * (1 - tick)) * (1 - fabs(i) / 4.0), unit * 0.12, col(GOLD, 0.6 * (1 - tick)));
    flush(ren);
}

static void frame(double alpha, const char *who, const char *sense, double lit)
{
    double b = unit * 0.45;
    rect(PX - b, PY - b, PS + 2 * b, b, col(THREAD, alpha)); rect(PX - b, PY + PS, PS + 2 * b, b, col(THREAD, alpha));
    rect(PX - b, PY, b, PS, col(THREAD, alpha)); rect(PX + PS, PY, b, PS, col(THREAD, alpha));
    if (who) {
        text(who, PX + PS / 2, PY + PS + unit * 1.6, unit * 0.34, 1, col(PAPER, alpha * (0.45 + 0.55 * lit)));
        text(sense, PX + PS / 2, PY + PS + unit * 4.6, unit * 0.22, 0.9, col(GOLD, alpha * (0.4 + 0.6 * lit)));
    }
    flush(ren);
}
static void clip(int on)
{
    flush(ren);
    if (on) { SDL_Rect c = {(int)PX, (int)PY, (int)PS, (int)PS}; SDL_RenderSetClipRect(ren, &c); }
    else SDL_RenderSetClipRect(ren, NULL);
}

static void draw_caption(int w, int h, const char *l1, const char *l2)
{
    double s1 = unit * 0.46, s2 = unit * 0.3;
    double widest = fmax(text_width(l1, s1), text_width(l2, s2)), room = w * 0.66;
    if (widest > room) { s1 *= room / widest; s2 *= room / widest; }
    double bw = fmax(text_width(l1, s1), text_width(l2, s2)) + unit * 13, bh = unit * 11.5;
    double bx = w / 2.0 - bw / 2, by = unit * 1.2;
    double cap = 0.135, cw = bh * 1.15 * sprite[SP_BANNER].w * cap / sprite[SP_BANNER].h;
    pic_part(SP_BANNER, bx + cw - 1, by, bw - 2 * cw + 2, bh, cap, 1 - cap, 0, 0);
    pic_part(SP_BANNER, bx, by, cw, bh, 0, cap, 0, 0);
    pic_part(SP_BANNER, bx + bw - cw, by, cw, bh, 1 - cap, 1, 0, 0);
    text(l1, w / 2.0, by + bh * 0.25, s1, 0.95, col(INK, 1));
    text(l2, w / 2.0, by + bh * 0.25 + s1 * 6 + unit * 1.2, s2, 0.9, col(CHORD, 1));
}

static void draw_scene(SDL_Renderer *r, int w, int h)
{
    ren = r; sw = w; sh = h;
    unit = (w < h ? w : h) * 0.0125;
    SDL_SetRenderDrawColor(r, BG.r, BG.g, BG.b, 255);
    SDL_RenderClear(r);
    pic_part(SP_BG, 0, 0, w, h, 0, 1, 0, 0);

    /* one panel in the middle splits into three, and at the end they come together again */
    double apart = ease(ramp(T, T_FLY - 2.5, T_FLY - 0.5)) * (1 - ease(ramp(T, T_MERGE, T_MERGE + 2.5)));
    double side = fmin(h * 0.58, w * 0.3), big = fmin(h * 0.66, w * 0.5), s0 = big + (side - big) * apart;
    double y0 = h * 0.19 + (big - s0) * 0.1, gap = w * 0.325 * apart;
    double whole_shown = 1 - apart; /* how much the middle panel is the thing itself and not the Bat's hearing of it */

    /* the Fly */
    if (apart > 0.02) {
        PS = s0; PX = w / 2.0 - gap - PS / 2; PY = y0;
        double t = T - T_FLY;
        rect(PX, PY, PS, PS, col(BLACK, apart));
        clip(1);
        if (t > 0) {
            fly_view(t);
            creature(SP_FLY, PX + PS * 0.17 + sin(anim_t * 2.1) * PS * 0.03, PY + PS * 0.8 + sin(anim_t * 3.3) * PS * 0.02, PS * 0.18, sin(anim_t * 5) * 4, 0);
        }
        clip(0);
        frame(apart, "THE FLY", "SIGHT, AND NOTHING ELSE", T >= T_FLY && (T < T_BAT || T >= T_ALL));
    }
    /* the Worm */
    if (apart > 0.02) {
        PS = s0; PX = w / 2.0 + gap - PS / 2; PY = y0;
        double t = T - T_WORM, hx, hy, hd;
        rect(PX, PY, PS, PS, col(BLACK, apart));
        clip(1);
        if (t > 0) {
            worm_view(t, &hx, &hy, &hd);
            creature(SP_WORM, hx, hy - PS * 0.02 + sin(anim_t * 6) * PS * 0.004, PS * 0.17, hd, 0);
        }
        clip(0);
        frame(apart, "THE WORM", "TOUCH, AND NOTHING ELSE", T >= T_WORM);
    }
    /* the middle: the thing itself to begin and end with, and the Bat's hearing of it between */
    PS = s0; PX = w / 2.0 - PS / 2; PY = y0;
    rect(PX, PY, PS, PS, col(BLACK, 1));
    clip(1);
    if (whole_shown > 0.01) {
        double end = ramp(T, T_MERGE + 2, T_MERGE + 5);
        int dim = T >= T_MERGE ? (int)(255 - 120 * end) : 255;
        whole(SP_SCENE, dim, dim, dim, whole_shown);
        if (T >= T_MERGE) whole(SP_EDGES, 255, 214, 120, end * whole_shown); /* its shape, drawn over it */
    }
    if (apart > 0.02 && T >= T_BAT) {
        double bx = PX + PS * 0.2, by = PY + PS * 0.2;
        bat_view(T - T_BAT, bx, by);
        creature(SP_BAT, bx, by - PS * 0.02, PS * 0.2, sin(anim_t * 1.7) * 3, 0);
    }
    clip(0);
    if (apart > 0.5) frame(apart, "THE BAT", "HEARING, AND NOTHING ELSE", T >= T_BAT && (T < T_WORM || T >= T_ALL));
    else frame(1, NULL, NULL, 1);
    if (T >= T_MERGE + 2.5) { /* the three of them, gathered round what they have agreed on */
        double a = ramp(T, T_MERGE + 2.5, T_MERGE + 4);
        (void)a;
        creature(SP_FLY, PX - PS * 0.16, PY + PS * 0.3 + sin(anim_t * 3.3) * PS * 0.02, PS * 0.2, sin(anim_t * 5) * 4, 0);
        creature(SP_BAT, PX + PS * 1.16, PY + PS * 0.18, PS * 0.2, sin(anim_t * 1.7) * 3, 0);
        creature(SP_WORM, PX - PS * 0.16, PY + PS * 0.86, PS * 0.22, 0, 0);
    }

    const char *l1 = "A TRAP IN A CAVE", *l2 = "A FLY, A BAT AND A WORM COME UPON IT";
    if (T >= T_FLY - 2) { l1 = "THE FLY SEES IT"; l2 = "A PATCH OF COLOUR FOR EACH FACET OF ITS EYES. IT HEARS NOTHING"; }
    if (T >= T_BAT) { l1 = "THE BAT HEARS IT"; l2 = "ITS CRIES COME BACK FROM EVERY EDGE, AND THE TRAP TICKS. IT SEES NOTHING"; }
    if (T >= T_WORM) { l1 = "THE WORM FEELS IT"; l2 = "IT KNOWS ONLY THE SURFACE IT HAS CRAWLED OVER"; }
    if (T >= T_ALL) { l1 = "NONE CAN TELL THE OTHERS WHAT IT IS LIKE"; l2 = "BUT EACH CAN SAY WHERE THINGS ARE"; }
    if (T >= T_MERGE) { l1 = "WHAT THEY SHARE IS ITS SHAPE"; l2 = "ANY MIND THAT SENSES A WORLD MUST BUILD IT IN SPACE AND TIME"; }
    draw_caption(w, h, l1, l2);
    flush(r);
    double fade = fmin(ramp(T, 0, 1.2), 1 - ramp(T, ROUND - 1.2, ROUND - 0.1));
    if (fade < 1) { rect(0, 0, w, h, col(BG, 1 - fade)); flush(r); }
}

typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *scene, *tex[NSPRITE]; int w, h; } View;

static void size_view(View *v)
{
    int ow, oh;
    SDL_RendererInfo info;
    SDL_GetRendererOutputSize(v->ren, &ow, &oh);
    if (ow < 1 || oh < 1) return;
    int ss = 2; /* drawn at twice the size and scaled down, for smooth edges */
    if (SDL_GetRendererInfo(v->ren, &info) == 0) {
        if (!(info.flags & SDL_RENDERER_TARGETTEXTURE) || (info.flags & SDL_RENDERER_SOFTWARE)) ss = 0;
        else if (info.max_texture_width && (ow * 2 > info.max_texture_width || oh * 2 > info.max_texture_height)) ss = 0;
    }
    if (v->w == ow * (ss ? ss : 1) && v->h == oh * (ss ? ss : 1) && (v->scene || !ss)) return;
    if (v->scene) SDL_DestroyTexture(v->scene);
    v->scene = NULL; v->w = ow; v->h = oh;
    if (!ss) return;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    v->scene = SDL_CreateTexture(v->ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, ow * ss, oh * ss);
    if (v->scene) { v->w = ow * ss; v->h = oh * ss; }
}
static void render(View *v)
{
    if (v->scene) SDL_SetRenderTarget(v->ren, v->scene);
    cur_tex = v->tex;
    draw_scene(v->ren, v->w, v->h);
    if (v->scene) { SDL_SetRenderTarget(v->ren, NULL); SDL_RenderCopy(v->ren, v->scene, NULL, NULL); }
    SDL_RenderPresent(v->ren);
}
static int make_view(View *v, int display)
{
    Uint32 flags = SDL_WINDOW_ALLOW_HIGHDPI;
    int x = SDL_WINDOWPOS_CENTERED_DISPLAY(display);
    if (opt_window) flags |= SDL_WINDOW_RESIZABLE;
    else flags |= SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_BORDERLESS;
    memset(v, 0, sizeof *v);
    v->win = SDL_CreateWindow("Fly, Bat, Worm", x, x, 1200, 750, flags);
    if (!v->win) return 0;
    v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
    if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_SOFTWARE);
    if (!v->ren) return 0;
    SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
    make_textures(v->ren, v->tex);
    return 1;
}

static void start_show(void)
{
    seed_state = opt_seed ? opt_seed * 0x9E3779B97F4A7C15ULL : ((uint64_t)time(NULL) << 20) ^ SDL_GetPerformanceCounter();
    if (!seed_state) seed_state = 1;
    for (int i = 0; i < 6; i++) next_rand();
    anim_t = 0;
    new_round();
}

static int snapshot(void)
{
    SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, opt_w, opt_h, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = surf ? SDL_CreateSoftwareRenderer(surf) : NULL;
    if (!r) { fprintf(stderr, "fly-bat-worm: %s\n", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_Texture *tex[NSPRITE];
    make_textures(r, tex);
    cur_tex = tex;
    start_show();
    for (double t = 0; t < opt_time; t += 1.0 / 60) update(1.0 / 60);
    draw_scene(r, opt_w, opt_h);
    SDL_RenderPresent(r);
    if (SDL_SaveBMP(surf, opt_snapshot) != 0) { fprintf(stderr, "fly-bat-worm: %s\n", SDL_GetError()); return 1; }
    return 0;
}

static void usage(void)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Fly, Bat, Worm",
        "Fly, Bat, Worm screensaver\n\n"
        "  --window        run in a window (Esc or Q quits)\n"
        "  --speed X       animation speed, 1 = normal\n"
        "  --seed N        repeatable sequence\n"
        "  --snapshot FILE.bmp [--time SECONDS] [--size WxH]\n"
        "                  write a picture of the screensaver after SECONDS and exit\n", NULL);
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && (a[1] == 's' || a[1] == 'S') && !a[2]) continue;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'p' || a[1] == 'P') && (!a[2] || a[2] == ':')) return 0;
        else if ((a[0] == '/' || a[0] == '-') && (a[1] == 'c' || a[1] == 'C') && (!a[2] || a[2] == ':')) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Fly, Bat, Worm", "Fly, Bat, Worm screensaver has no settings.", NULL);
            return 0;
        }
        else if (!strcmp(a, "--window")) opt_window = 1;
        else if (!strcmp(a, "--speed") && i + 1 < argc) opt_speed = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--seed") && i + 1 < argc) opt_seed = SDL_strtoull(argv[++i], NULL, 10);
        else if (!strcmp(a, "--snapshot") && i + 1 < argc) opt_snapshot = argv[++i];
        else if (!strcmp(a, "--time") && i + 1 < argc) opt_time = SDL_atof(argv[++i]);
        else if (!strcmp(a, "--size") && i + 1 < argc) {
            const char *x = SDL_strchr(argv[++i], 'x');
            opt_w = SDL_atoi(argv[i]); opt_h = x ? SDL_atoi(x + 1) : 0;
            if (opt_w < 64 || opt_h < 64) { usage(); return 1; }
        }
        else { usage(); return 1; }
    }
    if (opt_speed < 0.05) opt_speed = 0.05;
    load_sprites();

    if (opt_snapshot) return snapshot();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "fly-bat-worm: %s\n", SDL_GetError()); return 1; }

    View views[8];
    int nviews = 0, ndisp = opt_window ? 1 : SDL_GetNumVideoDisplays();
    if (ndisp < 1) ndisp = 1;
    for (int d = 0; d < ndisp && nviews < 8; d++)
        if (make_view(&views[nviews], d)) nviews++;
    if (!nviews) { fprintf(stderr, "fly-bat-worm: %s\n", SDL_GetError()); return 1; }
    if (!opt_window) SDL_ShowCursor(SDL_DISABLE);
    start_show();

    Uint64 freqc = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter(), start = last;
    double moved = 0;
    int running = 1;
    while (running) {
        SDL_Event e;
        double since = (double)(SDL_GetPerformanceCounter() - start) / freqc;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
            else if (e.type == SDL_KEYDOWN) {
                if (!opt_window || e.key.keysym.sym == SDLK_ESCAPE || e.key.keysym.sym == SDLK_q) running = 0;
            } else if (!opt_window && since > 1.0) {
                if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEWHEEL || e.type == SDL_FINGERDOWN) running = 0;
                else if (e.type == SDL_MOUSEMOTION) {
                    moved += abs(e.motion.xrel) + abs(e.motion.yrel);
                    if (moved > 24) running = 0;
                }
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        double dt = (double)(now - last) / freqc;
        last = now;
        if (dt > 0.1) dt = 0.1;
        update(dt);
        for (int i = 0; i < nviews; i++) { size_view(&views[i]); render(&views[i]); }
        SDL_Delay(views[0].scene ? 1 : 12);
    }
    SDL_Quit();
    return 0;
}





