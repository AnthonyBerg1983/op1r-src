/* Host test of rnd/rn_core.c: generators (in range, skipped words kept, playable envelope, quad words), the type lists,
 * and (test_history) the history and the order of a random step.
 *   mkdir -p build && cc -DOP1_HOST -I.. -Wall -Wextra -fsanitize=address,undefined rnd_test.c ../rnd/rn_core.c \
 *     -o build/rnd_test && build/rnd_test */
#include "../rnd/rn_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static rn_t R;

static void test_generators(void)
{
    static const int16_t shapes[][2] = { { 0, 32767 }, { -32768, 32767 }, { -16000, 5000 }, { 64, 14528 },
                                         { 8192, 32767 }, { 5, 6 }, { 1000, 1000 } };
    const int ns = (int)(sizeof shapes / sizeof shapes[0]);
    rn_init(&R, 0);
    /* knobs: words in range (a one-value range gives that value), skipped words kept */
    for (int n = 0; n < 20000; n++) {
        rn_range g[8]; int16_t w[8], before[8];
        for (int k = 0; k < 8; k++) {
            g[k].lo = shapes[(n + k) % ns][0]; g[k].hi = shapes[(n + k) % ns][1];
            g[k].skip = (uint8_t)((k == 1 && (n & 1)) || k == 4 || (k > 4 && (n & 2)));
        }
        for (int i = 0; i < 8; i++) before[i] = w[i] = (int16_t)(n * 7 + i);
        rn_knobs(&R, w, g);
        for (int k = 0; k < 8; k++)
            assert(g[k].skip ? w[k] == before[k] : w[k] >= g[k].lo && w[k] <= (g[k].hi > g[k].lo ? g[k].hi : g[k].lo));
    }
    /* envelope: attack low, release lower half, a low sustain gets a decay, words 4..7 kept */
    for (int n = 0; n < 20000; n++) {
        rn_range g[4]; int16_t w[8]; int32_t sp[4];
        for (int k = 0; k < 4; k++) { g[k].lo = (int16_t)(n & 1 ? -100 : 0); g[k].hi = (int16_t)(n & 1 ? 900 : 32767); g[k].skip = 0; }
        for (int k = 0; k < 4; k++) sp[k] = g[k].hi - g[k].lo;
        for (int i = 0; i < 8; i++) w[i] = (int16_t)i;
        rn_env_adsr(&R, w, g);
        assert(w[0] >= g[0].lo && w[0] <= g[0].lo + sp[0] / RN_ATT_DIV);
        assert(w[3] >= g[3].lo && w[3] <= g[3].lo + sp[3] / RN_REL_DIV);
        assert(w[1] >= g[1].lo && w[1] <= g[1].hi && w[2] >= g[2].lo && w[2] <= g[2].hi);
        if (w[2] < g[2].lo + sp[2] / RN_SUS_DIV) assert(w[1] >= g[1].lo + sp[1] / RN_DEC_DIV);
        for (int i = 4; i < 8; i++) assert(w[i] == i);
    }
    /* quad: fields in range, no self target, one row reaching a knob, amounts not 0, the marker */
    for (int n = 0; n < 20000; n++) {
        int16_t w[8]; int audible = 0;
        rn_quad(&R, w);
        assert(((uint16_t)w[7] >> 8) == 0xA5);
        for (unsigned i = 0; i < 4; i++) {
            int b = 30 * (int)i;
            uint32_t rate = rn_qget(w, b, 6), sync = rn_qget(w, b + 6, 1), wave = rn_qget(w, b + 7, 3),
                     type = rn_qget(w, b + 16, 3), tgt = rn_qget(w, b + 19, 5), amt = rn_qget(w, b + 24, 6);
            assert(wave <= 5 && type <= 5 && tgt <= 23 && (!sync || rate <= 10));
            assert(!(tgt >= 12 + 3 * i && tgt <= 14 + 3 * i));
            assert(amt >= 1 && amt != 32);
            audible |= tgt <= 11;
        }
        assert(audible);
    }
    R.rng = 0; assert(rn_rand(&R) != 0);                    /* a zero state never sticks */
    printf("generators ok\n");
}

static const char FX[] = "delay=[8000, 8000, 8000, 8000, 0, 0, 0, 0];grid=[8000, 8000, 18000, 18000, 8000, 8000, 8000, 8000];"
                         "tape=[3000, 9000, 6000, 20000, 21250, 0, 0, 0]";
static const char LFO[] = "tremolo=[16000,0,0,16000,0,0,0,0];midi=[2000,2000,2000,2000,2000,2000,2000,2000];"
                          "bend=[7200,7100,32767,0,0,0,0,2048];quad=[0,0,0,0,0,0,0,-23296]";

static void test_lists(void)
{
    rn_type t[4];
    assert(rn_parse_types(t, 4, "") == 0);
    int n = rn_parse_types(t, 4, "grid=[8000, 8000, 18000, 18000, 8000, 8000, 8000, 8000];cwo=[5000,8192,16384,32767,0,0,0,0];"
                                 "dna=[-16000, 5000, 0, 1024, 0, 0, 0, 0];averyveryverylongname=[1]");
    assert(n == 4 && !strcmp(t[0].name, "grid") && t[0].def[2] == 18000 && t[0].def[7] == 8000);
    assert(!strcmp(t[1].name, "cwo") && t[1].def[3] == 32767 && t[2].def[0] == -16000);
    assert(strlen(t[3].name) == RN_NAME - 1 && t[3].def[0] == 1 && t[3].def[1] == 0);
    assert(rn_parse_types(t, 2, "a=[1];b=[2];c=[3]") == 2);     /* at most max rows */
    rn_init(&R, 0);
    assert(rn_lists(&R, FX, LFO) && R.nfx == 3 && R.nlfo == 3); /* midi left out */
    for (int i = 0; i < R.nlfo; i++) assert(strcmp(R.lfo[i].name, "midi"));
    rn_init(&R, 0);
    assert(!rn_lists(&R, FX, "midi=[0]"));                      /* only midi: no usable LFO */
    rn_init(&R, 0);                                             /* single-entry lists */
    assert(rn_lists(&R, "delay=[8000, 8000, 8000, 8000, 0, 0, 0, 0]", "quad=[0,0,0,0,0,0,0,-23296]") && R.nfx == 1 && R.nlfo == 1);
    printf("lists ok\n");
}

/* ---- history: a mock device (one live sound per slot) ---- */
static rn_step dev[RN_SLOTS];
static int napply, ngen, fail_apply, order[8], norder;
enum { O_APPLY = 1, O_FXR, O_LFOR, O_FXSET, O_LFOSET };
static void log_op(int o) { if (norder < 8) order[norder++] = o; }
static void rg8(rn_range g[8], int lo, int hi)           /* words 4..7 skipped */
{ for (int k = 0; k < 8; k++) { g[k].lo = (int16_t)lo; g[k].hi = (int16_t)hi; g[k].skip = (uint8_t)(k >= 4); } }
static int e_read(int slot, rn_step *o) { *o = dev[slot]; return 0; }
static int e_apply(int slot, const rn_step *s) { if (fail_apply) return -1; dev[slot] = *s; napply++; log_op(O_APPLY); return 0; }
static int e_engine(int slot, rn_range g[8])
{
    ngen++;
    rg8(g, -16000, 5000);
    if (!strcmp(dev[slot].type, "sampler")) { rg8(g, 0, 32767); g[0].skip = g[1].skip = 1; }    /* linked start / end */
    if (!strcmp(dev[slot].type, "linked")) for (int k = 0; k < 4; k++) g[k].skip = 1;
    if (!strcmp(dev[slot].type, "arc")) for (int k = 5; k < 8; k++) { g[k].lo = 1; g[k].hi = 32767; g[k].skip = 0; }   /* page 2 */
    return 0;
}
static int e_fxr(int slot, rn_range g[8])
{
    static const int16_t r5[4][2] = { { 64, 14528 }, { 0, 24576 }, { 0, 22528 }, { 8192, 32767 } };   /* type 5, plugload.c */
    log_op(O_FXR);
    if (!dev[slot].fx_on) return -1;
    rg8(g, 100, 30000);
    if (!strcmp(dev[slot].fx, "tape")) {                                         /* plugin FX, page 2 on knobs 0 and 1 */
        for (int k = 0; k < 4; k++) { g[k].lo = r5[k][0]; g[k].hi = r5[k][1]; }
        for (int k = 5; k < 7; k++) { g[k].lo = 0; g[k].hi = 32767; g[k].skip = 0; }
    }
    return 0;
}
static int e_lfor(int slot, rn_range g[8])
{
    log_op(O_LFOR);
    if (!dev[slot].lfo_on || !strcmp(dev[slot].lfo, "quad")) return -1;      /* quad: packed words, as the firmware */
    rg8(g, 0, 32767);
    if (!strcmp(dev[slot].lfo, "bend")) g[3].skip = 1;
    return 0;
}
static int e_fxset(int slot, const int16_t w[8]) { log_op(O_FXSET); memcpy(dev[slot].fx_params, w, 16); return 0; }
static int e_lfoset(int slot, const int16_t w[8]) { log_op(O_LFOSET); memcpy(dev[slot].lfo_params, w, 16); return 0; }
static uint32_t seedv;
static uint32_t e_seed(void) { return seedv += 0x9E3779B9u; }
static int busy;
static int e_busy(void) { return busy; }
static const rn_env env = { e_read, e_apply, e_engine, e_fxr, e_lfor, e_fxset, e_lfoset, e_seed, e_busy };

static void sound(int slot, const char *type)              /* a plain sound in a slot */
{
    memset(&dev[slot], 0, sizeof dev[slot]);
    strcpy(dev[slot].type, type);
    for (int i = 0; i < 8; i++) { dev[slot].knobs[i] = (int16_t)(100 + i); dev[slot].adsr[i] = (int16_t)(200 + i); }
    strcpy(dev[slot].fx, "delay"); strcpy(dev[slot].lfo, "tremolo");
}
static void setup(void)
{
    rn_init(&R, &env);
    assert(rn_lists(&R, FX, LFO));
    for (int k = 0; k < 4; k++) { R.adsr[k].lo = 0; R.adsr[k].hi = 32767; R.adsr[k].skip = 0; }
    for (int s = 0; s < RN_SLOTS; s++) sound(s, "fm");
    napply = ngen = fail_apply = busy = 0; norder = 0;
}
static int same(const rn_step *a, const rn_step *b) { return !memcmp(a, b, sizeof *a); }

static void test_history(void)
{
    rn_step orig, r1, step[9];
    /* lift without a history: nothing */
    setup(); orig = dev[0];
    assert(!rn_lift(&R, 0) && napply == 0 && same(&dev[0], &orig));
    /* first drop: a new sound; lift: the original; lift again: nothing; drop: forward to the same sound */
    setup(); orig = dev[0];
    assert(rn_drop(&R, 0) && !same(&dev[0], &orig));
    r1 = dev[0];
    assert(rn_lift(&R, 0) && same(&dev[0], &orig));
    assert(!rn_lift(&R, 0));
    assert(rn_drop(&R, 0) && same(&dev[0], &r1));
    /* 8 drops: the 6 newest kept; back 5, then the original; forward 6 without new sounds; then a new one */
    setup(); orig = dev[0];
    for (int i = 1; i <= 8; i++) { assert(rn_drop(&R, 0)); step[i] = dev[0]; }
    assert(R.h[0].n == RN_KEEP && R.h[0].c == RN_KEEP);
    for (int i = 7; i >= 3; i--) { assert(rn_lift(&R, 0)); assert(same(&dev[0], &step[i])); }
    assert(rn_lift(&R, 0) && same(&dev[0], &orig));
    assert(!rn_lift(&R, 0));
    int g0 = ngen;
    for (int i = 3; i <= 8; i++) { assert(rn_drop(&R, 0)); assert(same(&dev[0], &step[i])); }
    assert(ngen == g0);
    assert(rn_drop(&R, 0) && ngen == g0 + 1);                /* new; step 3 dropped */
    for (int i = 0; i < RN_KEEP - 1; i++) assert(rn_lift(&R, 0));
    assert(same(&dev[0], &step[4]));
    assert(rn_lift(&R, 0) && same(&dev[0], &orig));          /* the original is never dropped */
    printf("history ok\n");
    /* a hand tweak after a step is kept when stepping away and back */
    setup();
    assert(rn_drop(&R, 0) && rn_drop(&R, 0));
    dev[0].knobs[0] = 1234; dev[0].fx_params[1] = 4321;
    rn_step tweaked = dev[0];
    assert(rn_lift(&R, 0) && rn_drop(&R, 0) && same(&dev[0], &tweaked));
    /* another engine in the slot: the history starts over with it as the original */
    setup();
    assert(rn_drop(&R, 0) && rn_drop(&R, 0));
    sound(0, "dna"); orig = dev[0];
    assert(!rn_lift(&R, 0) && same(&dev[0], &orig));
    assert(rn_drop(&R, 0) && R.h[0].n == 1);
    assert(rn_lift(&R, 0) && same(&dev[0], &orig));
    /* slots keep their own histories; no such slot */
    setup(); rn_step o1 = dev[1];
    assert(rn_drop(&R, 0) && same(&dev[1], &o1) && !R.h[1].used);
    assert(!rn_drop(&R, RN_SLOTS) && !rn_lift(&R, -1));
    /* lists or envelope ranges not there yet: nothing */
    setup(); R.nfx = 0; orig = dev[0];
    assert(!rn_drop(&R, 0) && napply == 0 && same(&dev[0], &orig));
    setup(); R.adsr[2].hi = R.adsr[2].lo;
    assert(!rn_drop(&R, 0) && napply == 0);
    /* apply failure: nothing moves */
    setup(); assert(rn_drop(&R, 0)); r1 = dev[0];
    fail_apply = 1;
    assert(!rn_lift(&R, 0) && R.h[0].c == 1 && same(&dev[0], &r1));
    assert(!rn_drop(&R, 0) && R.h[0].n == 1 && R.h[0].c == 1);
    /* the firmware busy (its saver writing a sound): no step, nothing moves */
    setup(); assert(rn_drop(&R, 0)); r1 = dev[0];
    busy = 1; int na = napply;
    assert(!rn_drop(&R, 0) && !rn_lift(&R, 0) && napply == na && same(&dev[0], &r1) && R.h[0].n == 1 && R.h[0].c == 1);
    busy = 0;
    assert(rn_lift(&R, 0) && R.h[0].c == 0);
    /* unknown names round-trip */
    setup(); strcpy(dev[0].fx, "oldplug"); strcpy(dev[0].lfo, "midi"); dev[0].lfo_params[7] = -1; orig = dev[0];
    assert(rn_drop(&R, 0) && rn_lift(&R, 0) && same(&dev[0], &orig));
    /* 100 drops */
    setup(); orig = dev[0];
    for (int i = 0; i < 100; i++) { assert(rn_drop(&R, 0)); assert(R.h[0].c <= R.h[0].n && R.h[0].n <= RN_KEEP); }
    int backs = 0; while (rn_lift(&R, 0)) backs++;
    assert(backs == RN_KEEP && same(&dev[0], &orig));
    /* linked knobs kept: the sampler's start / end, an engine with all four linked */
    setup(); sound(0, "sampler"); rn_step b = dev[0];
    assert(rn_drop(&R, 0) && dev[0].knobs[0] == b.knobs[0] && dev[0].knobs[1] == b.knobs[1]);
    setup(); sound(0, "linked"); b = dev[0];
    assert(rn_drop(&R, 0) && !memcmp(dev[0].knobs, b.knobs, sizeof b.knobs) && dev[0].fx_on && dev[0].lfo_on);
    /* a plugin engine's page-2 words: in their ranges, the selector kept */
    int paged = 0;
    for (int n = 0; n < 200; n++) {
        setup(); seedv = (uint32_t)n * 104729u; sound(0, "arc"); dev[0].knobs[4] = 0x5309; b = dev[0];
        assert(rn_drop(&R, 0) && dev[0].knobs[4] == b.knobs[4]);
        for (int i = 5; i < 8; i++) { assert(dev[0].knobs[i] >= 1); paged += dev[0].knobs[i] != b.knobs[i]; }
    }
    assert(paged > 0);
    /* single-entry lists */
    rn_init(&R, &env);
    assert(rn_lists(&R, "delay=[8000, 8000, 8000, 8000, 0, 0, 0, 0]", "quad=[0,0,0,0,0,0,0,-23296]"));
    for (int k = 0; k < 4; k++) { R.adsr[k].lo = 0; R.adsr[k].hi = 32767; R.adsr[k].skip = 0; }
    sound(0, "fm");
    assert(rn_drop(&R, 0) && !strcmp(dev[0].fx, "delay") && !strcmp(dev[0].lfo, "quad"));
    printf("history edges ok\n");
    /* a random step: types live first, then the new FX / LFO knob words in their ranges; defaults kept elsewhere */
    int fxpaged = 0;
    for (int n = 0; n < 2000; n++) {
        setup(); seedv = (uint32_t)n * 7919u;
        rn_step before = dev[0];
        assert(rn_drop(&R, 0));
        const rn_step *s = &dev[0];
        assert(order[0] == O_APPLY);
        assert(!strcmp(s->type, before.type) && s->fx_on && s->lfo_on && strcmp(s->lfo, "midi"));
        for (int i = 4; i < 8; i++) assert(s->knobs[i] == before.knobs[i] && s->adsr[i] == before.adsr[i]);
        for (int k = 0; k < 4; k++) assert(s->knobs[k] >= -16000 && s->knobs[k] <= 5000);
        const rn_type *fx = 0;
        for (int i = 0; i < R.nfx; i++) if (!strcmp(R.fx[i].name, s->fx)) fx = &R.fx[i];
        assert(fx);
        if (!strcmp(s->fx, "tape")) {                       /* plugin FX: page-2 words 5, 6 random, selector and 7 kept */
            assert(s->fx_params[0] >= 64 && s->fx_params[0] <= 14528 && s->fx_params[3] >= 8192);
            assert(s->fx_params[4] == fx->def[4] && s->fx_params[7] == fx->def[7]);
            assert(s->fx_params[5] >= 0 && s->fx_params[6] >= 0);
            fxpaged += s->fx_params[5] != fx->def[5] || s->fx_params[6] != fx->def[6];
        } else {
            for (int i = 4; i < 8; i++) assert(s->fx_params[i] == fx->def[i]);  /* selector, second page */
            for (int k = 0; k < 4; k++) assert(s->fx_params[k] >= 100 && s->fx_params[k] <= 30000);
        }
        int quad = !strcmp(s->lfo, "quad"), lfoset = 0;
        for (int i = 0; i < norder; i++) lfoset |= order[i] == O_LFOSET;
        assert(quad ? !lfoset && ((uint16_t)s->lfo_params[7] >> 8) == 0xA5 : lfoset);
        if (!strcmp(s->lfo, "bend")) assert(s->lfo_params[3] == 0);           /* skipped: its default */
    }
    assert(fxpaged > 0);
    printf("random steps ok\n");
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    test_generators();
    test_lists();
    test_history();
    printf("rnd ok\n");
    return 0;
}
