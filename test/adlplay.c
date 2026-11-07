/* ADLPLAY - a minimal "AdLib game": talks to the AdLib ports 388h/389h DIRECTLY (OUT/IN), exactly like a DOS game does.
 *   - standard AdLib detection (timer test), then 9-voice chords
 *   - standard AdLib timing: 6 status reads after an address write, 35 after a data write (these are trapped IN instructions under QEMM;
 *     EOPL patches them to NOPs after the detection phase, so the write rate shows how well trapping + patching works)
 * usage: adlplay [seconds]     Ctrl-C / any key stops.
 * With EOPL loaded (QEMM + QPI trap) you should HEAR the chords on the PC speaker. Without EOPL (and no real AdLib): detection fails. */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>

#define TICKS (*(volatile unsigned long far *)MK_FP(0x40, 0x6C))
static volatile int g_abort;
static void __interrupt __far brk_isr(void) { g_abort = 1; }
static unsigned long g_writes;

static void adl(unsigned reg, unsigned val)
{
    int i;
    outp(0x388, reg);
    for (i = 0; i < 6; i++)  inp(0x388);
    outp(0x389, val);
    for (i = 0; i < 35; i++) inp(0x388);
    g_writes++;
}

static int detect_adlib(void)
{
    unsigned s1, s2;
    adl(4, 0x60); adl(4, 0x80);
    s1 = inp(0x388);
    adl(2, 0xFF); adl(4, 0x21);
    {   unsigned long t0 = TICKS; while (TICKS - t0 < 2) { } }          /* > 80 us */
    s2 = inp(0x388);
    adl(4, 0x60); adl(4, 0x80);
    printf("  status after reset = %02X, after timer1 start = %02X\n", s1, s2);
    return (s1 & 0xE0) == 0x00 && (s2 & 0xE0) == 0xC0;
}

static void patch(void)
{
    static const unsigned char opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
    int c;
    adl(0x01, 0x20);
    for (c = 0; c < 9; c++) {
        unsigned m = opm[c];
        adl(0x20 + m, 0x01); adl(0x40 + m, 0x1C); adl(0x60 + m, 0xF0); adl(0x80 + m, 0x77);
        adl(0x23 + m, 0x01); adl(0x43 + m, 0x08); adl(0x63 + m, 0xC2); adl(0x83 + m, 0x15);
        adl(0xC0 + c, 0x00);
    }
}
static void chord(int step)
{
    static const unsigned fn[7] = { 0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5 };
    int c;
    for (c = 0; c < 9; c++) {
        unsigned f = fn[(step * 2 + c) % 7];
        adl(0xB0 + c, 0x10 | (f >> 8)); adl(0xA0 + c, f & 0xFF); adl(0xB0 + c, 0x30 | (f >> 8));
    }
}
static void silence(void) { int c; for (c = 0; c < 9; c++) adl(0xB0 + c, 0x00); }

int main(int argc, char **argv)
{
    unsigned long secs = (argc > 1) ? (unsigned long)atol(argv[1]) : 10UL, start, last_step = 0, last_show, loops = 0, lastloops = 0;
    int step = 0;
    void (__interrupt __far *old23)(void) = _dos_getvect(0x23), (__interrupt __far *old1b)(void) = _dos_getvect(0x1B);

    _dos_setvect(0x23, brk_isr); _dos_setvect(0x1B, brk_isr);
    printf("ADLPLAY: AdLib detection...\n");
    if (!detect_adlib()) { printf("  AdLib NOT detected (is EOPL loaded and the trap active?)\n"); _dos_setvect(0x23, old23); _dos_setvect(0x1B, old1b); return 1; }
    printf("  AdLib detected. Playing chords for %lu s (Ctrl-C / any key = stop)\n", secs);
    patch(); chord(0);
    start = TICKS; last_show = start;
    while (TICKS - start < (unsigned long)(secs * 18.2065) && !g_abort) {
        loops++;
        if (TICKS - last_step >= 5) { last_step = TICKS; chord(++step); }
        if (TICKS - last_show >= 18) {
            printf("\r %2lus  OPL writes=%lu  loops/s=%lu   ", (TICKS - start) * 55UL / 1000UL, g_writes, loops - lastloops);
            fflush(stdout); last_show = TICKS; lastloops = loops;
        }
        if (kbhit()) { getch(); break; }
    }
    silence();
    {
        unsigned long tk = TICKS - start; if (!tk) tk = 1;
        printf("\ndone: %lu OPL register writes in %lu ticks (%lu writes/s)\n", g_writes, tk, (g_writes * 18207UL) / (tk * 1000UL));
    }
    _dos_setvect(0x23, old23); _dos_setvect(0x1B, old1b);
    return 0;
}
