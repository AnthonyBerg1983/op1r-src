/* Host test of tape/erase.c's chunk selection: cc -DOP1_HOST erase_test.c ../tape/erase.c -o build/erase_test */
#include <stdint.h>
#include <stdio.h>
typedef struct { int32_t ch, start, stop; } te_clip_t;
void te_chunk_mask(const te_clip_t *clips, const uint8_t *has, uint32_t mask[4][16]);
static int bit(uint32_t m[4][16], int t, int i) { return (m[t][i >> 5] >> (i & 31)) & 1; }
int main(void)
{
    static te_clip_t c[512]; static uint8_t has[512]; uint32_t m[4][16]; int ok = 1;
    c[0] = (te_clip_t){ 0, 0, 32768 };          has[0] = 1;   /* exactly chunk 0 of track 1 */
    c[3] = (te_clip_t){ 2, 32767, 32769 };      has[3] = 1;   /* straddles chunks 0/1 of track 3 */
    c[7] = (te_clip_t){ 3, 15870000, 15876000 }; has[7] = 1;  /* last chunk (484) of track 4, up to the tape end */
    c[9] = (te_clip_t){ 1, 100000, 200000 };    has[9] = 0;   /* not a clip: ignored */
    c[11] = (te_clip_t){ 5, 0, 100 };           has[11] = 1;  /* bad channel: ignored */
    te_chunk_mask(c, has, m);
    int n = 0; for (int t = 0; t < 4; t++) for (int i = 0; i < 485; i++) n += bit(m, t, i);
    ok &= bit(m, 0, 0) && !bit(m, 0, 1) && bit(m, 2, 0) && bit(m, 2, 1) && bit(m, 3, 484) && n == 4;
    printf("chunks to zero: %d (expected 4) of 1940 -> %s\n", n, ok ? "OK" : "FAIL");
    return !ok;
}
