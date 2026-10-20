/* "dddd" - harsh noise, paraphonic (docs/53). DSP in dddd/dd_core.c: one shared wall for all keys.
 *   blue  : source  noise -> crackle -> chaos -> metal         page 2: filter  LP -> BP -> HP
 *   green : feedback (past ~80 % it screams)                   page 2: loop spread (the hands throw the taps)
 *   white : destroy  fold -> clip -> crush                     page 2: rate crush 1 .. 1/32
 *   red   : hands    random movement; stutter at the top
 * Paraphonic: the lowest sounding voice (the leader) renders the wall, the others silence; every sounding key adds a
 * loop tap (the newest four). A voice counts as sounding while it renders (a finished release is not rendered).
 * Copyright (C) 2026 pcppcp. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "op1_plugin.h"
#include "../../dddd/dd_core.h"

static dd_t dd;
static uint32_t calls, last_call[OP1_VOICES], order[OP1_VOICES], order_n;   /* render calls, note order */
static uint32_t voice_inc[OP1_VOICES], taps_inc[DD_TAPS];
static int taps_n;
static uint8_t inited, alive_seen[OP1_VOICES];
/* the screen's view of the wall (written by the leader's render, read by the GUI) */
static volatile int16_t scope[32];
static volatile int32_t level;

static int alive(int v) { return alive_seen[v] && calls - last_call[v] <= OP1_VOICES + 1; }

static void dd_note_on(void *self, const int16_t *params)
{
    int v = op1_voice_index(self), any = 0;
    (void)params;
    if (!inited) { dd_init(&dd, 0x5EEDu); inited = 1; }
    for (int u = 0; u < OP1_VOICES; u++) if (u != v && alive(u)) any = 1;
    if (!any) dd_reset_loop(&dd);                              /* nothing sounding: no stale howl */
    order[v] = ++order_n;
    last_call[v] = calls; alive_seen[v] = 1;
}

static void dd_render_voice(void *self, int16_t *out, const int16_t *params)
{
    int v = op1_voice_index(self);
    calls++;
    last_call[v] = calls; alive_seen[v] = 1;
    voice_inc[v] = (uint32_t)OP1_PITCH_INC[0];
    for (int u = 0; u < v; u++)
        if (alive(u)) { for (int i = 0; i < OP1_BLOCK; i++) out[i] = 0; return; }   /* a lower voice leads */
    if (!inited) { dd_init(&dd, 0x5EEDu); inited = 1; }
    /* the sounding keys, oldest first (the newest four become taps) */
    int idx[OP1_VOICES], n = 0;
    for (int u = 0; u < OP1_VOICES; u++) if (alive(u)) idx[n++] = u;
    for (int i = 1; i < n; i++) for (int j = i; j > 0 && order[idx[j]] < order[idx[j - 1]]; j--) { int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t; }
    uint32_t inc[OP1_VOICES];
    for (int i = 0; i < n; i++) inc[i] = voice_inc[idx[i]];
    int k0 = n > DD_TAPS ? n - DD_TAPS : 0, same = n - k0 == taps_n;
    for (int i = k0; i < n && same; i++) same = taps_inc[i - k0] == inc[i];
    if (!same) {                                               /* new keys: new taps (divisions only here) */
        dd_set_incs(&dd, inc + k0, n - k0);
        taps_n = n - k0;
        for (int i = k0; i < n; i++) taps_inc[i - k0] = inc[i];
    }
    dd_par p = { params[0], params[1], params[2], params[3],
                 params[OP1_ENG_PAGE_WORD(0)], params[OP1_ENG_PAGE_WORD(1)], params[OP1_ENG_PAGE_WORD(2)],
                 op1_voice_vel_gain(self), OP1_VOICE_ENV[OP1_BLOCK - 1] };
    dd_render(&dd, &p, out, OP1_BLOCK);
    int32_t pk = 0;
    for (int i = 0; i < OP1_BLOCK; i++) { int32_t a = out[i] < 0 ? -out[i] : out[i]; if (a > pk) pk = a; }
    for (int i = 0; i < 32; i++) scope[i] = out[i * 4];
    level = pk;
}

const op1_engine_t ENGINE_dddd = { dd_note_on, dd_render_voice, 0,
                                   { OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_PAGE, OP1_PRESS_NONE } };

/* ======================= screen (GUI thread) =======================
 * dddd.svg is empty. Drawn here: the wall (a column of short vertical strokes per 10 px, height from the level and
 * noise, more strokes as destroy rises; grey, the loudest white), torn in red for a few frames when the hands jump,
 * a jagged blue trace of the output (source), its green echo shifted by the feedback; the bottom row in the stock font. */
#include "op1_synth.h"
#define Q(v) ((v) << 10)
#define TS 12
#define WX0 10
#define WX1 310
#define WYC 56
#define WH  38
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF },
                     c_grey[4] = { 0x6F, 0x71, 0x73, 0xFF }, c_dimg[4] = { 0x00, 0x5F, 0x3B, 0xFF };
static const uint8_t *const col4[4] = { c_blue, c_green, c_white, c_red };
enum { TX_LAB, TX_VAL = 4, TX_N = 8 };
static uint32_t tx[TX_N][OP1_TEXT_SIZE / 4];
static char shown[TX_N][14];
static uint8_t tx_made;
static uint32_t gui_rng = 1, seen_jumps;
static int tear;

static void ln(void *r, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, const uint8_t *c)
{
    if (x0 == x1 && y0 == y1) return;
    op1_pt a = { x0, y0 }, b = { x1, y1 };
    op1_raster_line(r, &a, &b, w, c);
}
static void txt(void *r, int i, const char *s)
{
    static const int32_t ident[9] = { 1024, 0, 0, 0, 1024, 0, 0, 0, 1024 };
    int same = 1;
    for (int j = 0; j < 14; j++) { if (shown[i][j] != s[j]) { same = 0; break; } if (!s[j]) break; }
    if (!same) { op1_text_set(tx[i], s); int j = 0; for (; j < 13 && s[j]; j++) shown[i][j] = s[j]; shown[i][j] = 0; }
    op1_text_draw(tx[i], r, ident);
}
static uint32_t grnd(void) { gui_rng ^= gui_rng << 13; gui_rng ^= gui_rng >> 17; gui_rng ^= gui_rng << 5; return gui_rng; }
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static void pct(char *p, int32_t v) { int x = (v * 100 + 16383) >> 15; char t[4]; int n = 0; do t[n++] = (char)('0' + x % 10); while ((x /= 10)); while (n) *p++ = t[--n]; put(p, "%"); }

static void dd_screen_init(void *scr) { (void)scr; }

static void dd_screen_update(void *scr, int dt)
{
    int slot = OP1_SCREEN_SLOT & 7;
    int16_t k[4];
    op1_synth_knobs_live(slot, k);
    const int16_t *w = op1_synth_knobs(slot);
    if (!tx_made) {
        for (int i = 0; i < 4; i++) {
            op1_text_ctor(tx[TX_LAB + i], Q(8 + 78 * i), 114, TS, col4[i]);
            op1_text_ctor(tx[TX_VAL + i], Q(8 + 78 * i), 127, TS, c_white);
        }
        tx_made = 1;
    }
    op1_screen_draw(scr, dt);
    void *r = op1_raster();

    /* the wall */
    int32_t lv = level;                                         /* 0 .. 16384 */
    int strokes = 1 + (k[2] * 3 >> 15);                         /* destroy: 1 .. 3 strokes a column */
    if (dd.jumps != seen_jumps) { seen_jumps = dd.jumps; tear = 3; }
    for (int x = WX0; x <= WX1 && lv > 64; x += 10)            /* silence: no wall */
        for (int s = 0; s < strokes; s++) {
            uint32_t q = grnd();
            int32_t h = 2 + (int32_t)(((q & 255) * (uint32_t)(lv * WH * 2 / 16384)) >> 8);   /* up to the band */
            int32_t yc = WYC + (int32_t)((q >> 8) & 31) - 16, xx = x + s * 2;
            const uint8_t *c = tear && (q >> 20 & 3) == 0 ? c_red : (q >> 24 & 3) == 0 ? c_white : c_grey;
            ln(r, Q(xx), Q(yc - h / 2), Q(xx), Q(yc + h / 2), 0x400, c);
        }
    if (tear) tear--;
    /* the output trace (source) and its echo (feedback) */
    int32_t echo = (k[1] * 40) >> 15;                           /* px: the echo moves out as feedback rises */
    for (int i = 0; i < 24; i++) {                              /* 24 segments of the block's 32 points */
        int32_t x0 = Q(WX0) + i * Q(WX1 - WX0) / 24, x1 = Q(WX0) + (i + 1) * Q(WX1 - WX0) / 24;
        int32_t y0 = Q(WYC) - scope[i * 31 / 24] * Q(WH) / 16384, y1 = Q(WYC) - scope[(i + 1) * 31 / 24] * Q(WH) / 16384;
        if (echo > 1 && x1 + Q(echo) <= Q(WX1)) ln(r, x0 + Q(echo), y0 + Q(4), x1 + Q(echo), y1 + Q(4), 0x400, c_dimg);
        ln(r, x0, y0, x1, y1, 0x600, c_blue);
    }

    /* the bottom row */
    for (int i = 0; i < 4; i++) ln(r, Q(8 + 78 * i), Q(110), Q(78 + 78 * i), Q(110), 0x800, col4[i]);
    static const char *const srcs[4] = { "noise", "crackle", "chaos", "metal" };
    char v[16];
    if (op1_eng_page[0]) {
        int32_t f = w[OP1_ENG_PAGE_WORD(0)] < 0 ? 0 : w[OP1_ENG_PAGE_WORD(0)];
        txt(r, TX_LAB, "filter"); txt(r, TX_VAL, f < 10923 ? "lp" : f < 21845 ? "bp" : "hp");
    } else { txt(r, TX_LAB, "source"); txt(r, TX_VAL, srcs[(k[0] * 4 >> 15) > 3 ? 3 : k[0] * 4 >> 15]); }
    if (op1_eng_page[1]) { txt(r, TX_LAB + 1, "spread"); pct(v, w[OP1_ENG_PAGE_WORD(1)] < 0 ? 0 : w[OP1_ENG_PAGE_WORD(1)]); }
    else { txt(r, TX_LAB + 1, "feedback"); pct(v, k[1]); }
    txt(r, TX_VAL + 1, v);
    if (op1_eng_page[2]) {
        int32_t c = w[OP1_ENG_PAGE_WORD(2)] < 0 ? 0 : w[OP1_ENG_PAGE_WORD(2)];
        int n = 1 + ((c * 31) >> 15);
        char *p = put(v, "1/"); char t[4]; int m = 0; do t[m++] = (char)('0' + n % 10); while ((n /= 10)); while (m) *p++ = t[--m]; *p = 0;
        txt(r, TX_LAB + 2, "rate");
    } else { txt(r, TX_LAB + 2, "destroy"); pct(v, k[2]); }
    txt(r, TX_VAL + 2, v);
    txt(r, TX_LAB + 3, "hands"); pct(v, k[3]); txt(r, TX_VAL + 3, v);
}

const op1_screen_t SCREEN_dddd = { dd_screen_init, dd_screen_update };
