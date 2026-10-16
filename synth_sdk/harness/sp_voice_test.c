/* sampler+: the stock voice model against an emulator dump (docs/46): OP1EMU_BREAK=1a51a7c+ffa00d2a with
 * r0:10, r2:10, ff801950:200, ff802100:900, 3040028:81400 (buffer), ff80113c:100 (out) per hit.
 *   cc -O1 -I.. sp_voice_test.c ../samplerplus/sp_voice.c && ./a.out DIR RATE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../samplerplus/sp_voice.h"
static int rd(const char *dir, int hit, const char *n, void *b, size_t len)
{
    char p[512]; snprintf(p, sizeof p, "%s/hit%d_%s.bin", dir, hit, n);
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    size_t r = fread(b, 1, len, f); fclose(f); return r == len;
}
int main(int argc, char **argv)
{
    const char *dir = argv[1]; int32_t rate = (int32_t)strtol(argv[2], 0, 0);
    static int16_t data[0x81400 / 2], tab[0x900 / 2];
    int blocks = 0, bad = 0;
    for (int h = 0; ; h += 2) {
        uint8_t self[16]; int16_t k[8], out[128], ref[128]; int32_t inc[128];
        if (!rd(dir, h, "r0", self, 16) || !rd(dir, h + 1, "ff80113c", ref, 256)) break;
        rd(dir, h, "r2", k, 16); rd(dir, h, "ff801950", inc, 512); rd(dir, h, "ff802100", tab, 0x900);
        rd(dir, h, "3040028", data, 0x81400);
        sp_voice v; memcpy(&v.pos, self + 8, 4); v.released = self[12]; v.first = self[13];
        sp_stock_render(&v, out, k, inc, rate, data, tab + (0xFF8021D2 - 0xFF802100) / 2);
        int d = memcmp(out, ref, sizeof out) != 0;
        uint8_t nx[16];
        if (rd(dir, h + 2, "r0", nx, 16)) { int32_t np; memcpy(&np, nx + 8, 4); if (np != v.pos) d = 1; }
        if (d) { bad++; printf("block %d differs (out[0] %d / %d)\n", h / 2, out[0], ref[0]); }
        blocks++;
    }
    printf("%s: %d blocks, %d differ\n", bad ? "FAIL" : "OK", blocks, bad);
    return bad != 0 || !blocks;
}
