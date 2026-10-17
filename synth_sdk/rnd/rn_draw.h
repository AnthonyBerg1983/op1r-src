/* Synth randomiser panel (rn_draw.c): RANDOM / MODNAR over the current synth page */
#ifndef RN_DRAW_H
#define RN_DRAW_H
#define RN_PANEL_MS 1500
void rn_draw_panel(void *r, int back);      /* GUI thread; back: MODNAR */
#endif
