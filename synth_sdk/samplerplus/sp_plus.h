/* sampler+ voice (docs/46): the stock synth sampler voice (sp_voice.c, bit-exact) plus an own envelope (replaces the
 * slot's ADSR through the Adsr_render hook), filter with envelope / velocity, pitch envelope, loop crossfade,
 * ping-pong, loop in release, velocity / random start, drive and tempo sync (repitch). With every render extra off
 * the stock render runs unchanged. Integer only; constants from sp_plus_prepare, no divisions per sample. */
#ifndef SP_PLUS_H
#define SP_PLUS_H
#include "sp_voice.h"

/* per-slot extras (patch JSON "op1r"; 0 = off / neutral, except sustain 127 = full) */
typedef struct {
    uint8_t attack, hold, decay, sustain, release;   /* 0..127 (times: sp_time_samples; sustain level 0..127) */
    uint8_t curve_exp;                               /* 0 linear, 1 exponential decay / release */
    uint8_t vel_amt;                                 /* 0..127: velocity -> level (0: velocity ignored) */
    uint8_t filter;                                  /* 0 off, 1 low pass, 2 high pass, 3 band pass */
    uint8_t cutoff, resonance;                       /* 0..127 (30 Hz .. 16 kHz), 0..127 */
    int8_t  fenv;                                    /* -127..127 cutoff steps at full envelope */
    int8_t  vel_cutoff;                              /* -127..127 cutoff steps at velocity 127 vs 64 */
    int8_t  pitch_env;                               /* -48..48 semitones at the start, gliding to 0 */
    uint8_t pitch_time;                              /* 0..127 */
    uint8_t loop_mode;                               /* 0 forward (stock), 1 ping-pong */
    uint8_t loop_rel;                                /* 1: keep looping after key-up */
    uint8_t xfade;                                   /* 0..127: loop crossfade, xfade * 32 samples (forward loop) */
    uint8_t vel_start;                               /* 0..127: softer notes start later (up to vel_start * 64 samples) */
    uint8_t rand_start;                              /* 0..127: random start offset up to rand_start * 64 samples */
    uint8_t drive;                                   /* 0..127: gain 1..4 into a soft clip */
    uint8_t sync;                                    /* 0 off, 1..6: the region lasts 1/4, 1/2, 1, 2, 4, 8 bars at C4 */
} sp_extra;
#define SP_NFIELDS 21
int  sp_extra_set(sp_extra *x, int i, int32_t v);     /* field i (struct order) clamped; returns the stored value */
int  sp_extra_get(const sp_extra *x, int i);
void sp_extra_default(sp_extra *x);                   /* the stock-like start: sustain full, short release */
uint32_t sp_time_samples(uint8_t t);                  /* 0..127 -> 0 .. ~11 s */

typedef struct {
    uint8_t render_active;                            /* any render extra on (else the stock render) */
    uint32_t t_att, t_hold, t_dec, t_rel;
    int32_t a_step, d_step, r_step, sus;              /* Q30 */
    int32_t kd, kr;                                   /* exponential factors, Q15 */
    int32_t p_start, p_step;                          /* pitch envelope, 1/96 octave << 16 */
    int32_t f_k;                                      /* filter damping Q14 */
    int32_t fa[128][3];                               /* filter coefficients per cutoff step (Q15) */
    int32_t drive_q12;
    int32_t sync_q12;                                 /* tempo sync speed factor, Q12 (4096 = off) */
} sp_pre;

typedef struct {
    int32_t env;                                      /* Q30 */
    uint8_t stage;                                    /* 0 A, 1 H, 2 D, 3 S, 4 R, 5 done, 6 fast fade */
    uint32_t stage_n;
    int32_t fade_step;
    int32_t pitch_u;
    int8_t  dir;                                      /* +1 / -1 (reverse, ping-pong) */
    int16_t cut_vel;                                  /* velocity cutoff offset of this note (steps) */
    int32_t ic1, ic2;                                 /* filter state */
} sp_xvoice;

void sp_plus_prepare(const sp_extra *x, sp_pre *c);   /* when the extras change (GUI thread) */
/* tempo sync factor: knobs (region), rate (sampler object +4), samples per bar (4 beats) at the current tempo */
void sp_plus_sync(const sp_extra *x, sp_pre *c, const int16_t *knobs, int32_t rate, uint32_t samples_per_bar);
void sp_plus_note_on(sp_voice *v, sp_xvoice *s, const int16_t *knobs, const sp_extra *x, const sp_pre *c);
/* velocity 0..127 of the note (the firmware writes it around note_on: applied at the first render) */
void sp_plus_velocity(sp_voice *v, sp_xvoice *s, const int16_t *knobs, const sp_extra *x, int velocity);
/* level (Q15) of the note for sp_plus_env: velocity << 8 and the voice gain as the stock ADSR, scaled by vel_amt */
int32_t sp_plus_level(const sp_extra *x, int32_t vel_w, int32_t voice_gain);
/* the Adsr_render replacement: state = the voice's ADSR object state (7 start, 4 / 5 release, 6 retrigger fade,
 * 8 steal fade); a note on a free voice starts in state 1 with the block counter 0 (VoiceCtl 0x0191211a): fresh =
 * counter == 0. env[128] Q15 out (x level Q15); returns 1 when the voice is done */
int  sp_plus_env(sp_xvoice *s, const sp_extra *x, const sp_pre *c, int32_t *state, int16_t *env, int32_t level, int fresh);
void sp_plus_render(sp_voice *v, sp_xvoice *s, int16_t *out, const int16_t *knobs, const sp_extra *x, const sp_pre *c,
                    const int32_t *inc, int32_t rate, const int16_t *data, const int16_t *fir);
#define SP_INC_C4 3691513                             /* phase-increment bus at C4, octave 0 (emulator dump) */
#endif
