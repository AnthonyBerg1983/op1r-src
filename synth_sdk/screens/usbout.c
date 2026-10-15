/* USB out page (clock screen + T2, docs/21), in the style of the synth playmode page: blue label "USB OUT", value
 * "MASTER" or "MASTER / + TRACKS". Blue turns between them; applies at once (USB stays 6 channels, the track channels
 * are silent and the track work is skipped with "MASTER"). Green row "MIDI SEQ" OFF / ON: the MIDI sequencer
 * device (seqdev/ms_fw.c, docs/39) is opt-in, off by default. Both saved in the DB table op1rsettings. */
#include "../op1_synth.h"
#include "stroke_font.h"

extern volatile uint8_t usb_tracks_live;
static uint8_t setting;                                    /* 0 master, 1 master + tracks */
volatile uint8_t ms_enabled;                               /* MIDI sequencer device on (read by seqdev/ms_fw.c) */

#ifdef OP1_HOST
int usbout_host_saved;
int usbout_host_seq_saved;
int usbout_load(void) { setting = (uint8_t)usbout_host_saved; ms_enabled = (uint8_t)usbout_host_seq_saved; return setting; }
static void usbout_save(void) { usbout_host_saved = setting; }
static void seq_save(void) { usbout_host_seq_saved = ms_enabled; }
#else
#define DB_HANDLE ((void *(*)(void))0x0196DD44)
#define DB_EXEC   ((void (*)(void *, const char *))0x0196F950)
#define DB_GET    ((void (*)(void *, void *, const char *))0x0196FBD0)
typedef struct { uint32_t alloc; union { char buf[16]; char *ptr; } u; uint32_t size, cap; uint32_t pad; } us_dstr;
static const char US_CREATE[] = "CREATE TABLE IF NOT EXISTS op1rsettings (key TEXT PRIMARY KEY, val INTEGER NOT NULL)";
/* at USB start (boot, after the DB is up; also when USB restarts after disk mode) */
int usbout_load(void)
{
    void *db = DB_HANDLE();
    DB_EXEC(db, US_CREATE);
    us_dstr s = { 0 };
    DB_GET(&s, db, "SELECT val FROM op1rsettings WHERE key='usbtracks'");
    const char *p = s.cap > 15 ? s.u.ptr : s.u.buf;           /* (heap copy for long strings, not freed: 1 char) */
    setting = (uint8_t)(s.size && p[0] == '1');
    us_dstr s2 = { 0 };
    DB_GET(&s2, db, "SELECT val FROM op1rsettings WHERE key='midiseq'");
    p = s2.cap > 15 ? s2.u.ptr : s2.u.buf;
    ms_enabled = (uint8_t)(s2.size && p[0] == '1');
    return setting;
}
static void usbout_save(void)                               /* KeyHandler thread, on change */
{
    void *db = DB_HANDLE();
    DB_EXEC(db, US_CREATE);
    DB_EXEC(db, setting ? "INSERT OR REPLACE INTO op1rsettings VALUES ('usbtracks',1)"
                        : "INSERT OR REPLACE INTO op1rsettings VALUES ('usbtracks',0)");
}
static void seq_save(void)
{
    void *db = DB_HANDLE();
    DB_EXEC(db, US_CREATE);
    DB_EXEC(db, ms_enabled ? "INSERT OR REPLACE INTO op1rsettings VALUES ('midiseq',1)"
                           : "INSERT OR REPLACE INTO op1rsettings VALUES ('midiseq',0)");
}
#endif

static int32_t acc;
void usbout_turn(int delta)                                 /* blue: one detent each way switches */
{
    if ((acc > 0 && delta < 0) || (acc < 0 && delta > 0)) acc = 0;
    acc += delta;
    int st = acc / 1024; if (!st) return;
    acc = 0;
    uint8_t v = st > 0;
    if (v == setting) return;
    setting = v;
    usb_tracks_live = v;                                    /* at once */
    usbout_save();
}
static int32_t acc_g;
void usbout_seq_turn(int delta)                             /* green: right on, left off */
{
    if ((acc_g > 0 && delta < 0) || (acc_g < 0 && delta > 0)) acc_g = 0;
    acc_g += delta;
    int st = acc_g / 1024; if (!st) return;
    acc_g = 0;
    uint8_t v = st > 0;
    if (v == ms_enabled) return;
    ms_enabled = v;                                         /* ms_fw.c stops / starts in its main-loop service */
    seq_save();
}

/* ---- screen: stroke-font text like the stock playmode page (label column, value column, 1.5 px lines) ---- */
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF };
#define US_S   OP1_Q10(5) / 2                               /* 2.5 px per grid unit: 15 px caps, 15 px advance */
#define US_W   0x600                                        /* 1.5 px */
#define US_LX  OP1_Q10(24)
#define US_VX  OP1_Q10(164)
#define US_Y0  OP1_Q10(40)
#define US_DY  OP1_Q10(28)

static void us_init(void *scr) { (void)scr; }

static void us_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    sf_text(r, US_LX, US_Y0, US_S, "USB OUT", US_W, c_blue);
    sf_text(r, US_VX, US_Y0, US_S, "MASTER", US_W, c_blue);
    if (setting) sf_text(r, US_VX, US_Y0 + US_DY, US_S, "+ TRACKS", US_W, c_blue);
    sf_text(r, US_LX, US_Y0 + 2 * US_DY, US_S, "MIDI SEQ", US_W, c_green);
    sf_text(r, US_VX, US_Y0 + 2 * US_DY, US_S, ms_enabled ? "ON" : "OFF", US_W, c_green);
}

const op1_screen_t SCREEN_usbout = { us_init, us_update };
