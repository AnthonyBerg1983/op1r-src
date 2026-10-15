/* MIDI automation of the current synth slot (mx.c, docs/41) */
#ifndef MX_H
#define MX_H
#include <stdint.h>
#define MX_KEY 0x3F                     /* private key code: "apply pending automation" in the KeyHandler thread */
int mx_index(int cc);
int mx_scale(const int16_t *rg, int v);
int mx_cc(int cc, int v);               /* any thread; 1 if the CC is mapped */
void mx_apply(void);                    /* KeyHandler thread */
#endif
