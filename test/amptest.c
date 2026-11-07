/* Peak / RMS of the engine output for typical notes: how many of the 144 PWM levels does music really use? */
#include <stdio.h>
#include <stdlib.h>
#include "opl_core.h"
static void w(unsigned r, unsigned v) { oplc_write_port(0, (u8)r); oplc_write_port(1, (u8)v); }
static void voice(unsigned c, unsigned tl, unsigned fm)
{
    static const unsigned op[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };
    unsigned m = op[c];
    w(0x20 + m, 0x21); w(0x40 + m, fm); w(0x60 + m, 0xF0); w(0x80 + m, 0x07);   /* modulator */
    w(0x23 + m, 0x21); w(0x43 + m, tl); w(0x63 + m, 0xF0); w(0x83 + m, 0x07);   /* carrier, sustained */
    w(0xC0 + c, 0x00); w(0xA0 + c, 0x57 + c * 0x10); w(0xB0 + c, 0x32);
}
static void meas(const char *name, unsigned nv, unsigned tl, unsigned fm)
{
    static s16 buf[1024];
    unsigned i, k, c; long pk = 0; unsigned long used[1] = {0};
    oplc_init(OPLC_OPL2, 8192);
    w(0x01, 0x20);
    for (c = 0; c < nv; c++) voice(c, tl, fm);
    oplc_generate_mono(buf, 1024);                                   /* skip attack */
    for (k = 0; k < 4; k++) {
        oplc_generate_mono(buf, 1024);
        for (i = 0; i < 1024; i++) { long a = buf[i] < 0 ? -(long)buf[i] : buf[i]; if (a > pk) pk = a; }
    }
    (void)used;
    printf("%-34s peak=%5ld (%2ld%% FS)  PWM levels spanned by that peak (p-p)=%3ld of 143\n", name, pk, pk * 100 / 32768, (pk * 2L * 143L) / 65536L);
}
int main(void)
{
    meas("1 voice, TL=0  (loudest)", 1, 0x00, 0x3F);
    meas("1 voice, TL=10h (typical melody)", 1, 0x10, 0x3F);
    meas("1 voice, TL=20h (soft)", 1, 0x20, 0x3F);
    meas("3 voices, TL=10h", 3, 0x10, 0x3F);
    meas("6 voices, TL=10h", 6, 0x10, 0x3F);
    meas("9 voices, TL=10h", 9, 0x10, 0x3F);
    meas("9 voices, TL=0", 9, 0x00, 0x3F);
    return 0;
}

