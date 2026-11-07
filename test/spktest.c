/* Phase 2 test: DBOPL (OPL2, 9ch chords) -> PC Speaker PWM.
 * usage: spktest <rate> <pitmode 0|3> <seconds> [busy_ms] [assist 0|1] [gain]
 *   gain : software gain (default 4 = x4, clipped). peak level of the OPL signal is shown so you can tune it.
 *   busy_ms: foreground busy-wait per loop (simulates a game hogging the CPU)
 * While playing, one status line per second is shown:  UNDERRUN = audio dropouts (ring buffer ran empty -> crackle).
 * Any key aborts. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include "opl_core.h"
#include "pcspk.h"

#define BIOS_TICKS (*(volatile u32 *)0x46C)

static volatile int g_peak;         /* peak |sample| of the OPL signal before gain */

static void w(u8 reg, u8 val) { oplc_write_port(0, reg); oplc_write_port(1, val); }
static void gen_cb(s16 *dst, u32 n)
{
    u32 i;
    oplc_generate_mono(dst, n);
    for (i = 0; i < n; i++) { int a = dst[i] < 0 ? -dst[i] : dst[i]; if (a > g_peak) g_peak = a; }
}

static void setup_patch(void)
{
    static const u8 opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
    int c;
    w(0x01, 0x20);
    for (c = 0; c < 9; c++) {
        u8 m = opm[c];
        w(0x20 + m, 0x01); w(0x40 + m, 0x1C); w(0x60 + m, 0xF0); w(0x80 + m, 0x77);
        w(0x23 + m, 0x01); w(0x43 + m, 0x00); w(0x63 + m, 0xC2); w(0x83 + m, 0x15);   /* carrier TL 0 (loud), sustain level 1 */
        w(0xC0 + c, 0x00);
    }
}

static void chord(int step)
{
    static const unsigned fn[7] = { 0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5 };
    int c;
    for (c = 0; c < 9; c++) {
        unsigned f = fn[(step * 2 + c) % 7];
        w(0xB0 + c, (u8)(0x10 | (f >> 8)));                 /* key off */
        w(0xA0 + c, (u8)(f & 0xFF));
        w(0xB0 + c, (u8)(0x30 | (f >> 8)));                 /* block 4, key on */
    }
}

static void chord_locked(int step) { pcspk_lock(); chord(step); pcspk_unlock(); }

int main(int argc, char **argv)
{
    u32 rate, secs, busy_ms, start, last_step = 0, last_show = 0, pumps = 0, ticks, gain, achunk;
    int mode, step = 0, assist, quiet;
    const pcspk_stats_t *st;
    double bios_s, isr_s;

    if (argc < 4) { printf("usage: spktest <rate> <pitmode 0|3> <seconds> [busy_ms] [amode 0|1|2] [gain] [achunk 8..128] [quiet 0|1]\n"); return 1; }
    rate = (u32)atol(argv[1]); mode = atoi(argv[2]); secs = (u32)atol(argv[3]);
    busy_ms = (argc > 4) ? (u32)atol(argv[4]) : 0;
    assist = (argc > 5) ? atoi(argv[5]) : 1;
    gain = (argc > 6) ? (u32)atol(argv[6]) : 1;
    achunk = (argc > 7) ? (u32)atol(argv[7]) : 128;
    quiet = (argc > 8) ? atoi(argv[8]) : 0;

    oplc_init(OPLC_OPL2, rate);
    setup_patch(); chord(0);
    if (!pcspk_open(rate, mode, gen_cb)) { printf("pcspk_open failed\n"); return 2; }
    pcspk_set_assist(assist, achunk); pcspk_set_gain(gain * 256UL);
    st = pcspk_stats();
    printf("PC speaker: rate=%lu T=%lu levels=%lu pit_mode=%d busy=%lums amode=%d achunk=%lu gain=x%lu quiet=%d\n", rate, st->T, st->levels, mode, busy_ms, assist, achunk, gain, quiet);

    start = BIOS_TICKS; last_show = start;
    while (BIOS_TICKS - start < (u32)(secs * 18.2065)) {
        pcspk_pump(); pumps++;
        if (BIOS_TICKS - last_step >= 5) { last_step = BIOS_TICKS; chord_locked(++step); }     /* ~0.27 s per chord */
        if (busy_ms) { u32 t0 = BIOS_TICKS; while ((BIOS_TICKS - t0) * 55UL < busy_ms) { } }   /* game hogging CPU, no pump */
        if (!quiet && BIOS_TICKS - last_show >= 18) {
            last_show = BIOS_TICKS;
            st = pcspk_stats();
            printf("\r %2lus  UNDERRUN=%lu  assist=%lu  stack=%lu  peak=%d  clip=%lu  ", (BIOS_TICKS - start) * 55UL / 1000UL, st->underruns, st->assists, st->istack_used, g_peak, st->clips);
            fflush(stdout);
        }
        if (kbhit()) { getch(); break; }
    }
    ticks = BIOS_TICKS - start;
    pcspk_close();
    st = pcspk_stats();

    bios_s = ticks / 18.2065; isr_s = (double)st->isr_count / (double)rate;
    printf("\nBIOS ticks=%lu (%.2f s)  ISR count=%lu (%.2f s at nominal rate)  drift=%+.2f%%\n", ticks, bios_s, st->isr_count, isr_s,
           bios_s > 0 ? (isr_s - bios_s) * 100.0 / bios_s : 0.0);
    printf("pump refills=%lu  ISR assists=%lu  UNDERRUN=%lu  clipped=%lu\n", st->refills, st->assists, st->underruns, st->clips);
    printf("UNDERRUN=0 -> no dropouts. peak=%d of 32767 (gain x%lu). If quiet raise gain; if clip>0 lower it.\n", g_peak, gain);
    return 0;
}
