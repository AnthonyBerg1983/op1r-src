/* Host test for plugins/reverb/reverb.c: RT60 per time setting, stability with loud input at max settings, and
 * harness/out/reverb_*.wav to listen to (noise-burst drum loop through the reverb).
 *   cc -O1 -fsanitize=address,undefined -DOP1_HOST -I.. reverb_test.c -lm -o build/reverb_test && build/reverb_test */
#include "../plugins/reverb/reverb.c"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24];
int16_t host_knobs[8][8]; int host_slot; const uint8_t *host_sample_buffer[8];
int16_t host_fx_knobs[3][8]; int16_t host_fx_meter[3]; void *host_fx_scratch[3];
volatile uint8_t op1_fx_page[3][4];
void *host_find_node(void *s, const char *id) { (void)s; (void)id; return 0; }
void host_node_set_transform(void *n, const int32_t *m) { (void)n; (void)m; }
void host_node_set_visible(void *n, int on) { (void)n; (void)on; }
void host_screen_draw(void *s, int dt) { (void)s; (void)dt; }
void *host_raster(void) { return 0; }
void host_raster_line(void *r, const void *a, const void *b, int32_t w, const uint8_t *c) { (void)r; (void)a; (void)b; (void)w; (void)c; }
uint8_t *host_node_style_rgba(void *s, const char *id) { static uint8_t c[4]; (void)s; (void)id; return c; }

#define SR 44100
static int16_t out[SR * 12];

static void wav(const char *fn, const int16_t *s, int n)
{
    FILE *f = fopen(fn, "wb"); if (!f) return;
    uint32_t b = n * 2, h[] = { 0x46464952, 36 + b, 0x45564157, 0x20746d66, 16, 0x00010001, SR, SR * 2, 0x00100002, 0x61746164, b };
    fwrite(h, 4, 11, f); fwrite(s, 2, n, f); fclose(f);
}

/* run n samples; in(i) gives the input */
static void run(int16_t k[8], int n, int burst_len, int period, int amp)
{
    reverb_init(0);
    for (int b = 0; b < n / OP1_FX_BLOCK; b++) {
        int16_t *buf = out + b * OP1_FX_BLOCK;
        for (int i = 0; i < OP1_FX_BLOCK; i++) {
            int s = b * OP1_FX_BLOCK + i, ph = period ? s % period : s;
            buf[i] = ph < burst_len ? (int16_t)((int64_t)((rand() % (2 * amp + 1)) - amp) * (burst_len - ph) / burst_len) : 0;
        }
        reverb_render(0, buf, k);
    }
}

int main(void)
{
    static int32_t scratch[44115]; host_fx_scratch[0] = scratch;
    const int16_t times[] = { 0, 16384, 26000, 32767 };
    for (int j = 0; j < 4; j++) {
        int16_t k[8] = { times[j], 32767, 16384, 20000 };          /* fully wet */
        run(k, SR * 12, 441, 0, 20000);
        double e0 = 0; int t60 = -1;                               /* RT60 from 50 ms-window energy */
        for (int w = 0; w + 2205 <= SR * 12; w += 2205) {
            double e = 0; for (int i = 0; i < 2205; i++) e += (double)out[w + i] * out[w + i];
            if (w == 2205) e0 = e;
            if (w > 2205 && t60 < 0 && e < e0 * 1e-6) t60 = w;
            if (j == 2 && w % (SR / 2) == 0) printf("  %4.1f s %6.1f dB\n", w / (double)SR, 10 * log10(e / 2205 + 1e-9));
        }
        printf("time %5d: RT60 %s%.2f s (peak level %d)\n", times[j], t60 < 0 ? "> " : "", (t60 < 0 ? SR * 12 : t60) / (double)SR, host_fx_meter[0]);
    }
    int16_t hot[8] = { 32767, 32767, 32767, 32767 };              /* max everything, full-scale noise: bounded */
    run(hot, SR * 4, SR * 4, 0, 32767);
    for (int i = 0; i < SR * 4; i++) assert(out[i] >= -32768 && out[i] <= 32767);
    int16_t demo[8] = { 22000, 12000, 16384, 20000 };
    run(demo, SR * 8, 1500, SR / 2, 16000);
    wav("out/reverb_demo.wav", out, SR * 8);
    int16_t big[8] = { 30000, 20000, 24000, 12000 };
    run(big, SR * 8, 1500, SR * 2, 16000);
    wav("out/reverb_big_dark.wav", out, SR * 8);
    printf("ok -> out/reverb_demo.wav, out/reverb_big_dark.wav\n");
    return 0;
}
