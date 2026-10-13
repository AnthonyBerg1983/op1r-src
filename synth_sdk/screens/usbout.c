/* USB out page (clock screen + T2, docs/21), in the style of the synth playmode page: blue label "USB OUT", value
 * "MASTER" or "MASTER / + TRACKS". Blue turns between them; applies at once (USB stays 6 channels, the track channels
 * are silent and the track work is skipped with "MASTER"). Saved in the DB table op1rsettings. */
#include "../op1_synth.h"
#include "stroke_font.h"

extern volatile uint8_t usb_tracks_live;
static uint8_t setting;                                    /* 0 master, 1 master + tracks */

#ifdef OP1_HOST
int usbout_host_saved;
int usbout_load(void) { setting = (uint8_t)usbout_host_saved; return setting; }
static void usbout_save(void) { usbout_host_saved = setting; }
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
    return setting;
}
static void usbout_save(void)                               /* KeyHandler thread, on change */
{
    void *db = DB_HANDLE();
    DB_EXEC(db, US_CREATE);
    DB_EXEC(db, setting ? "INSERT OR REPLACE INTO op1rsettings VALUES ('usbtracks',1)"
                        : "INSERT OR REPLACE INTO op1rsettings VALUES ('usbtracks',0)");
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

/* ---- screen: stroke-font text like the stock playmode page (label column, value column, 1.5 px lines) ---- */
static const uint8_t c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF };
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
}

const op1_screen_t SCREEN_usbout = { us_init, us_update };
