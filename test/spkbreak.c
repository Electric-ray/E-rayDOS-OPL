/* Simulates a game that drives the PC speaker itself: clears the speaker gate and reprograms PIT ch2 (mode 3), then waits ~1 s. */
#include <conio.h>
#include <dos.h>
int main(void)
{
    volatile unsigned long far *t = (volatile unsigned long far *)MK_FP(0x40, 0x6C);
    unsigned long t0;
    int k;
    for (k = 0; k < 3; k++) {
        outp(0x61, inp(0x61) & ~3);
        outp(0x43, 0xB6); outp(0x42, 0x34); outp(0x42, 0x12);
        t0 = *t; while (*t - t0 < 6) { }
    }
    return 0;
}
