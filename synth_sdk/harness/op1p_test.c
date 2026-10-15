/* Host test of plugload/op1p_load.c: every build/plugins/<name>.op1p (make_plugin.py --all) relocated to CHECK_BASE must
 * equal the image the linker made at that base (<name>.ref); corrupted files are refused.
 *   cc -I.. -I../plugload op1p_test.c ../plugload/op1p_load.c -o build/op1p_test && build/op1p_test ../build/plugins/<all .op1p> */
#include "op1p_load.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK_BASE 0x0041A3C0u
#define IMP_FAKE   0x0E000000u

static uint8_t *slurp(const char *p, uint32_t *n)
{
    FILE *f = fopen(p, "rb"); assert(f);
    fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = aligned_alloc(4, (*n + 3) & ~3u); assert(fread(b, 1, *n, f) == *n); fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    for (int a = 1; a < argc; a++) {
        uint32_t n, rn; op1p_view v;
        uint8_t *f = slurp(argv[a], &n);
        assert(op1p_parse(f, n, &v) == 0);
        char ref[512]; snprintf(ref, sizeof ref, "%.*s.ref", (int)(strlen(argv[a]) - 5), argv[a]);
        uint8_t *r = slurp(ref, &rn);
        assert(rn == v.mem_size);
        uint32_t imp[OP1P_MAX_IMPORTS];          /* make_plugin's fake addresses: index order */
        for (uint32_t i = 0; i < v.hdr->n_imports; i++) imp[i] = IMP_FAKE + 0x100 * i;
        uint8_t *m = calloc(1, v.mem_size + 4);
        op1p_relocate(&v, m, CHECK_BASE, imp);
        assert(memcmp(m, r, v.mem_size) == 0);
        /* corruptions: every one must be refused */
        uint8_t *c = aligned_alloc(4, (n + 3) & ~3u);
        memcpy(c, f, n); c[n - 20] ^= 1;                       assert(op1p_parse(c, n, &v) == OP1P_E_CRC);
        memcpy(c, f, n); c[n - 16] ^= 1;                       assert(op1p_parse(c, n, &v) == OP1P_E_MAGIC);
        assert(op1p_parse(f, n - 1, &v) != 0);                 /* truncated */
        memcpy(c, f, n); memset(c + n - 12, 0xFF, 8);          assert(op1p_parse(c, n, &v) == OP1P_E_SIZE);
        printf("%s: ok (%u B, svg %u, mem %u, %u relocs, %u imports)\n", argv[a], n, v.svg_len, v.mem_size,
               v.hdr->n_relocs, v.hdr->n_imports);
        free(f); free(r); free(m); free(c);
    }
    return 0;
}
