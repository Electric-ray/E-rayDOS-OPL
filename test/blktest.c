/* Engine block-size independence: PCM generated in one call must equal PCM generated in small sub-blocks. */
#include <stdio.h>
#include <stdlib.h>
#include "opl_core.h"
static void w(unsigned r, unsigned v) { oplc_write_port(0, (u8)r); oplc_write_port(1, (u8)v); }
static void setup(void)
{
    static const unsigned op[3] = { 0x00, 0x01, 0x02 };
    unsigned c;
    oplc_init(OPLC_OPL2, 8192);
    w(0x01, 0x20); w(0xBD, 0xC0);                              /* tremolo + vibrato depth: exercises the LFO */
    for (c = 0; c < 3; c++) {
        unsigned m = op[c];
        w(0x20 + m, 0xE1); w(0x40 + m, 0x1C); w(0x60 + m, 0xF2); w(0x80 + m, 0x77);
        w(0x23 + m, 0xA1); w(0x43 + m, 0x08); w(0x63 + m, 0xC2); w(0x83 + m, 0x15);
        w(0xC0 + c, 0x01); w(0xA0 + c, 0x57 + c * 0x20); w(0xB0 + c, 0x32);
    }
}
static unsigned long run(unsigned blk, unsigned total)
{
    static s16 buf[4096];
    unsigned long h = 2166136261UL; unsigned i, done = 0;
    setup();
    while (done < total) { unsigned n = total - done; if (n > blk) n = blk; oplc_generate_mono(buf, n); for (i = 0; i < n; i++) h = (h * 16777619UL) ^ (unsigned)buf[i]; done += n; }
    return h;
}
int main(void)
{
    unsigned total = 4096;
    unsigned long a = run(4096, total), b = run(8, total), c = run(4, total), d = run(32, total);
    printf("whole=%08lX blk8=%08lX blk4=%08lX blk32=%08lX  %s\n", a, b, c, d, (a == b && a == c && a == d) ? "IDENTICAL" : "DIFFERENT");
    return 0;
}
