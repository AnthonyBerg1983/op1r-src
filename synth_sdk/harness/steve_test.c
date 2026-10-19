/* Host test of steve/st_core.c (docs/51).
 *   cc -O1 -Wall -Wextra -fsanitize=address,undefined -I.. steve_test.c ../steve/st_core.c -lm -o build/steve_test \
 *     && build/steve_test [IN.raw]       IN: int16 mono 44.1 kHz (e.g. a speech loop) -> out/steve_*.wav to listen to */
#include "../steve/st_core.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
#define B  64
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static int16_t mem[8 * SR], zeros[60 * SR];   /* zeros: input while only the loop plays */
static const int32_t PCT = 42949673;                       /* 1 % in Q32 */

static void feed(st_t *s, const st_par *q, const int16_t *src, int n, int16_t *l, int16_t *r)   /* n samples */
{
    for (int t = 0; t < n; t += B) st_render(s, q, src + t, 0, l + t, r ? r + t : 0, n - t < B ? n - t : B);
}
static void capture(st_t *s, const st_par *q, const int16_t *src, int len)  /* capture src[0 .. len + XF] */
{
    static int16_t junk[2][8 * SR];
    st_capture(s, len, 0);
    feed(s, q, src, (len + ST_XF + 1 + B) / B * B, junk[0], junk[1]);
    assert(s->state == ST_PLAY && s->len == len);
}

static void t_drift(void)                                   /* B gains len * drift samples per loop */
{
    st_t s; st_par q = { PCT / 2, 0, 8, 1, 2, 32767 };      /* +0.5 % */
    static int16_t src[SR], out[60 * SR];
    for (int i = 0; i < SR; i++) src[i] = (int16_t)(i * 7 % 2000);
    st_init(&s, mem, 8 * SR);
    int len = SR / 2;
    capture(&s, &q, src, len);
    feed(&s, &q, zeros, 20 * SR, out, 0);                   /* 40 loops */
    double want = fmod(20.0 * SR * 0.005, len);
    CHECK(fabs(st_offset(&s, 1) - want) < 2, "drift offset %u, want %.1f", st_offset(&s, 1), want);
    CHECK(s.loops == 40 || s.loops == 39, "loops %u", s.loops);
    q.drift = -PCT; st_unison(&s);                          /* -1 %: B lags (offset counts down from len) */
    feed(&s, &q, zeros, SR / 2, out, 0);
    double lag = SR / 2 * 0.01;
    CHECK(fabs(st_offset(&s, 1) - (len - lag)) < 2, "lag offset %u, want %.1f", st_offset(&s, 1), len - lag);
    q.copies = 4; q.drift = PCT; st_unison(&s);             /* copy k at k * drift */
    feed(&s, &q, zeros, SR / 2, out, 0);
    for (int k = 1; k < 4; k++)
        CHECK(abs((int)st_offset(&s, k) - (int)(k * SR / 2 * 0.01)) < 3, "copy %d offset %u", k, st_offset(&s, k));
    printf("ok drift\n");
}

static void t_steps(void)                                   /* lock on units, hold N loops, unison after the cycle */
{
    st_t s; st_par q = { 0, PCT, 8, 2, 2, 32767 };          /* 8 units, 1 % moves, hold 2 loops */
    static int16_t src[SR], out[SR];
    for (int i = 0; i < SR; i++) src[i] = (int16_t)(i * 13 % 3000);
    st_init(&s, mem, 8 * SR);
    int len = 8 * 1200;                                     /* units of 1200 samples */
    capture(&s, &q, src, len);
    uint32_t seen[64]; int ns = 0, stuck = 0, moved_in_hold = 0;
    uint32_t last = st_offset(&s, 1);
    for (int b = 0; b < 40 * SR / B && ns < 64; b++) {
        int held_before = s.held[1];
        st_render(&s, &q, zeros, 0, out, 0, B);
        uint32_t o = st_offset(&s, 1);
        if (held_before >= 0 && s.held[1] >= 0 && o != last) moved_in_hold++;
        if (s.held[1] == 0 && held_before < 0) {            /* landed */
            seen[ns++] = o;
            if (o % 1200) stuck++;
        }
        last = o;
    }
    CHECK(!stuck, "%d landings off the unit grid", stuck);
    CHECK(!moved_in_hold, "moved %d blocks while holding", moved_in_hold);
    CHECK(ns >= 9, "only %d landings", ns);
    int back = 0;
    for (int i = 0; i < ns; i++) back |= seen[i] == 0 && i > 0;
    CHECK(seen[0] == 1200 && seen[1] == 2400 && back, "units %u %u .., unison again %d", seen[0], seen[1], back);
    printf("ok steps (%d landings: %u %u %u ..)\n", ns, seen[0], seen[1], seen[2]);
}

static void t_clicks(void)                                  /* a loop that does not fit its length: no jump at the wraps */
{
    st_t s; st_par q = { PCT / 4, 0, 8, 1, 2, 32767 };
    static int16_t src[2 * SR], out[10 * SR];
    for (int i = 0; i < 2 * SR; i++) src[i] = (int16_t)(12000 * sin(2 * M_PI * 220.0 * i / SR));   /* 220 Hz, 0.3071 s loop */
    st_init(&s, mem, 8 * SR);
    capture(&s, &q, src, 13543);
    feed(&s, &q, zeros, 10 * SR, out, 0);
    int maxd = 0;
    for (int i = 1; i < 10 * SR; i++) { int d = abs(out[i] - out[i - 1]); if (d > maxd) maxd = d; }
    int sine_step = (int)(2 * 2 * 12000 * 0.62 * sin(M_PI * 220.0 / SR) * 1.1) + 60;   /* two copies' sines, steepest */
    CHECK(maxd <= sine_step, "max step %d (smooth: %d)", maxd, sine_step);
    printf("ok clicks (max step %d)\n", maxd);
}

static void t_stereo_mix(void)                              /* A left, B right; mix 0: dry; capture waits for the beat */
{
    st_t s; st_par q = { PCT, 0, 8, 1, 2, 32767 };
    static int16_t src[SR], l[SR], r[SR], silent[SR];
    for (int i = 0; i < SR; i++) src[i] = (int16_t)(i % 100 < 50 ? 8000 : -8000);
    st_init(&s, mem, 8 * SR);
    feed(&s, &q, src, 1024, l, r);                          /* no loop yet, mix 100 %: the input passes */
    CHECK(!memcmp(l, src, 2048) && !memcmp(r, src, 2048), "no loop: input muted");
    st_capture(&s, 4410, 1000);
    feed(&s, &q, src, 1024, l, r);
    CHECK(s.state == ST_CAPTURE && s.wp == 24, "beat delay: state %d wp %d", s.state, s.wp);
    feed(&s, &q, src, 8 * 1024, l, r);
    CHECK(s.state == ST_PLAY, "state %d", s.state);
    feed(&s, &q, silent, SR / 4, l, r);                     /* only the copies sound */
    st_unison(&s); q.drift = 0;
    feed(&s, &q, silent, SR / 4, l, r);
    int same = 1;
    for (int i = 0; i < SR / 4; i++) same &= abs(l[i] - r[i]) <= 1;
    CHECK(same, "unison: left (A) and right (B) differ");
    q.drift = 5 * PCT;
    feed(&s, &q, silent, SR / 4, l, r);
    int diff = 0;
    for (int i = 0; i < SR / 4; i++) diff += l[i] != r[i];
    CHECK(diff > SR / 8, "drifting: left and right equal (%d)", diff);
    q.mix = 0;
    feed(&s, &q, src, SR / 4, l, r);
    CHECK(!memcmp(l, src, SR / 4 * 2) && !memcmp(r, src, SR / 4 * 2), "mix 0 is not dry");
    static int16_t other[SR / 4];
    for (int i = 0; i < SR / 4; i++) other[i] = (int16_t)(-src[i] / 2);
    st_render(&s, &q, src, other, l, r, B);                 /* stereo input: the dry stays stereo */
    CHECK(!memcmp(l, src, B * 2) && !memcmp(r, other, B * 2), "stereo dry mixed");
    printf("ok stereo / mix / beat\n");
}

static void wav(const char *fn, const int16_t *l, const int16_t *r, int n)
{
    FILE *f = fopen(fn, "wb");
    if (!f) return;
    int ch = r ? 2 : 1;
    uint32_t d = (uint32_t)(n * 2 * ch), rate = SR, br = SR * 2 * ch, sz = 36 + d;
    uint16_t fmt = 1, c = (uint16_t)ch, ba = (uint16_t)(2 * ch), bits = 16;
    uint32_t sixteen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&c, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
    for (int i = 0; i < n; i++) { fwrite(&l[i], 2, 1, f); if (r) fwrite(&r[i], 2, 1, f); }
    fclose(f);
}

static void renders(const char *path)                       /* drift and steps pieces from a recording */
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("no %s\n", path); return; }
    static int16_t src[8 * SR], l[90 * SR], r[90 * SR];
    int n = (int)fread(src, 2, 8 * SR, f);
    fclose(f);
    int len = n > 2 * SR ? SR + SR / 4 : n - 1000;
    st_t s; st_par q = { PCT / 5, 0, 12, 4, 2, 32767 };     /* +0.2 %: a slow "It's Gonna Rain" */
    st_init(&s, mem, 8 * SR);
    capture(&s, &q, src, len);
    static int16_t silent[90 * SR];
    feed(&s, &q, silent, 60 * SR, l, r);
    wav("out/steve_drift.wav", l, r, 60 * SR);
    q.drift = 0; q.trans = PCT; st_unison(&s);              /* "Piano Phase": 12 units, 1 %, hold 4 */
    feed(&s, &q, silent, 60 * SR, l, r);
    wav("out/steve_steps.wav", l, r, 60 * SR);
    q.trans = 0; q.drift = PCT / 3; q.copies = 4; st_unison(&s);   /* "Come Out": 4 copies */
    feed(&s, &q, silent, 60 * SR, l, 0);
    wav("out/steve_4copies_mono.wav", l, 0, 60 * SR);
    printf("-> out/steve_drift.wav, out/steve_steps.wav, out/steve_4copies_mono.wav\n");
}

int main(int argc, char **argv)
{
    t_drift(); t_steps(); t_clicks(); t_stereo_mix();
    if (argc > 1) renders(argv[1]);
    printf(fails ? "%d FAILED\n" : "steve ok\n", fails);
    return fails != 0;
}
