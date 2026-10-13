/* Host test of shelved/paulstretch/paulstretch.c ("paul"): fake sampler recording, two notes, a third note steals the older
 * stretch voice, voices without a stretch voice stay silent, knob changes while sounding, nothing recorded = silence.
 *   cc -DOP1_HOST -I.. paulstretch_engine_test.c ../shelved/paulstretch/paulstretch.c ../shelved/paulstretch/paulstretch_dsp.c -lm */
#include "../op1_synth.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int16_t host_sine[8192]; int32_t host_pitch_inc[128]; uint8_t host_voice_mem[6 * 0x24]; int16_t host_voice_vel[6];
int16_t host_knobs[8][8]; int host_slot; const uint8_t *host_sample_buffer[8];
extern const op1_engine_t ENGINE_paulstretch;

/* op1_host.h: data = *(int16_t **)sb + 8 (guard), peaks at sb + sizeof(void *) */
static struct { int16_t *data; int16_t peaks[OP1_PEAK_BUCKETS]; } sb;
static int16_t rec[OP1_SAMPLE_GUARD + 88200];

static double play(int voice, const int16_t *k, int blocks, double hz)
{
    static int16_t out[OP1_BLOCK]; double acc = 0; int peak = 0;
    for (int i = 0; i < 128; i++) host_pitch_inc[i] = (int32_t)(hz / 44100 * 4294967296.0);
    for (int b = 0; b < blocks; b++) {
        ENGINE_paulstretch.render(OP1_VOICE(voice), out, k);
        for (int i = 0; i < OP1_BLOCK; i++) { acc += (double)out[i] * out[i]; if (abs(out[i]) > peak) peak = abs(out[i]); }
    }
    assert(peak < 32767);                                           /* no clipping on a -6 dB source */
    return sqrt(acc / (blocks * OP1_BLOCK)) / 32768;
}

int main(void)
{
    const int16_t k[8] = { 8000, 12000, 32767, 0, 0, 0, 0, 0 };     /* pos 25 %, stretch ~16x, N 8192, no smear */
    extern void paul_boot(void);
    host_sample_buffer[0] = (const uint8_t *)&sb;
    paul_boot();
    sb.data = rec;
    assert(play(0, k, 4, 261.63) == 0.0);                           /* no stretch voice yet: silent */
    ENGINE_paulstretch.note_on(OP1_VOICE(0), k);
    assert(play(0, k, 100, 261.63) == 0.0);                         /* nothing recorded: silent */

    for (int i = 0; i < 88200; i++)                                 /* 2 s: chord + slow sweep, -6 dB */
        rec[OP1_SAMPLE_GUARD + i] = (int16_t)(8000 * sin(i * 2 * M_PI * 220 / 44100) + 6000 * sin(i * 2 * M_PI * 330 / 44100)
                                              + 2000 * sin(i * 2 * M_PI * (500 + i * 0.01) / 44100));
    for (int i = 0; i < 88200 / 256; i++) sb.peaks[i] = 16000;
    ENGINE_paulstretch.note_on(OP1_VOICE(0), k);
    double a0 = play(0, k, 32, 261.63);                             /* first hop (2048 / 128 = 32 blocks) = latency */
    double a1 = play(0, k, 200, 261.63);
    printf("voice 0: first hop rms %.4f, then %.4f\n", a0, a1);
    assert(a0 == 0.0 && a1 > 0.05 && a1 < 0.6);

    ENGINE_paulstretch.note_on(OP1_VOICE(3), k);                    /* second stretch voice */
    double b1 = play(3, k, 200, 392.0), a2 = play(0, k, 50, 261.63);
    printf("voice 3 (G4): %.4f, voice 0 still %.4f\n", b1, a2);
    assert(b1 > 0.05 && a2 > 0.05);
    ENGINE_paulstretch.note_on(OP1_VOICE(5), k);                    /* third note steals voice 0's stretch voice */
    assert(play(0, k, 10, 261.63) == 0.0);
    assert(play(5, k, 200, 261.63) > 0.05 && play(3, k, 10, 392.0) > 0.05);
    assert(play(1, k, 10, 261.63) == 0.0);                          /* never got one */

    int16_t k2[8]; memcpy(k2, k, sizeof k2);                        /* knobs while sounding: jump, max stretch, smear */
    k2[0] = 30000; k2[1] = 32767; k2[3] = 32767;
    double c = play(5, k2, 300, 130.8);
    printf("voice 5 after knob changes, C3: %.4f\n", c);
    assert(c > 0.02);
    k2[2] = 0;                                                      /* window 2048 at the next note */
    ENGINE_paulstretch.note_on(OP1_VOICE(5), k2);
    assert(play(5, k2, 8, 261.63) == 0.0 && play(5, k2, 100, 261.63) > 0.02);
    printf("ok\n");
    return 0;
}
