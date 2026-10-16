/* sampler+ voice extras (docs/46): host checks.  cc -O1 -I.. sp_plus_test.c ../samplerplus/sp_plus.c ../samplerplus/sp_voice.c -lm */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../samplerplus/sp_plus.h"

#define N 264600
static int16_t data[N + 64], fir_t[16 * 64 + 64];
static int16_t *const fir = fir_t + 63;                /* tap k of phase p at fir[k * 64 - p] */
static int32_t inc[SP_BLOCK];
static const int32_t RATE = 0x1511F7C;                 /* the dumped sampler object's rate */

static void setup_fir(void)                            /* linear interpolation between taps 7 and 8 */
{
    for (int p = 0; p < 64; p++) { fir[7 * 64 - p] = (int16_t)(32767 - p * 512); fir[8 * 64 - p] = (int16_t)(p * 512); }
}
static void fill_sine(double period)   /* small level: the stock gain (x16) would clip */
{
    for (int i = 0; i < N + 64; i++) data[i] = (int16_t)(2000 * sin(2 * M_PI * i / period));
}
static void fill_noise(void)
{
    uint32_t r = 1;
    for (int i = 0; i < N + 64; i++) { r = r * 1664525u + 1013904223u; data[i] = (int16_t)(((int32_t)(r >> 16) - 32768) / 16); }
}

int main(void)
{
    setup_fir();
    for (int n = 0; n < SP_BLOCK; n++) inc[n] = SP_INC_C4;
    int16_t k[8] = { 2900, 3000, 3100, 8000, 9728, 0, 0, 23170 };
    static sp_extra x; static sp_pre c; sp_voice v, w; sp_xvoice s; int16_t a[SP_BLOCK], b[SP_BLOCK], env[SP_BLOCK];

    /* 1. no render extra: identical to the stock render */
    fill_sine(100.3);
    sp_extra_default(&x); sp_plus_prepare(&x, &c);
    assert(!c.render_active);
    sp_stock_note_on(&w, k); sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 127);
    for (int blk = 0; blk < 200; blk++) {
        sp_stock_render(&w, a, k, inc, RATE, data, fir); sp_plus_render(&v, &s, b, k, &x, &c, inc, RATE, data, fir);
        assert(!memcmp(a, b, sizeof a) && v.pos == w.pos);
    }

    /* 2. envelope: attack, decay to sustain, release, done; fast fades */
    sp_extra_default(&x); x.attack = 40; x.decay = 40; x.sustain = 64; x.release = 40; x.vel_amt = 0;
    sp_plus_prepare(&x, &c);
    int32_t st = 7; int blocks = 0, peak = 0;
    sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 100);
    for (; blocks < 2000 && s.stage != 3; blocks++) { assert(!sp_plus_env(&s, &x, &c, &st, env, 32767)); if (env[127] > peak) peak = env[127]; }
    assert(s.stage == 3 && peak > 32000 && abs(env[127] - 64 * 32767 / 127) < 200);
    st = 4; int d = 0;
    for (blocks = 0; blocks < 5000 && !d; blocks++) d = sp_plus_env(&s, &x, &c, &st, env, 32767);
    assert(d && st == 0 && blocks * SP_BLOCK >= (int)c.t_rel / 2 - 2 * SP_BLOCK /* linear, from sustain 0.5 */);
    st = 7; sp_plus_env(&s, &x, &c, &st, env, 32767); st = 6;
    assert(!sp_plus_env(&s, &x, &c, &st, env, 32767) && st == 7);                 /* retrigger fade -> start again */
    st = 8; assert(sp_plus_env(&s, &x, &c, &st, env, 32767) || sp_plus_env(&s, &x, &c, &st, env, 32767));

    /* 3. ping-pong: stays inside the loop, turns at both ends (3 octaves up: faster) */
    for (int n = 0; n < SP_BLOCK; n++) inc[n] = SP_INC_C4 * 8;
    sp_extra_default(&x); x.loop_mode = 1; sp_plus_prepare(&x, &c);
    sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 127);
    int32_t ls = sp_bound(k[1], k[5]), le = sp_bound(k[2], k[6]); int turns = 0, dir = 1, inloop = 0;
    for (int blk = 0; blk < 400; blk++) {
        sp_plus_render(&v, &s, a, k, &x, &c, inc, RATE, data, fir);
        if (s.dir != dir) { turns++; dir = s.dir; }
        if (v.pos >= ls) inloop = 1;
        if (inloop) assert(v.pos >= ls - 40000 && v.pos <= le + 40000);
    }
    assert(turns >= 4);

    /* 4. crossfade: smaller jump at the loop wrap (sine period not dividing the loop) */
    int jump[2];
    for (int xf = 0; xf < 2; xf++) {
        sp_extra_default(&x); x.loop_rel = 1; x.xfade = xf ? 40 : 0; sp_plus_prepare(&x, &c);
        sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 127);
        int prev = 0, mx = 0;
        for (int blk = 0; blk < 400; blk++) {
            sp_plus_render(&v, &s, a, k, &x, &c, inc, RATE, data, fir);
            for (int n = 0; n < SP_BLOCK; n++) { if (blk > 100 && abs(a[n] - prev) > mx) mx = abs(a[n] - prev); prev = a[n]; }
        }
        jump[xf] = mx;
    }
    printf("loop wrap: max step %d without, %d with crossfade\n", jump[0], jump[1]);
    fflush(stdout); assert(jump[1] < jump[0]);

    /* 5. low pass removes high frequencies (noise) */
    fill_noise();
    double e[2];
    for (int f = 0; f < 2; f++) {
        sp_extra_default(&x); x.loop_rel = 1; x.filter = f; x.cutoff = 40; sp_plus_prepare(&x, &c);
        sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 127); s.env = 0;
        double acc = 0; int prev = 0;
        for (int blk = 0; blk < 100; blk++) {
            sp_plus_render(&v, &s, a, k, &x, &c, inc, RATE, data, fir);
            for (int n = 0; n < SP_BLOCK; n++) { acc += (double)(a[n] - prev) * (a[n] - prev); prev = a[n]; }
        }
        e[f] = acc;
    }
    printf("HF energy: %.3g off, %.3g low pass\n", e[0], e[1]);
    assert(e[1] < e[0] * 0.05);

    for (int n = 0; n < SP_BLOCK; n++) inc[n] = SP_INC_C4;
    /* 6. tempo sync: 1 bar at 120 BPM (88200 samples) plays the region in about that time */
    fill_sine(100.3);
    sp_extra_default(&x); x.sync = 3; sp_plus_prepare(&x, &c); sp_plus_sync(&x, &c, k, RATE, 88200);
    sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 127);
    int32_t end = sp_bound(k[3], 0); int smp = 0;
    int16_t k2[8]; memcpy(k2, k, sizeof k2); k2[2] = 0;                    /* no loop */
    while (v.pos < end && smp < 400000) { sp_plus_render(&v, &s, a, k2, &x, &c, inc, RATE, data, fir); smp += SP_BLOCK; }
    printf("sync 1 bar: region in %d samples (want 88200), factor %d/4096\n", smp, c.sync_q12);
    assert(abs(smp - 88200) < 1500);

    /* 7. pitch envelope +12: the first block moves twice as far */
    sp_extra_default(&x); sp_plus_prepare(&x, &c); sp_plus_note_on(&w, &s, k, &x, &c); sp_plus_velocity(&w, &s, k, &x, 127);
    sp_extra_default(&x); x.pitch_env = 12; x.pitch_time = 100; x.loop_rel = 0; sp_plus_prepare(&x, &c);
    sp_xvoice s2; sp_plus_note_on(&v, &s2, k2, &x, &c); sp_plus_velocity(&v, &s2, k2, &x, 127);
    int32_t p0 = w.pos, q0 = v.pos;
    sp_stock_render(&w, a, k2, inc, RATE, data, fir); sp_plus_render(&v, &s2, b, k2, &x, &c, inc, RATE, data, fir);
    double ratio = (double)(v.pos - q0) / (w.pos - p0);
    printf("pitch env +12: step ratio %.3f\n", ratio);
    assert(ratio > 1.95 && ratio < 2.05);

    /* 8. velocity start: a soft note starts later */
    sp_extra_default(&x); x.vel_start = 127; sp_plus_prepare(&x, &c);
    sp_plus_note_on(&v, &s, k, &x, &c); sp_plus_velocity(&v, &s, k, &x, 0);
    assert(v.pos == sp_bound(k[0], 0) + ((127 * 64) << 12));
    puts("OK: sampler+ voice (stock path, envelope, ping-pong, crossfade, filter, sync, pitch env, start)");
    return 0;
}
