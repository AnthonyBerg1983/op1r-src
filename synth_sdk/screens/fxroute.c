/* Master FX source (docs/54): Shift + mixer T2 page. Blue chooses what the master FX processes: "master" (the whole
 * mix, stock) or "instrument" (the live synth / drum bus only: out = mix + FX(inst) - inst). Saved in op1rsettings
 * (key 'mfxsrc').
 * The instrument's stereo part is not in a buffer of its own: tape L / R is copied right after tape_play_mix (the
 * LFO / sequencer tick 0x019C9D04, fr_snap) and subtracted from tape L / R at the metronome entry 0x019DE548
 * (fr_capture), after Core_mix_master has added the instrument. Exact unless the mix clipped there.
 * Output (green, instrument source only): "stereo master" (the FX's stereo result replaces the dry instrument in
 * the master, not recorded: stock) or "mono tape": the FX runs on the raw instrument bus (l = r = bus) and its mono
 * sum is written back into the bus, which tape_record_block records after Core_mix_master; the monitor hears it with
 * the track's volume / pan, estimated per block as gain = sum(inst * bus) / sum(bus^2) (one division per block). */
#include "../op1_synth.h"
#include "stroke_font.h"

uint8_t fr_src;                                             /* 0 master, 1 instrument */
uint8_t fr_out;                                             /* instrument: 0 stereo master, 1 mono tape */
static int16_t fr_snl[64], fr_snr[64], fr_il[64], fr_ir[64];
static volatile uint8_t fr_snapped, fr_have;

#ifdef OP1_HOST
int16_t fr_host_tape[2][64];
int16_t fr_host_bus[64];
#define TAPE_L fr_host_tape[0]
#define TAPE_R fr_host_tape[1]
#define BUS    fr_host_bus
#else
#define TAPE_L ((int16_t *)0xFF800D34)
#define TAPE_R ((int16_t *)0xFF800DB4)
#define BUS    ((int16_t *)0xFF800BB4)                       /* synth_mix_bus: recorded after Core_mix_master */
#endif

void fr_snap(void)                                          /* audio ISR, after the tape mix */
{
#ifndef OP1_HOST
    *(volatile uint8_t *)0x01DD591E = 0;                    /* the stock body of 0x019C9D04 */
#endif
    if (!fr_src) return;
    for (int i = 0; i < 64; i++) { fr_snl[i] = TAPE_L[i]; fr_snr[i] = TAPE_R[i]; }
    fr_snapped = 1;
}

void fr_capture(void)                                       /* audio ISR, after the instrument was mixed in */
{
    if (!fr_snapped) return;
    fr_snapped = 0;
    for (int i = 0; i < 64; i++) { fr_il[i] = (int16_t)(TAPE_L[i] - fr_snl[i]); fr_ir[i] = (int16_t)(TAPE_R[i] - fr_snr[i]); }
    fr_have = 1;
}

/* master FX call: run fx on the instrument part only when chosen and captured this block, else on l / r */
void fr_route(void (*fx)(void *, int16_t *, int16_t *, const int16_t *), void *self, int16_t *l, int16_t *r, const int16_t *params)
{
    if (!fr_src || !fr_have) { fx(self, l, r, params); return; }
    fr_have = 0;
    int16_t a[64], b[64];
    if (fr_out) {                                           /* mono to tape */
        int32_t sl = 0, sr = 0, sm = 0;
        for (int i = 0; i < 64; i++) {
            int32_t m = BUS[i]; a[i] = b[i] = (int16_t)m;
            sl += (fr_il[i] * m) >> 8; sr += (fr_ir[i] * m) >> 8; sm += (m * m) >> 8;
        }
        fx(self, a, b, params);
        static int32_t gl, gr;                              /* Q14 track gain of the bus in the mix: kept while the
                                                               bus is quiet (FX tail), smoothed (crackle) */
        if (sm > 2048) {
            while (sm >= 1 << 15) { sm >>= 1; sl >>= 1; sr >>= 1; }   /* sl * 16384 stays in 32 bits */
            sl = sl > 2 * sm ? 2 * sm : sl < -2 * sm ? -2 * sm : sl;
            sr = sr > 2 * sm ? 2 * sm : sr < -2 * sm ? -2 * sm : sr;
            gl += (sl * 16384 / sm - gl) >> 2; gr += (sr * 16384 / sm - gr) >> 2;
        }
        for (int i = 0; i < 64; i++) {
            int32_t w = (a[i] + b[i]) >> 1, d = w - BUS[i];
            BUS[i] = (int16_t)w;
            l[i] = op1_sat16(l[i] + ((d * gl) >> 14));
            r[i] = op1_sat16(r[i] + ((d * gr) >> 14));
        }
        return;
    }
    for (int i = 0; i < 64; i++) { a[i] = fr_il[i]; b[i] = fr_ir[i]; }
    fx(self, a, b, params);
    for (int i = 0; i < 64; i++) { l[i] = op1_sat16(l[i] + a[i] - fr_il[i]); r[i] = op1_sat16(r[i] + b[i] - fr_ir[i]); }
}

#ifdef OP1_HOST
int fr_host_saved = -1;
static void fr_load(void) { if (fr_host_saved >= 0) { fr_src = fr_host_saved & 1; fr_out = fr_host_saved >> 1; } }
static void fr_save(void) { fr_host_saved = fr_src + 2 * fr_out; }
#else
#define DB_HANDLE ((void *(*)(void))0x0196DD44)
#define DB_EXEC   ((void (*)(void *, const char *))0x0196F950)
#define DB_GET    ((void (*)(void *, void *, const char *))0x0196FBD0)
typedef struct { uint32_t alloc; union { char buf[16]; char *ptr; } u; uint32_t size, cap; uint32_t pad; } fr_dstr;
static const char FR_CREATE[] = "CREATE TABLE IF NOT EXISTS op1rsettings (key TEXT PRIMARY KEY, val INTEGER NOT NULL)";
static void fr_load(void)
{
    void *db = DB_HANDLE();
    DB_EXEC(db, FR_CREATE);
    fr_dstr s = { 0 };
    DB_GET(&s, db, "SELECT val FROM op1rsettings WHERE key='mfxsrc'");
    const char *p = s.cap > 15 ? s.u.ptr : s.u.buf;
    if (s.size && p[0] >= '0' && p[0] <= '3') { fr_src = (p[0] - '0') & 1; fr_out = (uint8_t)((p[0] - '0') >> 1); }   /* src + 2 out */
}
static void fr_save(void)
{
    static char q[] = "INSERT OR REPLACE INTO op1rsettings VALUES ('mfxsrc',0)";
    q[sizeof q - 3] = (char)('0' + fr_src + 2 * fr_out);
    void *db = DB_HANDLE();
    DB_EXEC(db, FR_CREATE);
    DB_EXEC(db, q);
}
#endif

static int32_t acc;
void fxroute_turn(int knob, int delta)                      /* blue: source, green: output (instrument only) */
{
    if (knob > 1 || (knob == 1 && !fr_src)) return;
    static int8_t last = -1;
    if (knob != last) { acc = 0; last = (int8_t)knob; }
    if ((acc > 0 && delta < 0) || (acc < 0 && delta > 0)) acc = 0;
    acc += delta;
    int st = acc / 1024; if (!st) return;
    acc = 0;
    uint8_t v = st > 0, *t = knob ? &fr_out : &fr_src;
    if (v == *t) return;
    *t = v; fr_have = fr_snapped = 0;
    fr_save();
}

static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF };
#define FR_S   OP1_Q10(5) / 2                               /* as the settings page */
#define FR_W   0x600
#define FR_LX  OP1_Q10(24)
#define FR_Y1  OP1_Q10(50)
#define FR_Y2  OP1_Q10(80)
#define FR_Y3  OP1_Q10(102)
#define FR_Y0  OP1_Q10(28)

static void fr_init(void *scr) { (void)scr; fr_load(); }
static void fr_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    sf_text(r, FR_LX, FR_Y0, FR_S, "FX SOURCE", FR_W, c_blue);
    sf_text(r, FR_LX, FR_Y1, FR_S, fr_src ? "INSTRUMENT" : "MASTER", FR_W, c_blue);
    if (!fr_src) return;                                    /* master source: stereo to master only */
    sf_text(r, FR_LX, FR_Y2, FR_S, "OUTPUT", FR_W, c_green);
    sf_text(r, FR_LX, FR_Y3, FR_S, fr_out ? "MONO TAPE" : "STEREO MASTER", FR_W, c_green);
}
const op1_screen_t SCREEN_fxroute = { fr_init, fr_update };
