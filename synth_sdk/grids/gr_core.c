/* grids core (docs/52): ReadDrumMap / EvaluateDrums of Grids' pattern_generator.cc, same integer arithmetic
 * (U8Mix, U8U8MulShift8), so the triggers match the module bit for bit for the same random bytes. */
#include "gr_core.h"
#include "gr_map.h"

static inline uint8_t mix(uint8_t a, uint8_t b, uint8_t bal)         /* avrlib U8Mix */
{
    uint16_t s = (uint16_t)(a * (255 - bal));
    s = (uint16_t)(s + b * bal);
    return (uint8_t)(s >> 8);
}

void gr_init(gr_t *g, uint32_t seed)
{
    uint8_t *p = (uint8_t *)g;
    for (unsigned i = 0; i < sizeof *g; i++) p[i] = 0;
    g->rng = seed ? seed : 0x9E3779B9u;
}

uint8_t gr_rand_byte(gr_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return (uint8_t)(x >> 24);
}

uint8_t gr_level(uint8_t x, uint8_t y, int part, int step)
{
    int i = x >> 6, j = y >> 6, o = part * GR_STEPS + step;
    uint8_t a = gr_map[i][j][o], b = gr_map[i + 1][j][o], c = gr_map[i][j + 1][o], d = gr_map[i + 1][j + 1][o];
    return mix(mix(a, b, (uint8_t)(x << 2)), mix(c, d, (uint8_t)(x << 2)), (uint8_t)(y << 2));
}

static int eval_with(const gr_set *s, const uint8_t *pert, int step)
{
    int out = 0;
    for (int i = 0; i < GR_PARTS; i++) {
        uint8_t level = gr_level(s->x, s->y, i, step);
        level = level < 255 - pert[i] ? (uint8_t)(level + pert[i]) : 255;
        if (level > (uint8_t)~s->density[i]) {
            out |= 1 << i;
            if (level > 192) out |= 8 << i;
        }
    }
    return out;
}

int gr_eval(gr_t *g, int step)
{
    if (step == 0)
        for (int i = 0; i < GR_PARTS; i++)
            g->perturb[i] = (uint8_t)((gr_rand_byte(g) * (g->s.chaos >> 2)) >> 8);
    return eval_with(&g->s, g->perturb, step);
}

void gr_bar(const gr_t *g, uint32_t hits[GR_PARTS], uint32_t acc[GR_PARTS])
{
    for (int i = 0; i < GR_PARTS; i++) hits[i] = acc[i] = 0;
    for (int t = 0; t < GR_STEPS; t++) {
        int e = eval_with(&g->s, g->perturb, t);
        for (int i = 0; i < GR_PARTS; i++) {
            if (e & (1 << i)) hits[i] |= 1u << t;
            if (e & (8 << i)) acc[i] |= 1u << t;
        }
    }
}
