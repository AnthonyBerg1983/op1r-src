/* Synth randomiser core (rn_core.c): per synth slot the original sound and the RN_KEEP newest random ones.
 *   drop: the live sound into L[c], then forward one step, or a new random sound
 *   lift: the live sound into L[c], then back one step (nothing at the original)
 *   another engine in the slot starts over with it as the original */
#ifndef RN_CORE_H
#define RN_CORE_H
#include <stdint.h>

#define RN_SLOTS   8
#define RN_KEEP    6
#define RN_NAME    16
#define RN_TYPES   48                       /* FX / LFO list rows */
#define RN_ATT_DIV 4                        /* attack in the lower 1/4 of its range */
#define RN_REL_DIV 2                        /* release in the lower 1/2 */
#define RN_SUS_DIV 4                        /* a sustain in its lower 1/4 ... */
#define RN_DEC_DIV 8                        /* ... gets a decay of at least 1/8 */

typedef struct {
    char    type[RN_NAME];
    int16_t knobs[8], adsr[8];
    char    fx[RN_NAME], lfo[RN_NAME];
    int16_t fx_params[8], lfo_params[8];
    uint8_t fx_on, lfo_on;
} rn_step;

typedef struct { int16_t lo, hi; uint8_t skip; } rn_range;        /* skip: kept */
typedef struct { char name[RN_NAME]; int16_t def[8]; } rn_type;   /* list row: name, default words */

typedef struct {                            /* firmware / host services; 0 = ok */
    int  (*read)(int slot, rn_step *out);
    int  (*apply)(int slot, const rn_step *s);
    int  (*engine_ranges)(int slot, rn_range r[8]);
    int  (*fx_ranges)(int slot, rn_range r[8]);             /* of the live FX type */
    int  (*lfo_ranges)(int slot, rn_range r[8]);            /* of the live LFO type, not quad */
    int  (*fx_set)(int slot, const int16_t w[8]);           /* as turning the knobs */
    int  (*lfo_set)(int slot, const int16_t w[8]);
    uint32_t (*seed)(void);
    int  (*busy)(void);                                     /* 1: no step now (a save is writing the sound) */
} rn_env;

typedef struct { rn_step L[RN_KEEP + 1]; uint8_t n, c, used; } rn_hist;

typedef struct {
    const rn_env *env;
    rn_hist  h[RN_SLOTS];
    rn_type  fx[RN_TYPES], lfo[RN_TYPES];
    int      nfx, nlfo;
    rn_range adsr[4];
    uint32_t rng;
} rn_t;

void rn_init(rn_t *r, const rn_env *env);
int  rn_lists(rn_t *r, const char *fx, const char *lfo);  /* "name=[d0, ...];..." rows; 1 = both usable */
int  rn_drop(rn_t *r, int slot);                           /* 1 = the sound changed */
int  rn_lift(rn_t *r, int slot);
uint32_t rn_rand(rn_t *r);
void rn_knobs(rn_t *r, int16_t w[8], const rn_range g[8]);
void rn_env_adsr(rn_t *r, int16_t w[8], const rn_range g[4]);
void rn_quad(rn_t *r, int16_t w[8]);                            /* lfo/quadlfo.c words */
uint32_t rn_qget(const int16_t *w, int pos, int n);
int  rn_parse_types(rn_type *out, int max, const char *s);
#endif
