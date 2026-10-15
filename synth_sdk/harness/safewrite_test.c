/* Host test for tape/safewrite.c: same bytes, pieces of whole chunks, never the caller's buffer.
 *   cc -DOP1_HOST safewrite_test.c ../tape/safewrite.c */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
int tape_safe_write(int fd, const void *src, unsigned n);
static uint8_t out[65536]; static unsigned pos, calls; static const void *caller;
int host_yaffs_write(int fd, const void *buf, unsigned n)
{
    assert(fd == 7 && buf != caller && n <= 4096 && n % 2048 == 0);
    memcpy(out + pos, buf, n); pos += n; calls++;
    return (int)n;
}
int main(void)
{
    static uint32_t clip[16384];
    for (int i = 0; i < 16384; i++) clip[i] = (uint32_t)i * 2654435761u;
    caller = clip;
    assert(tape_safe_write(7, clip, 65536) == 65536 && pos == 65536 && calls == 16);
    assert(!memcmp(out, clip, 65536));
    printf("safewrite_test: ok\n");
    return 0;
}
