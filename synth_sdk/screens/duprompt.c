/* Dev update prompt (devupd, docs/35): shown over the current screen once a SysEx push is complete (COMMIT):
 * "UPDATE READY", files / size, T1 restart (blue), T2 cancel (green); after T1 "RESTARTING". Stroke font like the
 * USB out page. State is set by devupd/du_fw.c. */
#include "../op1_synth.h"
#include "stroke_font.h"

int dp_files, dp_kb, dp_restarting, dp_fw;                      /* set by du_fw.c; dp_fw: OP1_vdk.ldr staged */

static const uint8_t c_white[4] = { 0xFF, 0xFF, 0xFF, 0xFF }, c_grey[4] = { 0xAE, 0xB1, 0xDC, 0xFF },
                     c_blue[4] = { 0x4D, 0x9E, 0xFF, 0xFF }, c_green[4] = { 0x00, 0xED, 0x95, 0xFF },
                     c_red[4] = { 0xFF, 0x3A, 0x5D, 0xFF };
#define DP_S  OP1_Q10(5) / 2                                    /* 15 px caps, as the USB out page */
#define DP_W  0x600
#define DP_X  OP1_Q10(24)

static void num(char *o, int v) { char t[12]; int n = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) *o++ = t[--n]; *o = 0; }

static void dp_init(void *scr) { (void)scr; }

static void dp_update(void *scr, int dt)
{
    op1_screen_draw(scr, dt);
    void *r = op1_raster();
    if (dp_restarting) { sf_text(r, DP_X, OP1_Q10(58), DP_S, "RESTARTING", DP_W, c_white); return; }
    char s[40], *p = s;
    num(p, dp_files); while (*p) p++;
    const char *a = dp_files == 1 ? " FILE  " : " FILES  "; while (*a) *p++ = *a++;
    num(p, dp_kb); while (*p) p++;
    *p++ = ' '; *p++ = 'K'; *p++ = 'B'; *p = 0;
    sf_text(r, DP_X, OP1_Q10(24), DP_S, "UPDATE READY", DP_W, c_white);
    sf_text(r, DP_X, OP1_Q10(48), DP_S, s, DP_W, c_grey);
    if (dp_fw) sf_text(r, DP_X, OP1_Q10(72), DP_S, "NEW FIRMWARE", DP_W, c_red);   /* the LDR replaces the main firmware */
    sf_text(r, DP_X, OP1_Q10(100), DP_S, "T1 RESTART", DP_W, c_blue);
    sf_text(r, DP_X, OP1_Q10(124), DP_S, "T2 CANCEL", DP_W, c_green);
}

const op1_screen_t SCREEN_duprompt = { dp_init, dp_update };
