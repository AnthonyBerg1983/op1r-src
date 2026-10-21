/* Master FX source (docs/54): Shift + mixer T2 page. Blue chooses what the master FX processes: "master" (the whole
 * mix, stock) or "instrument" (the live synth / drum bus only: out = mix + FX(inst) - inst). Saved in op1rsettings
 * (key 'mfxsrc').
 * The instrument's stereo part is not in a buffer of its own: tape L / R is copied right after tape_play_mix (the
 * LFO / sequencer tick 0x019C9D04, fr_snap) and subtracted from tape L / R at the metronome entry 0x019DE548
 * (fr_capture), after Core_mix_master has added the instrument. Exact unless the mix clipped there. */
#include "../op1_synth.h"
#include "stroke_font.h"

uint8_t fr_src;                                             /* 0 master, 1 instrument */
static int16_t fr_snl[64], fr_snr[64], fr_il[64], fr_ir[64];
static volatile uint8_t fr_snapped, fr_have;

#ifdef OP1_HOST
int16_t fr_host_tape[2][64];
#define TAPE_L fr_host_tape[0]
#define TAPE_R fr_host_tape[1]
#else
#define TAPE_L ((int16_t *)0xFF800D34)
#define TAPE_R ((int16_t *)0xFF800DB4)
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
    for (int i = 0; i < 64; i++) { a[i] = fr_il[i]; b[i] = fr_ir[i]; }
    fx(self, a, b, params);
    for (int i = 0; i < 64; i++) { l[i] = op1_sat16(l[i] + a[i] - fr_il[i]); r[i] = op1_sat16(r[i] + b[i] - fr_ir[i]); }
}

#ifdef OP1_HOST
int fr_host_saved = -1;
static void fr_load(void) { if (fr_host_saved >= 0) fr_src = (uint8_t)fr_host_saved; }
static void fr_save(void) { fr_host_saved = fr_src; }
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
    if (s.size && (p[0] == '0' || p[0] == '1')) fr_src = (uint8_t)(p[0] - '0');
}
static void fr_save(void)
{
    static char q[] = "INSERT OR REPLACE INTO op1rsettings VALUES ('mfxsrc',0)";
    q[sizeof q - 3] = (char)('0' + fr_src);
    void *db = DB_HANDLE();
    DB_EXEC(db, FR_CREATE);
    DB_EXEC(db, q);
}
#endif

static int32_t acc;
void fxroute_turn(int knob, int delta)                      /* blue: one value per detent */
{
    if (knob != 0) return;
    if ((acc > 0 && delta < 0) || (acc < 0 && delta > 0)) acc = 0;
    acc += delta;
    int st = acc / 1024; if (!st) return;
    acc = 0;
    uint8_t v = st > 0;
    if (v == fr_src) return;
    fr_src = v; fr_have = fr_snapped = 0;
    fr_save();
}

static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF };
#define FR_S   OP1_Q10(5) / 2                               /* as the settings page */
#define FR_W   0x600
#define FR_LX  OP1_Q10(24)
#define FR_Y1  OP1_Q10(70)
#define FR_Y0  OP1_Q10(40)

static void fr_init(void *scr) { (void)scr; fr_load(); }
static void fr_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    sf_text(r, FR_LX, FR_Y0, FR_S, "FX SOURCE", FR_W, c_blue);
    sf_text(r, FR_LX, FR_Y1, FR_S, fr_src ? "INSTRUMENT" : "MASTER", FR_W, c_blue);
}
const op1_screen_t SCREEN_fxroute = { fr_init, fr_update };
