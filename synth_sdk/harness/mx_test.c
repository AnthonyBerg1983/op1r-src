/* Host test for midi/mx.c: CC map and range scaling.   cc -DOP1_HOST -I.. mx_test.c ../midi/mx.c */
#include <assert.h>
#include <stdio.h>
#include "../midi/mx.h"
int main(void)
{
    assert(mx_index(52) == -1 && mx_index(53) == 0 && mx_index(56) == 3 && mx_index(57) == 4 && mx_index(60) == 7);
    assert(mx_index(61) == -1 && mx_index(71) == -1 && mx_index(72) == 8 && mx_index(79) == 15 && mx_index(80) == -1);
    assert(mx_index(1) == -1 && mx_index(102) == -1);       /* MIDI LFO / encoders untouched */
    const int16_t full[3] = { 0, 32767, 0 }, sym[3] = { -16384, 16384, 0 }, empty[3] = { 0, 0, 0 };
    assert(mx_scale(full, 0) == 0 && mx_scale(full, 127) == 32767 && mx_scale(full, 64) == 16512);
    assert(mx_scale(sym, 0) == -16384 && mx_scale(sym, 127) == 16384);
    assert(mx_scale(empty, 100) == 0);                       /* unfilled table: min, no write past it */
    for (int v = 0; v < 128; v++) { int x = mx_scale(sym, v); assert(x >= -16384 && x <= 16384); }
    printf("mx_test: ok\n");
    return 0;
}
