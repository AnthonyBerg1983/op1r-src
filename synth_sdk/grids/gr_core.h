/* grids: Mutable Instruments Grids' pattern generator (drums mode), ported from pattern_generator.cc
 * (Copyright 2011 Emilie Gillet, GPL-3.0-or-later). docs/52. 3 parts (BD, SD, HH), 32 steps per bar. */
#ifndef GR_CORE_H
#define GR_CORE_H
#include <stdint.h>

#define GR_PARTS 3
#define GR_STEPS 32

typedef struct {
    uint8_t x, y, chaos;                 /* map position, randomness (0..255) */
    uint8_t density[GR_PARTS];           /* fill per part (0..255) */
} gr_set;

typedef struct {
    gr_set   s;
    uint8_t  perturb[GR_PARTS];          /* chaos offsets of this bar (drawn at step 0) */
    uint32_t rng;                        /* xorshift32, never 0 */
} gr_t;

void    gr_init(gr_t *g, uint32_t seed);
uint8_t gr_level(uint8_t x, uint8_t y, int part, int step);   /* the map's level at (x, y) */
uint8_t gr_rand_byte(gr_t *g);
/* step 0..31: hits in bits 0..2 (BD SD HH), accents in bits 3..5; at step 0 a new bar's chaos offsets */
int     gr_eval(gr_t *g, int step);
/* the bar as it plays now (chaos offsets of this bar): hits / accents per part as 32-bit masks, for the screen */
void    gr_bar(const gr_t *g, uint32_t hits[GR_PARTS], uint32_t acc[GR_PARTS]);
#endif
