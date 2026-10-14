/* drum+ stock voice model vs the firmware: an emulator dump of one held drum hit (docs/31, phase 1).
 *   cc -O1 -I.. dp_voice_test.c ../drumplus/dp_voice.c -o build/dp_voice_test && build/dp_voice_test [dir]
 * Needs (local only, firmware-derived, never committed): dir/dv_l1.bin (L1 data 0xFF800000, 32 KB), dir/dv_scr.bin
 * (scratchpad 0xFFB00000), dir/dv_drum0.raw (the slot's sample), dir/dv_pad.txt (the voice's 8 pad words); default
 * dir /tmp. Skips (exit 0) if missing. Full replay: note-on, then every block the voice has played (increment and
 * velocity are constant while a pad is held), compared with the firmware's last block, position and state: covers
 * the start bound, reverse and loop wraps. */
#include "../drumplus/dp_voice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *load(const char *dir, const char *name, long *len)
{
    char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(*len + 64);
    if (fread(b, 1, *len, f) != (size_t)*len) { fclose(f); free(b); return 0; }
    fclose(f); return b;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "/tmp";
    long nl1 = 0, nscr = 0, nraw = 0, npad = 0;
    uint8_t *l1 = load(dir, "dv_l1.bin", &nl1), *scr = load(dir, "dv_scr.bin", &nscr);
    int16_t *raw = load(dir, "dv_drum0.raw", &nraw);
    char *padt = load(dir, "dv_pad.txt", &npad);
    int rc = 0;
    if (!l1 || !scr || !raw || !padt) { printf("SKIP: no emulator dump in %s\n", dir); goto done; }
    padt[npad] = 0;
    int w[8];
    if (sscanf(padt, "%d %d %d %d %d %d %d %d", &w[0], &w[1], &w[2], &w[3], &w[4], &w[5], &w[6], &w[7]) != 8) { rc = 2; goto done; }
    dp_pad pad = { w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7] };
    int16_t *data = calloc(nraw / 2 + 8 + 64, 2);
    memcpy(data + 8, raw, nraw);                                  /* 8 guard samples before the audio */
    const int16_t *fir = (const int16_t *)(l1 + 0x21D2);
    int32_t inc[DP_BLOCK]; int16_t vel[DP_BLOCK], ref[DP_BLOCK], out[DP_BLOCK];
    memcpy(inc, l1 + 0x1950, sizeof inc); memcpy(vel, l1 + 0x1D50, sizeof vel); memcpy(ref, l1 + 0x113C, sizeof ref);
    int32_t pos_after, state; uint32_t blocks;
    memcpy(&pos_after, scr + 8, 4); memcpy(&state, scr + 12, 4); memcpy(&blocks, scr + 16, 4);
    int constant = 1;
    for (int n = 1; n < DP_BLOCK; n++) constant &= inc[n] == inc[0] && vel[n] == vel[0];
    if (!constant || !blocks) { printf("SKIP: increment / velocity not constant in the dump\n"); free(data); goto done; }
    dp_voice v;
    dp_stock_note_on(&v, &pad);
    for (uint32_t b = 0; b < blocks; b++) dp_stock_render(&v, out, &pad, inc, vel, data, fir);
    int diff = 0;
    for (int n = 0; n < DP_BLOCK; n++) diff += out[n] != ref[n];
    printf("pad playmode %d reverse %d, %u blocks replayed: %d of %d samples differ, position %s, state %s\n",
           pad.playmode, pad.reverse, blocks, diff, DP_BLOCK, v.pos == pos_after ? "exact" : "WRONG",
           v.state == state ? "exact" : "WRONG");
    int ok = !diff && v.pos == pos_after && v.state == state;
    printf(ok ? "OK: stock drum voice model is bit-exact\n" : "FAIL\n");
    rc = !ok;
    free(data);
done:
    free(l1); free(scr); free(raw); free(padt);
    return rc;
}
