/* Host test of plugins/dddd (the engine wrapper): the paraphonic leader (lowest key renders the wall, the others stay
 * silent), one loop tap per sounding key, and the hand-over when the lowest key is released while others hold.
 * The core itself: dddd_test.c.
 *   cc -O1 -Wall -DOP1_HOST -fsanitize=address,undefined -I.. dddd_plugin_test.c ../dddd/dd_core.c -lm -o build/dddd_plugin_test
 * Copyright (C) 2026 Hundekraft. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "../op1_plugin.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24]; int16_t host_voice_vel[6]; int16_t host_voice_env[128];
volatile uint8_t op1_eng_page[4];
int16_t host_knobs[8][8]; int host_slot;                                 /* screen stubs (screen not drawn here) */
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }
void *host_raster(void) { return 0; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c) { (void)r; (void)a; (void)b; (void)w; (void)c; }
void *host_text_ctor(void *o, int32_t x, int32_t y, int32_t z, const uint8_t *c) { (void)x; (void)y; (void)z; (void)c; return o; }
void host_text_set(void *t, const char *s) { (void)t; (void)s; }
void host_text_draw(void *t, void *r, const int32_t *m) { (void)t; (void)r; (void)m; }
static int dbg_resets;
#define op1_dbg(text, a, b) (dbg_resets += (text)[5] == 'r')    /* "dddd reset: voice" */
#include "../plugins/dddd/dddd.c"

static int16_t knobs[8] = { 0, 24000, 20000, 8000, 0x5300, 0, 0, 0 };   /* defaults: noise, feedback .73, destroy .61 */
static int32_t inc_of(int midi) { return (int32_t)lrint(4294967296.0 * 440.0 * pow(2, (midi - 69) / 12.0) / 44100); }
static void *voice(int i) { return host_voice_mem + i * 0x24; }
static int16_t out[OP1_VOICES][OP1_BLOCK];
static int32_t pk[OP1_VOICES];

/* one audio block as the firmware runs it: every sounding voice in index order; host_pitch_inc[0] is the current
 * voice's increment, so it is set per call */
static void block(const int *sounding, const int *midi, int n)
{
    for (int i = 0; i < n; i++) {
        int v = sounding[i];
        host_pitch_inc[0] = inc_of(midi[i]);
        ENGINE_dddd.render(voice(v), out[v], knobs);
        pk[v] = 0;
        for (int j = 0; j < OP1_BLOCK; j++) { int32_t a = abs(out[v][j]); if (a > pk[v]) pk[v] = a; }
    }
}
static int near(int a, int b) { return abs(a - b) <= 1; }

int main(void)
{
    for (int i = 0; i < 6; i++) host_voice_vel[i] = 127 << 8;
    for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)lrint(32767.0 * sin(2 * M_PI * i / 8192.0));
    for (int i = 0; i < OP1_BLOCK; i++) host_voice_env[i] = 32767;

    /* one key on voice 3: it leads, one tap at the key's period (C4 = 168 samples) */
    host_pitch_inc[0] = inc_of(60);
    ENGINE_dddd.note_on(voice(3), knobs);
    assert(dbg_resets == 1);                                    /* nothing sounding: the loop is cleared */
    int s1[1] = { 3 }, m1[1] = { 60 };
    for (int b = 0; b < 40; b++) block(s1, m1, 1);
    assert(dd.ntaps == 1 && near(dd.tap[0], 168));
    assert(pk[3] > 1000 && pk[3] <= 16384);

    /* a second, lower-index voice joins (as a chord is played): it takes the lead, voice 3 goes silent */
    ENGINE_dddd.note_on(voice(1), knobs);
    assert(dbg_resets == 1);                                    /* a key is sounding: no reset, the wall goes on */
    int s2[2] = { 1, 3 }, m2[2] = { 64, 60 };
    for (int b = 0; b < 40; b++) block(s2, m2, 2);
    assert(pk[1] > 1000 && pk[3] == 0);
    assert(dd.ntaps == 2);
    assert(near(dd.tap[0], 168) && near(dd.tap[1], 133));       /* note order: C4 first (oldest), E4 newest */
    assert(near((int)(dd.inc >> 8), (int)(inc_of(64) >> 8)));    /* oscillators follow the newest key */

    /* the leader is released: once it stops being rendered, voice 3 leads again and keeps the wall */
    int s3[1] = { 3 }, m3[1] = { 60 };
    for (int b = 0; b < 40; b++) block(s3, m3, 1);
    assert(pk[3] > 1000 && dd.ntaps == 1 && near(dd.tap[0], 168));

    /* the lowest key is released while two higher keys hold. Its voice still renders (the release), but the
     * lowest held voice takes the lead at once: no gap while the release fades out */
    const int sr[3] = { 0, 1, 2 }, mr[3] = { 60, 64, 67 };
    for (int i = 0; i < 3; i++) { host_pitch_inc[0] = inc_of(mr[i]); ENGINE_dddd.note_on(voice(sr[i]), knobs); }
    for (int b = 0; b < 20; b++) block(sr, mr, 3);
    assert(pk[0] > 1000 && pk[1] == 0 && pk[2] == 0);
    ENGINE_dddd.note_off(voice(0));
    for (int b = 0; b < 20; b++) block(sr, mr, 3);
    assert(pk[0] == 0 && pk[1] > 1000 && pk[2] == 0);
    assert(dd.ntaps == 3);                                      /* the releasing key still adds its tap */
    /* every key released: the lowest sounding voice leads the tail */
    ENGINE_dddd.note_off(voice(1)); ENGINE_dddd.note_off(voice(2));
    for (int b = 0; b < 5; b++) block(sr, mr, 3);
    assert(pk[0] > 1000 && pk[1] == 0 && pk[2] == 0);

    /* six keys: the newest four become taps, in the order they were struck */
    const int mid[6] = { 48, 52, 55, 60, 64, 67 };
    int sv[6], mv[6];
    for (int i = 0; i < 6; i++) { sv[i] = i; mv[i] = mid[i]; }
    int order5[6] = { 5, 2, 4, 0, 3, 1 };                       /* strike order, by voice */
    for (int i = 0; i < 6; i++) { host_pitch_inc[0] = inc_of(mv[order5[i]]); ENGINE_dddd.note_on(voice(order5[i]), knobs); }
    for (int b = 0; b < 40; b++) block(sv, mv, 6);
    assert(pk[0] > 1000 && pk[0] <= 16384);
    for (int v = 1; v < 6; v++) assert(pk[v] == 0);
    assert(dd.ntaps == 4);
    for (int k = 0; k < 4; k++) {                               /* newest four: strike order 2..5 = voices 4, 0, 3, 1 */
        int want = (int)(4294967296.0 / inc_of(mv[order5[2 + k]]));
        while (want >= DD_BUF / 2) want >>= 1;
        assert(near(dd.tap[k], want));
    }

    /* all keys gone for a while, then a new one: the loop starts clean */
    for (int b = 0; b < 10; b++) { calls++; }                   /* (no voice rendered: they have all finished) */
    ENGINE_dddd.note_on(voice(2), knobs);
    assert(dbg_resets == 2);

    /* bounded over a long run with every knob at an extreme */
    for (int c = 0; c < 16; c++) {
        for (int k = 0; k < 4; k++) knobs[k] = (int16_t)((c >> k) & 1 ? 32767 : 0);
        for (int b = 0; b < 100; b++) { block(sv, mv, 6); assert(pk[0] <= 16384); }
    }
    printf("dddd plugin ok\n");
    return 0;
}
