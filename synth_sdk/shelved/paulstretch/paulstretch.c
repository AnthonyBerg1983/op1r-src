/* Paulstretch engine over the slot's sampler recording (engines/paulstretch_dsp.c, docs/19).
 *
 * Workflow like granular: record with "sampler" on a synth slot, then switch the slot to "paulstretch".
 *   knob 0 (blue)  : position  - where the stretch starts in the recording (at note-on; turning it while a note
 *                                sounds jumps there)
 *   knob 1 (green) : stretch   - 1x .. 512x (exponential)
 *   knob 2 (white) : window    - 2048 / 4096 / 8192 samples (46 / 93 / 186 ms; smoother and slower with size),
 *                                applied at note-on
 *   knob 3 (red)   : smear     - random frame start offset 0 .. ~0.75 s (blurs further)
 *   keyboard       : pitch (C4 = original), spectral: the magnitude bins move (no aliasing), 3 octaves down / up
 * Two stretch voices (the last two notes); other voices are silent. Latency: one hop (N/2) after note-on.
 * Graphics: the stock iter screen (no own screen).
 */
#include "op1_synth.h"
#include "paulstretch_dsp.h"

#define PS_VOICES  2
#define LOG2N_MAX  13
#define INC_C4     25480370u                 /* phase inc of 261.63 Hz @44.1 kHz (2^32 = 1 cycle/sample) */

/* State and buffers live in the firmware heap (0x00000000-0x00FFFFFF: cacheable, DIRTY set). Our own code/bss page
 * 0x01C00000 is write-back without DIRTY: every store there traps to the CPLB handler (docs/19), ~10x slower. */
static ps_t *ps;                                /* PS_VOICES, allocated by paul_boot() */
static uint32_t *ps_mem[PS_VOICES];
static int8_t owner[PS_VOICES] = { -1, -1 };  /* OP-1 voice index using each stretch voice */
static uint8_t next_inst;                       /* round robin: the older one is replaced */
static int16_t last_pos[PS_VOICES], last_st[PS_VOICES], last_smear[PS_VOICES];

static int32_t recorded_length(const uint8_t *sb)   /* end of the last peak bucket above a small threshold */
{
    const int16_t *pk = op1_sample_peaks(sb);
    for (int i = OP1_PEAK_BUCKETS - 1; i >= 0; i--)
        if (pk[i] > 96) {
            int32_t l = (i + 1) * 256;
            return l > OP1_SAMPLE_MAX ? OP1_SAMPLE_MAX : l;
        }
    return 0;
}

static uint32_t stretch_q8(int32_t k)                /* 0..32767 -> 256 .. 256 * 2^9, exponential */
{
    uint32_t e = (uint32_t)k * 9u;                   /* octaves, Q15 */
    uint32_t i = e >> 15, f = e & 0x7FFF;
    uint32_t base = 256u << i;
    return base + ((base * f) >> 15);                /* linear between octaves */
}

#ifndef OP1_HOST
#define FW_MALLOC ((void *(*)(uint32_t))0x01A9F348)   /* heap 0 */
#else
#include <stdlib.h>
#define FW_MALLOC(n) malloc(n)
#endif
void paul_boot(void)                            /* boot (main thread), from dispatcher.c screens_load_hook */
{
    if (ps) return;
    ps_t *p = FW_MALLOC(PS_VOICES * sizeof(ps_t));
    for (int i = 0; i < PS_VOICES; i++) {
        ps_mem[i] = FW_MALLOC(PS_MEM_BYTES(LOG2N_MAX));
        if (!ps_mem[i]) return;                 /* engine stays silent */
    }
    ps = p;
}

static int inst_of(const void *self)
{
    int v = op1_voice_index(self);
    for (int i = 0; i < PS_VOICES; i++) if (owner[i] == v) return i;
    return -1;
}

static void apply_pos(ps_t *p, int16_t k)
{
    p->pos = p->src_len > 0 ? (int32_t)(((uint32_t)(uint16_t)k * (uint32_t)p->src_len) >> 15) : 0;
    if (p->pos >= p->src_len) p->pos = 0;
    p->pos_frac = 0;
}

static void pst_note_on(void *self, const int16_t *params)
{
    int v = op1_voice_index(self), i = inst_of(self);
    if (!ps) return;
    if (i < 0) { i = next_inst; next_inst ^= 1; owner[i] = (int8_t)v; }
    const uint8_t *sb = op1_sample_buffer(op1_current_slot());
    int log2n = 11 + (params[2] >= 10923) + (params[2] >= 21846);
    ps_t *p = &ps[i];
    ps_init(p, ps_mem[i], log2n, (uint32_t)v * 2654435761u + 977u);
    int32_t len = sb ? recorded_length(sb) : 0;
    ps_set_source(p, sb && len ? op1_sample_data(sb) : 0, len, 0);
    apply_pos(p, params[0]);
    ps_set_stretch(p, stretch_q8(params[1]));
    p->jitter = (int32_t)(((uint32_t)params[3] * 33075u) >> 15);
    last_pos[i] = params[0]; last_st[i] = params[1]; last_smear[i] = params[3];
}

static void pst_render(void *self, int16_t *out, const int16_t *params)
{
    int i = ps ? inst_of(self) : -1;
    ps_t *p = i >= 0 ? &ps[i] : 0;
    if (!p || p->src_len < 4096) {                     /* no stretch voice / nothing recorded */
        for (int n = 0; n < OP1_BLOCK; n++) out[n] = 0;
        return;
    }
    if (params[1] != last_st[i]) { last_st[i] = params[1]; ps_set_stretch(p, stretch_q8(params[1])); }
    if (params[3] != last_smear[i]) { last_smear[i] = params[3]; p->jitter = (int32_t)(((uint32_t)params[3] * 33075u) >> 15); }
    int32_t d = params[0] - last_pos[i];
    if (d > 256 || d < -256) { last_pos[i] = params[0]; apply_pos(p, params[0]); }   /* knob moved: jump */
    uint32_t inc = (uint32_t)OP1_PITCH_INC[0];                                       /* pitch: bins move */
    ps_set_pitch_inv(p, inc ? (uint32_t)(((uint64_t)INC_C4 << 16) / inc) : 65536);  /* one division per block */
    ps_render(p, out, OP1_BLOCK);
    int32_t g = op1_voice_vel_gain(self);
    for (int n = 0; n < OP1_BLOCK; n++) out[n] = (int16_t)((out[n] * g) >> 15);
}

const op1_engine_t ENGINE_paulstretch = { pst_note_on, pst_render, 0, { 0, 0, 0, 0 } };
