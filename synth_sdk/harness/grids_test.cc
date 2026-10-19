/* Host test of grids/gr_core.c (docs/52) against Mutable Instruments' original pattern_generator.cc.
 *   cc -c -O1 -Wall -fsanitize=address,undefined ../grids/gr_core.c -o build/gr_core.o
 *   g++ -O1 -Wall -fsanitize=address,undefined -DGRIDS_REF -I grids_shim -I $EURORACK grids_test.cc \
 *       $EURORACK/grids/pattern_generator.cc $EURORACK/grids/resources.cc build/gr_core.o -o build/grids_test
 * ($EURORACK: a checkout of pichenettes/eurorack; without -DGRIDS_REF only the own checks run, plain cc works). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern "C" {
#include "../grids/gr_core.h"
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void t_own(void)
{
    gr_t g; gr_init(&g, 1);
    g.s.x = 0; g.s.y = 0; g.s.chaos = 0;
    for (int i = 0; i < 3; i++) g.s.density[i] = 0;
    int any = 0;
    for (int t = 0; t < 32; t++) any |= gr_eval(&g, t);
    CHECK(!any, "density 0 fires (%x)", any);
    for (int i = 0; i < 3; i++) g.s.density[i] = 255;            /* full: every step with a level > 0 */
    uint32_t h[3], a[3]; gr_bar(&g, h, a);
    CHECK(h[0] & 1, "BD on step 0 at full density");
    CHECK(gr_level(0, 0, 0, 0) == (145 * 255 >> 8) * 255 >> 8, "map corner: node 10 step 0 through two U8Mix (145 -> 143)");
    for (int i = 0; i < 3; i++) CHECK((a[i] & ~h[i]) == 0, "accent without hit, part %d", i);
    printf("ok own checks\n");
}

#ifdef GRIDS_REF
#include "grids/pattern_generator.h"
static gr_t shadow;                                              /* the reference's random bytes: the same generator */
namespace avrlib { uint8_t (*ref_random_byte)(void) = [] { return gr_rand_byte(&shadow); }; }
using namespace grids;

static void t_reference(void)
{
    static const uint8_t xs[] = { 0, 31, 64, 100, 127, 160, 200, 255 }, dens[] = { 0, 40, 128, 200, 255 },
                         chaos[] = { 0, 90, 255 };
    int cases = 0, steps = 0;
    for (int res = 0; res < 2; res++)                            /* 24 ppqn (all 32 steps) / 4 ppqn (even steps) */
    for (uint8_t x : xs) for (uint8_t y : xs) for (uint8_t d : dens) for (uint8_t c : chaos) {
        PatternGenerator::set_output_mode(OUTPUT_MODE_DRUMS);
        PatternGenerator::set_output_clock(0);
        PatternGenerator::set_swing(0);
        PatternGenerator::Reset();
        PatternGeneratorSettings *s = PatternGenerator::mutable_settings();
        s->options.drums.x = x; s->options.drums.y = y; s->options.drums.randomness = c;
        s->density[0] = d; s->density[1] = (uint8_t)(255 - d); s->density[2] = (uint8_t)(d / 2 + 64);
        gr_t g; gr_init(&g, 12345u + cases); shadow = g;
        g.s.x = x; g.s.y = y; g.s.chaos = c;
        for (int i = 0; i < 3; i++) g.s.density[i] = s->density[i];
        int inc = res ? 6 : 3;
        for (int bar = 0; bar < 4; bar++)
            for (int t = 0; t < 32; t += inc / 3) {
                PatternGenerator::TickClock(inc);
                int want = PatternGenerator::state() & 0x3F, got = gr_eval(&g, t);
                if (want != got) { CHECK(0, "x %d y %d d %d chaos %d res %d bar %d step %d: ref %02x got %02x",
                                         x, y, d, c, res, bar, t, want, got); return; }
                steps++;
            }
        cases++;
    }
    printf("ok reference: %d settings, %d steps bit-exact\n", cases, steps);
}
#endif

int main(void)
{
    t_own();
#ifdef GRIDS_REF
    t_reference();
#endif
    printf(fails ? "%d FAILED\n" : "grids ok\n", fails);
    return fails != 0;
}
