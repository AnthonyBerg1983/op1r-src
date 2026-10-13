/* Host test: custom engines scale their output with the note velocity (op1_voice_vel_gain).
 *   for e in supersaw fmbell granular; do cc -DOP1_HOST -DENGINE=ENGINE_$e -I.. vel_test.c ../plugins/$e/$e.c -lm -o build/vel_$e && build/vel_$e; done */
#include "../op1_synth.h"
#include <math.h>
#include <stdio.h>
int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24]; int16_t host_voice_vel[6];
int16_t host_knobs[8][8]; int host_slot; const uint8_t *host_sample_buffer[8];
int16_t host_fx_knobs[3][8]; int16_t host_fx_meter[3]; void *host_fx_scratch[3]; volatile uint8_t op1_fx_page[3][4]; volatile uint8_t op1_eng_page[4];
void *host_find_node(void *s, const char *id) { (void)s; (void)id; return 0; }
void host_node_set_transform(void *n, const int32_t *m) { (void)n; (void)m; }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }
void *host_raster(void) { return 0; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c) { (void)r; (void)a; (void)b; (void)w; (void)c; }
extern const op1_engine_t ENGINE;
static int16_t sample[8 + 44100]; static int16_t peaks[0x813]; static struct { int16_t *d; int16_t p[0x813]; } sb;

static int16_t w7;
static double rms(int vel)
{
    for (int i = 0; i < (int)sizeof host_voice_mem; i++) host_voice_mem[i] = 0;
    host_voice_vel[0] = (int16_t)(vel << 8);
    int16_t k[8] = { 8000, 12000, 8000, 12000, 0x5301, 0, 0, w7 }, out[OP1_BLOCK];
    ENGINE.note_on(host_voice_mem, k);
    double e = 0;
    for (int b = 0; b < 2000; b++) { ENGINE.render(host_voice_mem, out, k); for (int i = 0; i < OP1_BLOCK; i++) e += (double)out[i] * out[i]; }
    return sqrt(e / (2000 * OP1_BLOCK));
}
int main(void)
{
    for (int i = 0; i < 8192; i++) host_sine[i] = (int16_t)lrint(32767.0 * sin(2 * M_PI * i / 8192.0));
    for (int i = 0; i < 128; i++) host_pitch_inc[i] = 25480370;                   /* C4 */
    for (int i = 0; i < 44100; i++) sample[8 + i] = (int16_t)(12000 * sin(i * 0.05));
    sb.d = sample; for (int i = 0; i < 0x813; i++) sb.p[i] = i < 160 ? 12000 : 0; (void)peaks;
    host_sample_buffer[0] = (const uint8_t *)&sb;
    double full = rms(127), half = rms(64);
    printf("rms vel127 %.0f, vel64 %.0f (ratio %.2f)\n", full, half, half / full);
    int ok = full > 100 && fabs(half / full - 64.0 / 127) < 0.03 && (host_voice_vel[0] = 0, op1_voice_vel_gain(host_voice_mem) == 32767);   /* no velocity: full level */
    w7 = 4096; double g = rms(127); w7 = 0;                                   /* word 7 = granular sample gain 0.5x */
    printf("word7 4096: ratio %.2f (granular: 0.5, others ignore it: 1.0)\n", g / full);
    ok = ok && (fabs(g / full - 0.5) < 0.05 || fabs(g / full - 1) < 0.01);
    printf(ok ? "OK\n" : "FAIL\n");
    return !ok;
}
