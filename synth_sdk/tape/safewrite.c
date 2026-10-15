/* Tape clip save through a bounce buffer (docs/42). tape_c::service 0x01A54710 saves a dirty clip with
 * yaffs_write(fd, live clip data, 64 KB) while the audio interrupt may still record into it. YAFFS writes full chunks
 * straight from the caller's buffer and, on the first chunk of a fresh block, compares the read-back with that buffer;
 * a change in between fails the compare and retires a good block (shown as "bad" in FILE). Here every piece is copied
 * first, so the flash always matches what YAFFS compares. A copy taken mid-recording is fine: recording marks the
 * clip dirty again and it is saved again. Replaces the CALL yaffs_write at 0x01A54842. */
#include <stdint.h>

#define SW_PIECE 4096                            /* whole YAFFS chunks (2 KB pages), aligned like the 64 KB write */

#ifdef OP1_HOST
extern int host_yaffs_write(int fd, const void *buf, unsigned n);
#define Y_WRITE host_yaffs_write
#else
#define Y_WRITE ((int (*)(int, const void *, unsigned))0x01A99F84)
#endif

static uint32_t sw_buf[SW_PIECE / 4];

int tape_safe_write(int fd, const void *src, unsigned n)
{
    const uint32_t *s = (const uint32_t *)src;       /* clip data: int16 pairs */
    unsigned done = 0;
    while (done < n) {
        unsigned k = n - done < SW_PIECE ? n - done : SW_PIECE;
        if (((uintptr_t)src & 3) == 0)
            for (unsigned i = 0; i < (k + 3) / 4; i++) sw_buf[i] = s[(done >> 2) + i];
        else                                         /* (not seen: misaligned loads would fault on Blackfin) */
            for (unsigned i = 0; i < k; i++) ((uint8_t *)sw_buf)[i] = ((const uint8_t *)src)[done + i];
        int r = Y_WRITE(fd, sw_buf, k);
        if (r < 0) return done ? (int)done : r;
        done += (unsigned)r;
        if ((unsigned)r < k) break;
    }
    return (int)done;
}
