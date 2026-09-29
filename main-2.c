/*
 * Mini BR - Stage 1
 * A PSP first-person shooter with a setup menu, 8 map environments,
 * 3 weather types, bot opponents and two modes:
 *   - SOLO SURVIVAL: free-for-all, last one standing wins
 *   - TEAM DEATHMATCH: 6 vs 6, first team to 25 kills wins
 *
 * Controls (in match)
 *   Analog stick ... move / strafe
 *   D-pad L/R  or  Square/Circle ... turn
 *   R trigger ... fire      L trigger ... sprint
 *   Cross ....... reload    Start ....... back to menu
 * Controls (menu)
 *   D-pad up/down ... pick row, left/right ... change, Cross ... deploy
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

PSP_MODULE_INFO("MiniBR", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);

#define SCR_W 480
#define SCR_H 272
#define BUF_W 512
#define MAP_W 24
#define MAP_H 24
#define MAX_BOTS 12
#define MAG_SIZE 12
#define NUM_ENVS 8
#define TDM_TARGET 25

enum { MODE_SOLO = 0, MODE_TDM = 1 };
enum { ST_MENU = 0, ST_PLAY = 1, ST_OVER = 2 };

/* ---------- exit callback (HOME button) ---------- */
static int running = 1;

static int exit_callback(int a1, int a2, void *c)
{
    running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int th = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0)
        sceKernelStartThread(th, 0, 0);
}

/* ---------- map ---------- */
static char map[MAP_H][MAP_W];

typedef struct
{
    int x, y, w, h;
    char t;
} Rect;

static const Rect rects[] = {
    {2, 2, 2, 1, '2'},   {2, 3, 1, 1, '2'},   {20, 2, 2, 1, '3'},  {21, 3, 1, 1, '3'},
    {9, 4, 3, 1, '1'},   {9, 5, 1, 1, '1'},   {16, 6, 2, 1, '1'},  {17, 7, 1, 1, '1'},
    {3, 7, 2, 1, '3'},   {3, 8, 1, 1, '3'},   {10, 9, 1, 2, '1'},  {20, 9, 1, 2, '2'},
    {12, 11, 2, 2, '1'}, {4, 13, 1, 2, '2'},  {17, 13, 2, 2, '1'}, {9, 15, 2, 1, '1'},
    {9, 16, 1, 1, '1'},  {20, 16, 2, 1, '3'}, {21, 17, 1, 1, '3'}, {4, 18, 2, 1, '3'},
    {4, 19, 1, 1, '3'},  {11, 19, 3, 1, '1'}, {11, 20, 1, 1, '1'}, {19, 20, 2, 1, '2'},
    {6, 10, 1, 1, '2'},  {15, 16, 1, 1, '2'}};

static void build_map(void)
{
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            map[y][x] = (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1) ? '1' : '0';
    int n = (int)(sizeof(rects) / sizeof(rects[0]));
    for (int i = 0; i < n; i++)
        for (int y = 0; y < rects[i].h; y++)
            for (int x = 0; x < rects[i].w; x++)
                map[rects[i].y + y][rects[i].x + x] = rects[i].t;
}

/* ---------- environments ---------- */
typedef struct
{
    const char *name;
    int sky0[3], sky1[3]; /* top of sky, horizon */
    int fl0[3], fl1[3];   /* floor at horizon, floor near you */
    int wall[3][3];
} Env;

static const Env envs[NUM_ENVS] = {
    {"DESERT", {110, 170, 230}, {240, 210, 160}, {215, 190, 140}, {170, 135, 85},
     {{200, 160, 100}, {170, 90, 60}, {110, 80, 60}}},
    {"FOREST", {100, 160, 220}, {190, 220, 240}, {70, 110, 60}, {40, 75, 35},
     {{110, 80, 50}, {120, 125, 120}, {60, 110, 60}}},
    {"SNOW", {150, 180, 215}, {230, 240, 250}, {235, 240, 245}, {200, 210, 225},
     {{150, 200, 230}, {110, 115, 125}, {50, 90, 70}}},
    {"CITY", {90, 120, 170}, {180, 190, 205}, {80, 80, 85}, {50, 50, 55},
     {{150, 150, 155}, {150, 70, 60}, {70, 110, 150}}},
    {"JUNGLE", {90, 170, 150}, {170, 220, 190}, {60, 100, 40}, {30, 60, 25},
     {{80, 120, 50}, {100, 80, 50}, {60, 60, 50}}},
    {"VOLCANO", {70, 30, 30}, {200, 90, 40}, {70, 45, 40}, {35, 25, 25},
     {{60, 55, 55}, {170, 60, 20}, {90, 40, 35}}},
    {"SWAMP", {110, 130, 110}, {170, 180, 140}, {80, 90, 55}, {45, 55, 35},
     {{90, 100, 70}, {70, 80, 60}, {110, 100, 80}}},
    {"BEACH", {90, 190, 240}, {220, 240, 250}, {240, 225, 175}, {210, 190, 140},
     {{230, 210, 170}, {60, 150, 170}, {200, 120, 80}}}};

static const char *weather_names[3] = {"SUNNY", "RAIN", "NIGHT"};
static const char *mode_names[2] = {"SOLO SURVIVAL", "TEAM DEATHMATCH"};

/* weather: colour tint and fog strength */
static float wt[3] = {1, 1, 1};
static float fog_k = 0.02f;

static void set_weather(int w)
{
    if (w == 0) { wt[0] = 1.0f; wt[1] = 1.0f; wt[2] = 1.0f; fog_k = 0.02f; }
    else if (w == 1) { wt[0] = 0.70f; wt[1] = 0.75f; wt[2] = 0.85f; fog_k = 0.07f; }
    else { wt[0] = 0.30f; wt[1] = 0.38f; wt[2] = 0.60f; fog_k = 0.12f; }
}

/* ---------- game data ---------- */
typedef struct
{
    float x, y, ang;
    int hp, alive, team;
    int flash, cool, respawn, gt, moving;
    float gx, gy;
} Bot;

typedef struct
{
    float x, y, ang;
    float hp, stamina;
    int mag, reserve;
    int cooldown, reload;
    int regen_delay;
    int kills;
    int hitmarker, recoil, flash, dmg_flash;
    int dead, dead_timer;
} Player;

typedef struct
{
    int x, y, s;
} Drop;

static unsigned int backbuf[BUF_W * SCR_H];
static float zbuf[SCR_W];
static Bot bots[MAX_BOTS];
static int nbots = 0;
static Player pl;
static Drop drops[100];
static int stars[40][2];

static int state = ST_MENU;
static int result = 0; /* 0 defeat, 1 victory, 2 eliminated */
static int cfg_mode = MODE_TDM, cfg_map = 0, cfg_weather = 0, menu_row = 0;
static int cur_mode = MODE_TDM, cur_env = 0, cur_weather = 0;
static int team_score[2];
static int last_hit_team = 1;

/* ---------- drawing helpers ---------- */
static unsigned int rgb(int r, int g, int b)
{
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return 0xFF000000u | ((unsigned int)b << 16) | ((unsigned int)g << 8) | (unsigned int)r;
}

/* colour with current weather tint and brightness factor */
static unsigned int tc(int r, int g, int b, float f)
{
    return rgb((int)(r * wt[0] * f), (int)(g * wt[1] * f), (int)(b * wt[2] * f));
}

static int lerp(int a, int b, float t)
{
    return (int)(a + (b - a) * t);
}

static void fill_rect(int x, int y, int w, int h, unsigned int c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) w = SCR_W - x;
    if (y + h > SCR_H) h = SCR_H - y;
    if (w <= 0 || h <= 0) return;
    for (int j = 0; j < h; j++)
    {
        unsigned int *p = &backbuf[(y + j) * BUF_W + x];
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

static const unsigned char font[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}};

static const unsigned char alpha[26][5] = {
    {2, 5, 7, 5, 5}, {6, 5, 6, 5, 6}, {3, 4, 4, 4, 3}, {6, 5, 5, 5, 6}, {7, 4, 6, 4, 7},
    {7, 4, 6, 4, 4}, {3, 4, 5, 5, 3}, {5, 5, 7, 5, 5}, {7, 2, 2, 2, 7}, {1, 1, 1, 5, 2},
    {5, 5, 6, 5, 5}, {4, 4, 4, 4, 7}, {5, 7, 7, 5, 5}, {6, 5, 5, 5, 5}, {2, 5, 5, 5, 2},
    {6, 5, 6, 4, 4}, {2, 5, 5, 6, 3}, {6, 5, 6, 5, 5}, {3, 4, 2, 1, 6}, {7, 2, 2, 2, 2},
    {5, 5, 5, 5, 7}, {5, 5, 5, 5, 2}, {5, 5, 7, 7, 5}, {5, 5, 2, 5, 5}, {5, 5, 2, 2, 2},
    {7, 1, 2, 4, 7}};

static const unsigned char glyph_lt[5] = {1, 2, 4, 2, 1};
static const unsigned char glyph_gt[5] = {4, 2, 1, 2, 4};
static const unsigned char glyph_sp[5] = {0, 0, 0, 0, 0};

static void draw_glyph(int x, int y, const unsigned char *g, int s, unsigned int col)
{
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 3; c++)
            if (g[r] & (4 >> c))
                fill_rect(x + c * s, y + r * s, s, s, col);
}

static void draw_text(int x, int y, const char *str, int s, unsigned int col)
{
    for (; *str; str++)
    {
        char ch = *str;
        const unsigned char *g = glyph_sp;
        if (ch >= 'A' && ch <= 'Z') g = alpha[ch - 'A'];
        else if (ch >= '0' && ch <= '9') g = font[ch - '0'];
        else if (ch == '<') g = glyph_lt;
        else if (ch == '>') g = glyph_gt;
        draw_glyph(x, y, g, s, col);
        x += 4 * s;
    }
}

static int text_w(const char *str, int s)
{
    return (int)strlen(str) * 4 * s - s;
}

static int draw_number(int x, int y, int val, int s, unsigned int col)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", val < 0 ? 0 : val);
    draw_text(x, y, buf, s, col);
    return (int)strlen(buf) * 4 * s;
}

/* ---------- map helpers ---------- */
static int is_wall(float x, float y)
{
    int mx = (int)x, my = (int)y;
    if (x < 0 || y < 0 || mx >= MAP_W || my >= MAP_H) return 1;
    return map[my][mx] != '0';
}

static int blocked(float x, float y, float r)
{
    return is_wall(x - r, y - r) || is_wall(x + r, y - r) ||
           is_wall(x - r, y + r) || is_wall(x + r, y + r);
}

static void try_move(float *x, float *y, float dx, float dy, float r)
{
    if (!blocked(*x + dx, *y, r)) *x += dx;
    if (!blocked(*x, *y + dy, r)) *y += dy;
}

static int has_los(float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0;
    float d = sqrtf(dx * dx + dy * dy);
    int n = (int)(d / 0.2f) + 1;
    for (int i = 1; i < n; i++)
        if (is_wall(x0 + dx * i / n, y0 + dy * i / n)) return 0;
    return 1;
}

static void spawn_pos(int team, int avoid_player, float *ox, float *oy)
{
    for (int t = 0; t < 300; t++)
    {
        int x0 = 1, x1 = MAP_W - 2, y0 = 1, y1 = MAP_H - 2;
        if (cur_mode == MODE_TDM)
        {
            if (team == 0) { x1 = 9; y1 = 9; }
            else { x0 = 14; y0 = 14; }
        }
        int cx = x0 + rand() % (x1 - x0 + 1);
        int cy = y0 + rand() % (y1 - y0 + 1);
        if (map[cy][cx] != '0') continue;
        float fx = cx + 0.5f, fy = cy + 0.5f;
        if (avoid_player)
        {
            float dx = fx - pl.x, dy = fy - pl.y;
            if (dx * dx + dy * dy < 25.0f) continue;
        }
        *ox = fx;
        *oy = fy;
        return;
    }
    *ox = 12.5f;
    *oy = 12.5f;
}

/* ---------- rendering ---------- */
static void render_world(const Player *p)
{
    const Env *E = &envs[cur_env];
    float dir_x = cosf(p->ang), dir_y = sinf(p->ang);
    float plane_x = -dir_y * 0.66f, plane_y = dir_x * 0.66f;
    int horizon = SCR_H / 2;

    for (int y = 0; y < SCR_H; y++)
    {
        unsigned int c;
        if (y < horizon)
        {
            float t = (float)y / horizon;
            c = tc(lerp(E->sky0[0], E->sky1[0], t), lerp(E->sky0[1], E->sky1[1], t),
                   lerp(E->sky0[2], E->sky1[2], t), 1.0f);
        }
        else
        {
            float t = (float)(y - horizon) / (SCR_H - horizon);
            c = tc(lerp(E->fl0[0], E->fl1[0], t), lerp(E->fl0[1], E->fl1[1], t),
                   lerp(E->fl0[2], E->fl1[2], t), 1.0f);
        }
        unsigned int *row = &backbuf[y * BUF_W];
        for (int x = 0; x < SCR_W; x++) row[x] = c;
    }

    if (cur_weather == 2)
        for (int i = 0; i < 40; i++)
            backbuf[stars[i][1] * BUF_W + stars[i][0]] = rgb(230, 230, 255);

    for (int x = 0; x < SCR_W; x++)
    {
        float cam = 2.0f * x / SCR_W - 1.0f;
        float rx = dir_x + plane_x * cam, ry = dir_y + plane_y * cam;
        int mx = (int)p->x, my = (int)p->y;
        float ddx = (rx == 0) ? 1e30f : fabsf(1.0f / rx);
        float ddy = (ry == 0) ? 1e30f : fabsf(1.0f / ry);
        int stepx, stepy;
        float sdx, sdy;
        if (rx < 0) { stepx = -1; sdx = (p->x - mx) * ddx; }
        else        { stepx = 1;  sdx = (mx + 1.0f - p->x) * ddx; }
        if (ry < 0) { stepy = -1; sdy = (p->y - my) * ddy; }
        else        { stepy = 1;  sdy = (my + 1.0f - p->y) * ddy; }

        int side = 0;
        char t = '1';
        for (;;)
        {
            if (sdx < sdy) { sdx += ddx; mx += stepx; side = 0; }
            else           { sdy += ddy; my += stepy; side = 1; }
            if (mx < 0 || my < 0 || mx >= MAP_W || my >= MAP_H) { t = '1'; break; }
            if (map[my][mx] != '0') { t = map[my][mx]; break; }
        }
        float perp = (side == 0) ? (sdx - ddx) : (sdy - ddy);
        if (perp < 0.05f) perp = 0.05f;
        zbuf[x] = perp;

        int line_h = (int)(SCR_H / perp);
        int y0 = horizon - line_h / 2, y1 = horizon + line_h / 2;
        if (y0 < 0) y0 = 0;
        if (y1 >= SCR_H) y1 = SCR_H - 1;

        int ti = t - '1';
        if (ti < 0 || ti > 2) ti = 0;
        float f = 1.0f / (1.0f + perp * perp * fog_k);
        if (side == 1) f *= 0.75f;
        unsigned int col = tc(E->wall[ti][0], E->wall[ti][1], E->wall[ti][2], f);
        for (int y = y0; y <= y1; y++) backbuf[y * BUF_W + x] = col;
    }
}

static int project(const Player *p, float ex, float ey, float *tx, float *ty)
{
    float dir_x = cosf(p->ang), dir_y = sinf(p->ang);
    float plane_x = -dir_y * 0.66f, plane_y = dir_x * 0.66f;
    float rx = ex - p->x, ry = ey - p->y;
    float inv = 1.0f / (plane_x * dir_y - dir_x * plane_y);
    *tx = inv * (dir_y * rx - dir_x * ry);
    *ty = inv * (-plane_y * rx + plane_x * ry);
    return *ty > 0.1f;
}

static const int ffa_colors[8][3] = {{200, 60, 60}, {60, 160, 70}, {220, 170, 40}, {150, 70, 190},
                                      {230, 120, 40}, {40, 170, 170}, {200, 200, 200}, {170, 90, 90}};

static const int skin_tones[5][3] = {{235, 190, 160}, {210, 160, 120}, {160, 110, 80}, {110, 75, 55}, {240, 205, 180}};
static const int pants_cols[3][3] = {{45, 50, 70}, {70, 65, 50}, {40, 55, 45}};
static int anim_t = 0;

static unsigned int cc(const int *c, float f, float s, int white)
{
    if (white) return rgb(255, 255, 255);
    return tc(c[0], c[1], c[2], f * s);
}

static void seg(int x, int top, int h, float v0, float v1, unsigned int col)
{
    int y0 = top + (int)(v0 * h), y1 = top + (int)(v1 * h);
    if (y1 <= y0) y1 = y0 + 1;
    fill_rect(x, y0, 1, y1 - y0, col);
}

/* a humanoid soldier: helmet, face, neck, vest, belt, arms, rifle, legs, boots */
static void draw_bot(const Player *p, const Bot *b, int idx)
{
    float tx, ty;
    if (!project(p, b->x, b->y, &tx, &ty)) return;

    int h = (int)(SCR_H / ty);
    int w = h * 45 / 100;
    if (w < 3) w = 3;
    int cx = (int)((SCR_W / 2) * (1.0f + tx / ty));
    int left = cx - w / 2;
    int top = SCR_H / 2 - h / 2;
    float f = 1.0f / (1.0f + ty * ty * fog_k);

    int uni[3], helm[3];
    if (cur_mode == MODE_TDM)
    {
        if (b->team == 0) { uni[0] = 50; uni[1] = 90; uni[2] = 210; helm[0] = 30; helm[1] = 55; helm[2] = 140; }
        else { uni[0] = 210; uni[1] = 55; uni[2] = 50; helm[0] = 140; helm[1] = 30; helm[2] = 30; }
    }
    else
    {
        for (int k = 0; k < 3; k++)
        {
            uni[k] = ffa_colors[idx % 8][k];
            helm[k] = uni[k] * 6 / 10;
        }
    }
    const int *sk = skin_tones[idx % 5];
    const int *pt = pants_cols[idx % 3];
    const int boot[3] = {28, 26, 24};
    const int gun[3] = {35, 35, 40};
    const int belt[3] = {30, 28, 26};
    int white = b->flash > 0;

    /* walking animation */
    float phase = anim_t * 0.25f + idx * 1.7f;
    float raise_l = 0.0f, raise_r = 0.0f;
    if (b->moving)
    {
        float s = sinf(phase);
        raise_l = 0.05f * (0.5f + 0.5f * s);
        raise_r = 0.05f * (0.5f - 0.5f * s);
        top -= (int)(fabsf(s) * h * 0.012f);
    }

    for (int i = 0; i < w; i++)
    {
        int x = left + i;
        if (x < 0 || x >= SCR_W || zbuf[x] <= ty) continue;
        float u = (i + 0.5f) / w;
        float d = u - 0.5f;
        float ad = fabsf(d);
        float light = 1.0f - 0.35f * u; /* lit from the left */

        /* legs */
        for (int leg = 0; leg < 2; leg++)
        {
            float c = leg == 0 ? -0.13f : 0.13f;
            float dd = fabsf(d - c);
            if (dd < 0.11f)
            {
                float bottom = 1.0f - (leg == 0 ? raise_l : raise_r);
                float e = (dd > 0.09f) ? 0.6f : 1.0f;
                seg(x, top, h, 0.55f, 0.78f, cc(pt, f, light * e, white));
                seg(x, top, h, 0.78f, 0.90f, cc(pt, f, light * e * 0.85f, white));
                seg(x, top, h, 0.90f, bottom, cc(boot, f, light * e, white));
            }
        }

        /* torso / vest */
        if (ad < 0.30f)
        {
            float t0 = 0.17f + (ad > 0.2f ? (ad - 0.2f) * 0.4f : 0.0f);
            float t1 = (ad < 0.22f) ? 0.51f : 0.46f;
            float e = (ad > 0.28f) ? 0.65f : 1.0f;
            float shade = light * e;
            if (ad < 0.035f) shade *= 0.75f; /* zip / chest strap */
            seg(x, top, h, t0, t1, cc(uni, f, shade, white));
            if (ad < 0.22f)
                seg(x, top, h, 0.51f, 0.56f, cc(belt, f, light, white)); /* belt */
            if (ad > 0.06f && ad < 0.2f)
                seg(x, top, h, 0.40f, 0.46f, cc(uni, f, shade * 0.8f, white)); /* pouches */
        }

        /* neck */
        if (ad < 0.06f)
            seg(x, top, h, 0.14f, 0.18f, cc(sk, f, light * 0.8f, white));

        /* head: round, helmet on top, face below */
        if (ad < 0.17f)
        {
            float du = ad / 0.17f;
            float hh = 0.075f * sqrtf(1.0f - du * du);
            float y0 = 0.075f - hh, y1 = 0.075f + hh;
            float split = 0.058f;
            if (split > y1) split = y1;
            seg(x, top, h, y0, split, cc(helm, f, light, white));
            if (y1 > split)
                seg(x, top, h, split, y1, cc(sk, f, light, white));
            if (h > 40 && !white && ad > 0.045f && ad < 0.095f)
                seg(x, top, h, 0.078f, 0.088f, rgb(20, 20, 25)); /* eyes */
        }

        /* arms: sleeve, bare forearm, hand */
        if (ad >= 0.26f && ad < 0.45f)
        {
            float e = (ad > 0.43f) ? 0.65f : 1.0f;
            seg(x, top, h, 0.19f, 0.36f, cc(uni, f, light * e * 0.9f, white));
            seg(x, top, h, 0.36f, 0.50f, cc(sk, f, light * e, white));
            seg(x, top, h, 0.50f, 0.54f, cc(sk, f, light * e * 0.85f, white));
        }

        /* rifle held across the body */
        if (d > -0.05f && d < 0.44f)
        {
            seg(x, top, h, 0.395f, 0.445f, cc(gun, f, light, white));
            if (d > 0.30f)
                seg(x, top, h, 0.385f, 0.395f, cc(gun, f, light * 1.6f, white));
        }
    }
}

static void render_bots(const Player *p)
{
    int idx[MAX_BOTS];
    float dist[MAX_BOTS];
    int n = 0;
    for (int i = 0; i < nbots; i++)
    {
        if (!bots[i].alive) continue;
        float dx = bots[i].x - p->x, dy = bots[i].y - p->y;
        idx[n] = i;
        dist[n] = dx * dx + dy * dy;
        n++;
    }
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (dist[j] > dist[i])
            {
                float td = dist[i]; dist[i] = dist[j]; dist[j] = td;
                int ti = idx[i]; idx[i] = idx[j]; idx[j] = ti;
            }
    for (int i = 0; i < n; i++) draw_bot(p, &bots[idx[i]], idx[i]);
}

static void render_rain(void)
{
    if (cur_weather != 1) return;
    for (int i = 0; i < 100; i++)
    {
        drops[i].y += drops[i].s;
        drops[i].x -= 2;
        if (drops[i].y >= SCR_H) { drops[i].y = -8; drops[i].x = rand() % (SCR_W + 20); }
        if (drops[i].x < 0) drops[i].x += SCR_W;
        fill_rect(drops[i].x, drops[i].y, 1, 7, rgb(170, 190, 225));
    }
}

static void draw_gun(const Player *p)
{
    int off = p->recoil;
    if (p->reload > 0)
    {
        int r = p->reload > 40 ? 80 - p->reload : p->reload;
        off += r;
    }
    int cx = SCR_W / 2;
    fill_rect(cx - 5, SCR_H - 95 + off, 10, 55, rgb(40, 40, 45));
    fill_rect(cx - 20, SCR_H - 48 + off, 40, 60, rgb(70, 70, 78));
    fill_rect(cx - 16, SCR_H - 44 + off, 32, 6, rgb(110, 110, 120));
    if (p->flash > 0)
    {
        fill_rect(cx - 12, SCR_H - 115 + off, 24, 20, rgb(255, 230, 90));
        fill_rect(cx - 6, SCR_H - 125 + off, 12, 12, rgb(255, 255, 220));
    }
}

static void draw_hud(const Player *p)
{
    int cx = SCR_W / 2, cy = SCR_H / 2;
    unsigned int white = rgb(255, 255, 255);

    fill_rect(cx - 8, cy, 5, 1, white);
    fill_rect(cx + 4, cy, 5, 1, white);
    fill_rect(cx, cy - 8, 1, 5, white);
    fill_rect(cx, cy + 4, 1, 5, white);
    if (p->hitmarker > 0)
    {
        unsigned int red = rgb(255, 40, 40);
        for (int i = 4; i < 10; i++)
        {
            fill_rect(cx - i, cy - i, 2, 2, red);
            fill_rect(cx + i, cy - i, 2, 2, red);
            fill_rect(cx - i, cy + i, 2, 2, red);
            fill_rect(cx + i, cy + i, 2, 2, red);
        }
    }

    int hp = (int)p->hp;
    if (hp < 0) hp = 0;
    fill_rect(10, SCR_H - 24, 104, 10, rgb(20, 20, 20));
    fill_rect(12, SCR_H - 22, hp, 6, rgb(255 - hp * 2, (int)(hp * 2.2f), 40));
    fill_rect(10, SCR_H - 12, 104, 6, rgb(20, 20, 20));
    fill_rect(12, SCR_H - 11, (int)p->stamina, 3, rgb(70, 140, 255));
    draw_number(10, SCR_H - 42, hp, 2, white);

    int w = draw_number(SCR_W - 110, SCR_H - 42, p->mag, 4, rgb(255, 220, 80));
    draw_number(SCR_W - 110 + w + 6, SCR_H - 34, p->reserve, 2, rgb(200, 200, 200));
    if (p->reload > 0)
        fill_rect(cx - 20, cy + 16, (80 - p->reload) * 40 / 80, 3, rgb(255, 220, 80));

    /* scoreboard, top centre */
    if (cur_mode == MODE_TDM)
    {
        draw_text(SCR_W / 2 - 90, 8, "BLU", 2, rgb(90, 140, 255));
        draw_number(SCR_W / 2 - 60, 6, team_score[0], 3, white);
        draw_text(SCR_W / 2 + 10, 8, "RED", 2, rgb(255, 90, 80));
        draw_number(SCR_W / 2 + 40, 6, team_score[1], 3, white);
    }
    else
    {
        int alive = 0;
        for (int i = 0; i < nbots; i++) alive += bots[i].alive;
        draw_text(SCR_W / 2 - 40, 8, "ALIVE", 2, rgb(255, 220, 80));
        draw_number(SCR_W / 2 + 6, 6, alive + 1, 3, white);
    }

    draw_text(10, 8, "KILLS", 2, rgb(90, 230, 90));
    draw_number(62, 6, p->kills, 3, white);

    if (p->dmg_flash > 0)
    {
        unsigned int r = rgb(180, 0, 0);
        fill_rect(0, 0, SCR_W, 6, r);
        fill_rect(0, SCR_H - 6, SCR_W, 6, r);
        fill_rect(0, 0, 6, SCR_H, r);
        fill_rect(SCR_W - 6, 0, 6, SCR_H, r);
    }
}

static void draw_center_text(int y, const char *s, int sc, unsigned int col)
{
    draw_text((SCR_W - text_w(s, sc)) / 2, y, s, sc, col);
}

static void draw_dead_overlay(const Player *p)
{
    unsigned int dark = rgb(70, 0, 0);
    for (int y = 0; y < SCR_H; y += 2) fill_rect(0, y, SCR_W, 1, dark);
    draw_center_text(90, "YOU DIED", 5, rgb(255, 255, 255));
    draw_center_text(150, "RESPAWN", 2, rgb(255, 200, 200));
    draw_number(SCR_W / 2 - 6, 175, p->dead_timer / 60 + 1, 4, rgb(255, 255, 255));
}

static void draw_over(const Player *p)
{
    unsigned int dark = rgb(15, 15, 25);
    for (int y = 0; y < SCR_H; y += 2) fill_rect(0, y, SCR_W, 1, dark);
    if (result == 1) draw_center_text(60, "VICTORY", 6, rgb(90, 240, 110));
    else if (result == 2) draw_center_text(60, "ELIMINATED", 5, rgb(255, 90, 80));
    else draw_center_text(60, "DEFEAT", 6, rgb(255, 90, 80));
    draw_text(180, 130, "KILLS", 3, rgb(200, 200, 200));
    draw_number(300, 130, p->kills, 3, rgb(255, 255, 255));
    draw_center_text(215, "PRESS START", 3, rgb(255, 220, 80));
}

/* ---------- setup menu ---------- */
static void draw_preview(int x, int y, int w, int h, int env, int weather)
{
    set_weather(weather);
    const Env *E = &envs[env];
    int half = h / 2;
    for (int j = 0; j < h; j++)
    {
        unsigned int c;
        if (j < half)
        {
            float t = (float)j / half;
            c = tc(lerp(E->sky0[0], E->sky1[0], t), lerp(E->sky0[1], E->sky1[1], t),
                   lerp(E->sky0[2], E->sky1[2], t), 1.0f);
        }
        else
        {
            float t = (float)(j - half) / (h - half);
            c = tc(lerp(E->fl0[0], E->fl1[0], t), lerp(E->fl0[1], E->fl1[1], t),
                   lerp(E->fl0[2], E->fl1[2], t), 1.0f);
        }
        fill_rect(x, y + j, w, 1, c);
    }
    fill_rect(x + 12, y + half - 16, 30, 16, tc(E->wall[0][0], E->wall[0][1], E->wall[0][2], 0.9f));
    fill_rect(x + w - 46, y + half - 26, 34, 26, tc(E->wall[1][0], E->wall[1][1], E->wall[1][2], 0.8f));
    fill_rect(x + w / 2 - 8, y + half - 10, 16, 10, tc(E->wall[2][0], E->wall[2][1], E->wall[2][2], 0.7f));
    if (weather == 1)
        for (int i = 0; i < 20; i++) fill_rect(x + (i * 37) % w, y + (i * 23) % (h - 8), 1, 6, rgb(170, 190, 225));
    if (weather == 2)
        for (int i = 0; i < 12; i++) fill_rect(x + (i * 53) % w, y + (i * 11) % (half - 2), 1, 1, rgb(230, 230, 255));
    /* frame */
    fill_rect(x - 2, y - 2, w + 4, 2, rgb(200, 200, 210));
    fill_rect(x - 2, y + h, w + 4, 2, rgb(200, 200, 210));
    fill_rect(x - 2, y, 2, h, rgb(200, 200, 210));
    fill_rect(x + w, y, 2, h, rgb(200, 200, 210));
}

static void draw_menu(void)
{
    fill_rect(0, 0, SCR_W, SCR_H, rgb(15, 18, 28));
    draw_text(20, 14, "MINI BR", 6, rgb(255, 255, 255));
    draw_text(20, 58, "PSP BATTLE ARENA", 2, rgb(150, 160, 190));

    const char *labels[3] = {"MODE", "MAP", "WEATHER"};
    const char *values[3] = {mode_names[cfg_mode], envs[cfg_map].name, weather_names[cfg_weather]};
    for (int i = 0; i < 3; i++)
    {
        int y = 100 + i * 36;
        if (i == menu_row) fill_rect(10, y - 6, 322, 30, rgb(40, 50, 85));
        draw_text(20, y + 2, labels[i], 2, rgb(160, 170, 200));
        char buf[40];
        snprintf(buf, sizeof(buf), "< %s >", values[i]);
        draw_text(100, y, buf, 3, i == menu_row ? rgb(255, 255, 255) : rgb(190, 190, 200));
    }

    draw_preview(348, 104, 116, 80, cfg_map, cfg_weather);
    draw_center_text(222, "PRESS CROSS TO DEPLOY", 2, rgb(255, 220, 80));
    draw_center_text(246, "UP DOWN SELECT  LEFT RIGHT CHANGE", 2, rgb(120, 130, 160));
}

/* ---------- game logic ---------- */
static void respawn_bot(Bot *b)
{
    spawn_pos(b->team, 1, &b->x, &b->y);
    b->hp = 6;
    b->alive = 1;
    b->flash = 0;
    b->cool = 30;
    b->gt = 0;
}

static void start_match(void)
{
    cur_mode = cfg_mode;
    cur_env = cfg_map;
    cur_weather = cfg_weather;
    set_weather(cur_weather);
    build_map();
    team_score[0] = team_score[1] = 0;

    memset(&pl, 0, sizeof(pl));
    spawn_pos(0, 0, &pl.x, &pl.y);
    pl.ang = 0.8f;
    pl.hp = 100.0f;
    pl.stamina = 100.0f;
    pl.mag = MAG_SIZE;
    pl.reserve = 48;

    nbots = 11;
    for (int i = 0; i < nbots; i++)
    {
        memset(&bots[i], 0, sizeof(Bot));
        if (cur_mode == MODE_TDM) bots[i].team = (i < 5) ? 0 : 1;
        else bots[i].team = i + 1;
        respawn_bot(&bots[i]);
        bots[i].ang = (float)(rand() % 628) / 100.0f;
    }

    for (int i = 0; i < 100; i++)
    {
        drops[i].x = rand() % SCR_W;
        drops[i].y = rand() % SCR_H;
        drops[i].s = 8 + rand() % 6;
    }
    for (int i = 0; i < 40; i++)
    {
        stars[i][0] = rand() % SCR_W;
        stars[i][1] = rand() % (SCR_H / 2 - 4);
    }
    state = ST_PLAY;
}

static void bot_killed(int i, int killer_team, int by_player)
{
    Bot *b = &bots[i];
    b->alive = 0;
    b->respawn = (cur_mode == MODE_TDM) ? 180 : 0;
    if (cur_mode == MODE_TDM && killer_team >= 0 && killer_team < 2) team_score[killer_team]++;
    if (by_player) pl.kills++;
}

static void hurt_player(float d, int team)
{
    pl.hp -= d;
    pl.dmg_flash = 10;
    pl.regen_delay = 0;
    last_hit_team = team;
}

static void fire(Player *p)
{
    p->mag--;
    p->cooldown = 7;
    p->recoil = 6;
    p->flash = 3;

    int best = -1;
    float best_ty = 1e9f;
    int dmg = 2;
    for (int i = 0; i < nbots; i++)
    {
        Bot *b = &bots[i];
        if (!b->alive || b->team == 0) continue; /* team 0 = player's team */
        float tx, ty;
        if (!project(p, b->x, b->y, &tx, &ty)) continue;
        if (fabsf(tx) < 0.28f && ty < best_ty && ty < zbuf[SCR_W / 2])
        {
            best = i;
            best_ty = ty;
            dmg = (fabsf(tx) < 0.1f) ? 3 : 2; /* crit */
        }
    }
    if (best >= 0)
    {
        bots[best].hp -= dmg;
        bots[best].flash = 5;
        p->hitmarker = 8;
        if (bots[best].hp <= 0)
        {
            bot_killed(best, 0, 1);
            p->reserve += 4;
            if (p->reserve > 99) p->reserve = 99;
        }
    }
}

static void update_bots(void)
{
    for (int i = 0; i < nbots; i++)
    {
        Bot *b = &bots[i];
        if (!b->alive)
        {
            if (b->respawn > 0 && --b->respawn == 0) respawn_bot(b);
            continue;
        }
        if (b->flash > 0) b->flash--;
        if (b->cool > 0) b->cool--;

        /* look for the nearest visible enemy */
        int tgt = -2;
        float best = 144.0f, tx = 0, ty = 0;
        if (!pl.dead && b->team != 0)
        {
            float dx = pl.x - b->x, dy = pl.y - b->y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best && has_los(b->x, b->y, pl.x, pl.y))
            {
                best = d2; tgt = -1; tx = pl.x; ty = pl.y;
            }
        }
        for (int j = 0; j < nbots; j++)
        {
            if (j == i || !bots[j].alive || bots[j].team == b->team) continue;
            float dx = bots[j].x - b->x, dy = bots[j].y - b->y;
            float d2 = dx * dx + dy * dy;
            if (d2 < best && has_los(b->x, b->y, bots[j].x, bots[j].y))
            {
                best = d2; tgt = j; tx = bots[j].x; ty = bots[j].y;
            }
        }

        float ox = b->x, oy = b->y;
        if (tgt != -2)
        {
            float d = sqrtf(best);
            b->ang = atan2f(ty - b->y, tx - b->x);
            float fwd = 0.0f;
            if (d > 5.0f) fwd = 0.035f;
            else if (d < 2.5f) fwd = -0.03f;
            float side = ((i + (int)(b->cool / 20)) & 1) ? 0.018f : -0.018f;
            try_move(&b->x, &b->y,
                     cosf(b->ang) * fwd - sinf(b->ang) * side,
                     sinf(b->ang) * fwd + cosf(b->ang) * side, 0.2f);

            if (b->cool == 0 && d < 11.0f)
            {
                float chance = 0.5f - d * 0.03f;
                if (chance < 0.1f) chance = 0.1f;
                if ((rand() % 100) < (int)(chance * 100.0f))
                {
                    if (tgt == -1) hurt_player(5.0f, b->team);
                    else
                    {
                        bots[tgt].hp -= 2;
                        bots[tgt].flash = 5;
                        if (bots[tgt].hp <= 0) bot_killed(tgt, b->team, 0);
                    }
                }
                b->cool = 25 + rand() % 25;
            }
        }
        else
        {
            float gx = b->gx - b->x, gy = b->gy - b->y;
            if (--b->gt <= 0 || gx * gx + gy * gy < 0.36f)
            {
                spawn_pos(b->team, 0, &b->gx, &b->gy);
                if (cur_mode == MODE_TDM)
                {
                    /* push toward the middle so teams meet */
                    b->gx = (b->gx + 12.0f) * 0.5f;
                    b->gy = (b->gy + 12.0f) * 0.5f;
                }
                b->gt = 300;
                gx = b->gx - b->x;
                gy = b->gy - b->y;
            }
            b->ang = atan2f(gy, gx);
            try_move(&b->x, &b->y, cosf(b->ang) * 0.028f, sinf(b->ang) * 0.028f, 0.2f);
        }
        float mx = b->x - ox, my = b->y - oy;
        b->moving = (mx * mx + my * my > 0.00002f);
        if (mx * mx + my * my < 0.000009f && tgt == -2) b->gt = 0; /* stuck: new goal */
    }
}

static void respawn_player(void)
{
    spawn_pos(0, 0, &pl.x, &pl.y);
    pl.hp = 100.0f;
    pl.stamina = 100.0f;
    pl.mag = MAG_SIZE;
    pl.reserve = 48;
    pl.dead = 0;
    pl.reload = 0;
}

static void update_game(const SceCtrlData *pad, unsigned int pressed)
{
    Player *p = &pl;

    if (p->dead)
    {
        if (--p->dead_timer <= 0) respawn_player();
        update_bots();
    }
    else
    {
        float turn = 0.045f;
        if (pad->Buttons & (PSP_CTRL_LEFT | PSP_CTRL_SQUARE)) p->ang -= turn;
        if (pad->Buttons & (PSP_CTRL_RIGHT | PSP_CTRL_CIRCLE)) p->ang += turn;

        float fwd = (128.0f - pad->Ly) / 128.0f;
        float str = (pad->Lx - 128.0f) / 128.0f;
        if (fabsf(fwd) < 0.2f) fwd = 0;
        if (fabsf(str) < 0.2f) str = 0;
        float speed = 0.05f;
        if ((pad->Buttons & PSP_CTRL_LTRIGGER) && p->stamina > 1.0f && (fwd != 0 || str != 0))
        {
            speed *= 1.6f;
            p->stamina -= 0.8f;
        }
        else if (p->stamina < 100.0f)
        {
            p->stamina += 0.4f;
        }
        float dx = (cosf(p->ang) * fwd - sinf(p->ang) * str) * speed;
        float dy = (sinf(p->ang) * fwd + cosf(p->ang) * str) * speed;
        try_move(&p->x, &p->y, dx, dy, 0.2f);

        if (p->reload > 0)
        {
            p->reload--;
            if (p->reload == 0)
            {
                int need = MAG_SIZE - p->mag;
                int take = need < p->reserve ? need : p->reserve;
                p->mag += take;
                p->reserve -= take;
            }
        }
        else if ((pressed & PSP_CTRL_CROSS) && p->mag < MAG_SIZE && p->reserve > 0)
        {
            p->reload = 80;
        }

        if (p->cooldown > 0) p->cooldown--;
        if ((pad->Buttons & PSP_CTRL_RTRIGGER) && p->cooldown == 0 && p->reload == 0)
        {
            if (p->mag > 0) fire(p);
            else if (p->reserve > 0) p->reload = 80;
        }
        if (p->recoil > 0) p->recoil--;
        if (p->flash > 0) p->flash--;
        if (p->hitmarker > 0) p->hitmarker--;
        if (p->dmg_flash > 0) p->dmg_flash--;

        update_bots();

        p->regen_delay++;
        if (p->regen_delay > 240 && p->hp < 100.0f) p->hp += 0.15f;

        if (p->hp <= 0.0f)
        {
            p->hp = 0.0f;
            if (cur_mode == MODE_TDM)
            {
                p->dead = 1;
                p->dead_timer = 180;
                if (last_hit_team >= 0 && last_hit_team < 2) team_score[last_hit_team]++;
            }
            else
            {
                result = 2;
                state = ST_OVER;
                return;
            }
        }
    }

    /* win / lose checks */
    if (cur_mode == MODE_TDM)
    {
        if (team_score[0] >= TDM_TARGET) { result = 1; state = ST_OVER; }
        else if (team_score[1] >= TDM_TARGET) { result = 0; state = ST_OVER; }
    }
    else
    {
        int alive = 0;
        for (int i = 0; i < nbots; i++) alive += bots[i].alive;
        if (alive == 0) { result = 1; state = ST_OVER; }
    }
}

static void update_menu(unsigned int pressed)
{
    if (pressed & PSP_CTRL_UP) menu_row = (menu_row + 2) % 3;
    if (pressed & PSP_CTRL_DOWN) menu_row = (menu_row + 1) % 3;
    int dir = 0;
    if (pressed & PSP_CTRL_LEFT) dir = -1;
    if (pressed & PSP_CTRL_RIGHT) dir = 1;
    if (dir != 0)
    {
        if (menu_row == 0) cfg_mode ^= 1;
        else if (menu_row == 1) cfg_map = (cfg_map + dir + NUM_ENVS) % NUM_ENVS;
        else cfg_weather = (cfg_weather + dir + 3) % 3;
    }
    if (pressed & PSP_CTRL_CROSS) start_match();
}

/* ---------- main ---------- */
int main(void)
{
    setup_callbacks();

    void *vram = (void *)(0x40000000 | (unsigned int)sceGeEdramGetAddr());
    sceDisplaySetMode(0, SCR_W, SCR_H);
    sceDisplaySetFrameBuf(vram, BUF_W, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    srand(sceKernelGetSystemTimeLow());
    build_map();

    SceCtrlData pad;
    unsigned int prev = 0;

    while (running)
    {
        sceCtrlReadBufferPositive(&pad, 1);
        anim_t++;
        unsigned int pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;

        if (state == ST_MENU)
        {
            update_menu(pressed);
            if (state == ST_MENU) draw_menu();
        }
        if (state == ST_PLAY)
        {
            update_game(&pad, pressed);
            if (pressed & PSP_CTRL_START) state = ST_MENU;
        }
        if (state == ST_PLAY || state == ST_OVER)
        {
            render_world(&pl);
            render_bots(&pl);
            render_rain();
            if (!pl.dead) draw_gun(&pl);
            draw_hud(&pl);
            if (state == ST_PLAY && pl.dead) draw_dead_overlay(&pl);
            if (state == ST_OVER)
            {
                draw_over(&pl);
                if (pressed & PSP_CTRL_START) state = ST_MENU;
            }
        }

        sceDisplayWaitVblankStart();
        memcpy(vram, backbuf, sizeof(backbuf));
    }

    sceKernelExitGame();
    return 0;
}
