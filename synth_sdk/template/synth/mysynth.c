/* Template synth engine: two detuned saws through a one-pole low-pass.
 *   blue  : detune of the second saw
 *   green : low-pass cutoff
 *   white : drive (soft clip)
 *   red   : level
 * Copy this folder to plugins/<name>/, rename the file, the symbol and "name" in plugin.json.
 * Copyright (C) <year> <you>. GPL-3.0-or-later with the OP-1 firmware permission (see LICENSE and NOTICE). */
#include "op1_plugin.h"

typedef struct {              /* one per voice, max 0x24 bytes */
    uint32_t vtable;          /* firmware-owned: never write */
    uint32_t ph1, ph2;        /* saw phases (2^32 = one cycle) */
    int32_t  lp;              /* low-pass state */
} my_voice_t;

static void my_note_on(void *self, const int16_t *params)
{
    my_voice_t *v = (my_voice_t *)self; (void)params;
    v->ph1 = 0; v->ph2 = 0x80000000u; v->lp = 0;
}

static void my_render(void *self, int16_t *out, const int16_t *params)
{
    my_voice_t *v = (my_voice_t *)self;
    const int32_t *inc = OP1_PITCH_INC;                    /* per-sample phase increment of this note */
    uint32_t detune = 32768 + (params[0] >> 4);            /* 1.0 .. 1.06, Q15 */
    int32_t  cut    = 512 + (params[1] >> 1);              /* one-pole coefficient, Q15 */
    int32_t  drive  = 32768 + params[2] * 3;               /* 1 .. 4x, Q15 */
    int32_t  gain   = (params[3] * op1_voice_vel_gain(self)) >> 15;
    uint32_t ph1 = v->ph1, ph2 = v->ph2; int32_t lp = v->lp;

    for (int n = 0; n < OP1_BLOCK; n++) {
        uint32_t i = (uint32_t)inc[n];
        ph1 += i;
        ph2 += i + (i >> 15) * (detune - 32768);             /* i * detune, 32-bit only (no 64-bit math) */
        int32_t s = (((int32_t)ph1 >> 17) + ((int32_t)ph2 >> 17)) >> 1;   /* two saws, Q15 */
        lp += ((s - lp) * cut) >> 15;
        int32_t x = (lp * drive) >> 15;
        x = x > 32767 ? 32767 : (x < -32767 ? -32767 : x);
        x -= (((((x * x) >> 15) * x) >> 15) * 10923) >> 15;   /* soft clip x - x^3/3 (no division) */
        out[n] = op1_sat16((x * gain) >> 15);
    }
    v->ph1 = ph1; v->ph2 = ph2; v->lp = lp;
}

/* note_off may be 0; press: per knob OP1_PRESS_NONE or OP1_PRESS_PAGE (second parameter page, see op1_plugin.h) */
const op1_engine_t ENGINE_mysynth = { my_note_on, my_render, 0, { OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE, OP1_PRESS_NONE } };
