/* Host runner for the fixed-point Clouds port (plugins/clouds/clouds_dsp.c); same CLI as clouds_ref/clouds_ref.cc:
 *   clouds_port <g|l> in.raw out.raw pos size pitch density texture dry_wet feedback reverb frz_period frz_on
 * float CLI values are converted to the port's Q15 / semitone*256 parameters; 32-sample blocks. */
#include <stdio.h>
#include <stdlib.h>
#include "../plugins/clouds/clouds_dsp.h"

static int32_t q15(const char *s) { double v = atof(s) * 32768; if (v < 0) v = 0; if (v > 32767) v = 32767; return (int32_t)(v + 0.5); }   /* k/32768 exact */

int main(int argc, char **argv)
{
    if (argc != 14) { fprintf(stderr, "usage: see source\n"); return 1; }
    FILE *fi = fopen(argv[2], "rb"), *fo = fopen(argv[3], "wb");
    if (!fi || !fo) return 1;
    static uint8_t mem[CLOUDS_MEM_BYTES];
    static clouds_t c;
    clouds_init(&c, mem, argv[1][0] == 'l' ? CLOUDS_LOOPING : CLOUDS_GRANULAR);
    clouds_params_t p = { q15(argv[4]), q15(argv[5]), q15(argv[7]), q15(argv[8]), q15(argv[9]),
                          q15(argv[10]), q15(argv[11]), (int32_t)(atof(argv[6]) * 256), 0, 0 };
    int period = atoi(argv[12]), on = atoi(argv[13]);
    int16_t in[32], out[32];
    for (long block = 0; fread(in, 2, 32, fi) == 32; block++) {
        p.freeze = period > 0 && (block % period) >= on;
        clouds_process(&c, &p, in, out, 32);
        fwrite(out, 2, 32, fo);
    }
    fclose(fi); fclose(fo);
    return 0;
}
