/* SPDX-License-Identifier: GPL-3.0-or-later */
/* NLAB - noise lab for the PC speaker PWM driver (real hardware A/B listening, no OPL engine involved).
 * Plays a fixed sequence of synthetic signals through the same driver EOPL uses, so every noise source can be heard separately:
 *   A: carrier only (constant level)   B: 1 kHz tone, soft / loud   C: tone + simulated synthesis load (IF=0 vs /NEST)
 * Usage: NLAB            run the whole sequence, any key = next test, Q = quit
 *        NLAB <n>        run only test n
 * Ctrl-C / Ctrl-Break are intercepted so the driver is always closed (INT 8 / INT 70h / PIT / RTC restored). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <conio.h>
#include <dos.h>
#include "opl_core.h"
#include "pcspk.h"

#define BIOS_TICKS (*(volatile u32 far *)MK_FP(0x40, 0x6C))

typedef struct { char clk; unsigned rate; char kind; unsigned amp; unsigned synth_us; int nest; const char *what; int mask0; int prio; unsigned dbg; unsigned bias; unsigned gain; unsigned nlo, nhi; } test_t;

static const test_t T[] = {
 /* A: carrier only */
 { 'R',  8192, 'D',  0, 0, 0, "A1  RTC 8192 Hz, constant level (carrier only, no music)" },
 { 'P', 11025, 'D',  0, 0, 0, "A2  PIT 11025 Hz, constant level" },
 { 'P', 16000, 'D',  0, 0, 0, "A3  PIT 16000 Hz, constant level" },
 { 'P', 22050, 'D',  0, 0, 0, "A4  PIT 22050 Hz, constant level" },
 /* B: tone, amplitude */
 { 'R',  8192, 'T',  6, 0, 0, "B1  RTC 8192 Hz, 1 kHz tone 6% (music-like level)" },
 { 'R',  8192, 'T', 50, 0, 0, "B2  RTC 8192 Hz, 1 kHz tone 50% (loud)" },
 { 'P', 22050, 'T',  6, 0, 0, "B3  PIT 22050 Hz, 1 kHz tone 6%" },
 { 'P', 22050, 'T', 50, 0, 0, "B4  PIT 22050 Hz, 1 kHz tone 50%" },
 /* C: timing jitter from synthesis load (20 us busy per sample, tone 6%) */
 { 'R',  8192, 'T',  6, 20, 0, "C1  RTC 8192, tone 6% + simulated synthesis 20us/sample, IF=0 (no /NEST)" },
 { 'R',  8192, 'T',  6, 20, 1, "C2  RTC 8192, tone 6% + simulated synthesis, /NEST" },
 { 'P', 22050, 'T',  6, 20, 0, "C3  PIT 22050, tone 6% + simulated synthesis, IF=0 (no /NEST)" },
 { 'P', 22050, 'T',  6, 20, 1, "C4  PIT 22050, tone 6% + simulated synthesis, /NEST" },
 /* D: where does the periodic crackle come from? (constant level, no synthesis) */
 { 'R',  8192, 'D',  0, 0, 0, "D1  RTC 8192, constant level, IRQ0 masked (BIOS 18.2 Hz tick OFF; DOS clock stops while this runs)", 1, 0, 0 },
 { 'R',  8192, 'D',  0, 0, 0, "D2  RTC 8192, constant level, BIOS tick ON, PIC priority changed so IRQ8 beats IRQ0/IRQ1", 0, 1, 0 },
 { 'R',  8192, 'D',  0, 0, 0, "D3  RTC 8192, constant level, driver guard + rate meter OFF", 0, 0, 3 },
 { 'P', 22050, 'D',  0, 0, 0, "D4  PIT 22050, constant level, BIOS INT 8 chain OFF (DOS clock stops while this runs)", 0, 0, 4 },
 { 'R',  8192, 'D',  0, 0, 0, "D5  RTC 8192, constant level, IRQ0 masked + guard/meter OFF (quietest possible RTC case)", 1, 0, 3 },
 { 'R',  8192, 'T',  6, 20, 1, "D6  RTC 8192, tone 6% + synthesis load, /NEST + PIC priority fix (realistic candidate)", 0, 1, 0 },
 { 'P', 22050, 'T',  6, 20, 1, "D7  PIT 22050, tone 6% + synthesis load, /NEST + no BIOS chain (realistic candidate)", 0, 0, 4 },
 /* E: PWM details taken from simple DOS PCM players (control word 90h re-sent before every count) */
 { 'R',  8192, 'D',  0, 0, 0, "E1  RTC 8192, constant level, control word 90h before every count (compare with A1)", 0, 0, 8 },
 { 'P', 22050, 'D',  0, 0, 0, "E2  PIT 22050, constant level, control word 90h before every count (compare with A4)", 0, 0, 8 },
 { 'R',  8192, 'T',  6, 20, 1, "E3  RTC 8192, tone 6% + synthesis load, /NEST, control word 90h before every count (compare with C2)", 0, 0, 8 },
 /* F: is the periodic ticking caused by IRQ delays (SMI etc.)?  meter = PIT ch0 timestamps at every RTC IRQ */
 { 'R',  8192, 'D',  0, 0, 0, "F1  RTC 8192, constant level + IRQ latency meter (prints the gap statistics at the end)", 0, 0, 16 },
 { 'R',  8192, 'D',  0, 0, 0, "F2  RTC 8192, output FROZEN after 100 IRQs (constant DC, no PWM activity) + meter: do you still hear ticks?", 0, 0, 48 },
 { 'R',  8192, 'D',  0, 0, 0, "F3  like F1 but IRQ0 masked and guard/meter-off (quietest case) + latency meter", 1, 0, 19 },
 /* G: PWM operating point near the rail: the stuck-HIGH level during an SMI stall and the carrier amplitude both shrink; signal swing kept equal (6% tone) */
 { 'R',  8192, 'T',  6, 0, 0, "G1  RTC 8192, tone 6%, legacy mapping (silence = 50% duty) - reference", 0, 0, 0, 0, 0 },
 { 'R',  8192, 'T',  6, 0, 0, "G2  RTC 8192, tone 6%, rail bias 36 ticks (25% of period), gain x2 (same loudness)", 0, 0, 0, 36, 512 },
 { 'R',  8192, 'T',  6, 0, 0, "G3  RTC 8192, tone 6%, rail bias 18 ticks (12%), gain x4 (same loudness)", 0, 0, 0, 18, 1024 },
 { 'R',  8192, 'T',  6, 0, 0, "G4  RTC 8192, tone 6%, rail bias 10 ticks (7%), gain x7 (same loudness)", 0, 0, 0, 10, 1792 },
 { 'P', 22050, 'T',  6, 0, 0, "G5  PIT 22050, tone 6%, legacy mapping - reference", 0, 0, 0, 0, 0 },
 { 'P', 22050, 'T',  6, 0, 0, "G6  PIT 22050, tone 6%, rail bias 6 ticks, gain x4", 0, 0, 0, 6, 1024 },
 { 'P', 22050, 'T',  6, 0, 0, "G7  PIT 22050, tone 6%, rail bias 4 ticks, gain x6", 0, 0, 0, 4, 1536 },
 /* H: does the carrier whine depend on the pulse width?  constant pulses at 8192 Hz, LOW time per period (ticks of 0.84 us) shown in the name */
 { 'R',  8192, 'D',  2, 0, 0, "H1  RTC 8192, constant pulses, LOW 70 ticks (49% duty)", 0, 0, 0, 71, 0 },
 { 'R',  8192, 'D', 50, 0, 0, "H2  RTC 8192, constant pulses, LOW 36 ticks (25%)", 0, 0, 0, 71, 0 },
 { 'R',  8192, 'D', 75, 0, 0, "H3  RTC 8192, constant pulses, LOW 18 ticks (12%)", 0, 0, 0, 71, 0 },
 { 'R',  8192, 'D', 87, 0, 0, "H4  RTC 8192, constant pulses, LOW 10 ticks (7%)", 0, 0, 0, 71, 0 },
 { 'R',  8192, 'D', 94, 0, 0, "H5  RTC 8192, constant pulses, LOW 5 ticks (3.5%)", 0, 0, 0, 71, 0 },
 { 'R',  8192, 'D', 97, 0, 0, "H6  RTC 8192, constant pulses, LOW 3 ticks (2%)", 0, 0, 0, 71, 0 },
 /* I: PIT channel 2 as a free-running rate generator (pulse-density modulation): carrier 1193182/N Hz inside the speaker band edge, no stuck level during stalls */
 { 'R',  8192, 'D', 20, 0, 0, "I1  RTC 8192 sample clock, PIT ch2 mode 2 (pulse density), constant, N=20..75 (carrier ~ 38 kHz)", 0, 0, 0, 0, 0, 20, 75 },
 { 'R',  8192, 'T',  6, 0, 0, "I2  RTC 8192, tone 6% x8, pulse density N=20..75", 0, 0, 0, 0, 2048, 20, 75 },
 { 'R',  8192, 'T',  6, 0, 0, "I3  RTC 8192, tone 6% x8, pulse density N=16..48 (higher carrier, larger swing)", 0, 0, 0, 0, 2048, 16, 48 },
 { 'R',  8192, 'T',  6, 0, 0, "I4  RTC 8192, tone 6% x8, pulse density N=28..75 (smaller swing)", 0, 0, 0, 0, 2048, 28, 75 },
 { 'R',  8192, 'T',  6, 20, 1, "I5  RTC 8192, tone 6% x8 + synthesis load, /NEST, pulse density N=20..75 (compare C2)", 0, 0, 0, 0, 2048, 20, 75 },
 /* J: louder pulse-density presets (denser pulses = larger swing; compare loudness with I2; same tone, same gain) */
 { 'R',  8192, 'T',  6, 0, 0, "J1  PDM preset 4: N=8..75 (swing x3 vs I2), tone 6% x8", 0, 0, 0, 0, 2048, 8, 75 },
 { 'R',  8192, 'T',  6, 0, 0, "J2  PDM preset 5: N=5..75 (swing x5), tone 6% x8", 0, 0, 0, 0, 2048, 5, 75 },
 { 'R',  8192, 'T',  6, 0, 0, "J3  PDM preset 6: N=3..75 (swing x8), tone 6% x8", 0, 0, 0, 0, 2048, 3, 75 },
};
#define NT ((int)(sizeof(T) / sizeof(T[0])))

static volatile int g_abort;
static void (__interrupt __far *old23)(void), (__interrupt __far *old1b)(void);
static void __interrupt __far brk_isr(void) { g_abort = 1; }
static void brk_install(void) { old23 = _dos_getvect(0x23); old1b = _dos_getvect(0x1B); _dos_setvect(0x23, brk_isr); _dos_setvect(0x1B, brk_isr); }
static void brk_restore(void) { if (old23) { _dos_setvect(0x23, old23); old23 = 0; } if (old1b) { _dos_setvect(0x1B, old1b); old1b = 0; } }
static int g_masked, g_prio;
static void lab_restore(void)
{
    if (g_masked) { outp(0x21, (unsigned char)(inp(0x21) & ~0x01)); g_masked = 0; }   /* unmask IRQ0 */
    if (g_prio) { outp(0x20, 0xC7); g_prio = 0; }                                    /* default priority order: IRQ0 highest, IRQ7 lowest */
}
static void cleanup(void) { lab_restore(); pcspk_close(); brk_restore(); }

static s16 g_tab[256];
static u32 g_phase, g_inc;
static const test_t *g_t;
static u32 g_auto_secs;       /* >0: each test ends by itself after this many seconds (automated smoke test) */
static u32 g_loops_per_us;     /* busy-loop iterations per microsecond (calibrated) */

static void spin_us(unsigned us)
{
    volatile u32 n = (u32)us * g_loops_per_us;
    while (n) n--;
}

static void gen_cb(s16 *dst, u32 n)
{
    unsigned i;
    if (g_t->kind == 'D') {
        { s16 dc = g_t->amp ? (s16)((long)g_t->amp * 327L) : 1500; for (i = 0; i < (unsigned)n; i++) dst[i] = dc; }            /* constant, non-zero (zero would trigger the idle mute) */
    } else {
        for (i = 0; i < (unsigned)n; i++) { dst[i] = g_tab[(unsigned)(g_phase >> 8) & 255]; g_phase += g_inc; }
    }
    if (g_t->synth_us) spin_us((unsigned)(g_t->synth_us * (unsigned)n));
}

static void calibrate(void)
{
    u32 t0, n = 0;
    t0 = BIOS_TICKS; while (BIOS_TICKS == t0) { }
    t0 = BIOS_TICKS;
    { volatile u32 k; while (BIOS_TICKS == t0) { for (k = 0; k < 16; k++) { } n++; } }
    g_loops_per_us = (n * 16UL) / 54925UL;               /* one BIOS tick = 54925 us; n outer iterations x 16 inner ~ loop count */
    if (g_loops_per_us == 0) g_loops_per_us = 1;
}

static void logf2(const char *m) { FILE *f = fopen("NL.LOG", "a"); if (f) { fputs(m, f); fputs("\n", f); fclose(f); } }

static void print_lat(void)
{
    const pcspk_lat_t *l = pcspk_lat();
    unsigned i;
    u32 us100;                                  /* microseconds x100 per counter unit (RTC period 122.07 us / mean gap) */
    if (!l->nominal) { printf("\n      latency meter: too few IRQs\n"); return; }
    us100 = 0;
    printf("\n      IRQs=%lu  mean gap=%u units (=122.07 us)  max gap=%lu us\n", l->n, l->nominal, (u32)l->max * 12207UL / ((u32)l->nominal * 100UL));
    printf("      gaps >1.5x: %lu   >5x (>0.6 ms): %lu   >20x (>2.4 ms): %lu   (%lu IRQs = %lu s)\n", l->n15, l->n5, l->n20, l->n, l->n / 8192UL);
    for (i = 0; i < l->nev; i++) printf("      event %2u: t=%lu.%02lu s  gap=%lu us\n", i + 1, l->ev_idx[i] / 8192UL, (l->ev_idx[i] % 8192UL) * 100UL / 8192UL, (u32)l->ev_gap[i] * 12207UL / ((u32)l->nominal * 100UL));
}

static int run(int idx)
{
    const test_t *t = &T[idx];
    const pcspk_stats_t *st;
    unsigned i; int ok, key = 0;
    double a = 32767.0 * t->amp / 100.0;

    g_t = t; g_phase = 0; g_inc = (1000UL << 16) / t->rate;
    for (i = 0; i < 256; i++) g_tab[i] = (s16)(a * sin(6.283185307179586 * i / 256.0));
    oplc_init(OPLC_OPL2, t->rate);                         /* driver wants an initialised core even though we do not use it */
    logf2("before open");
    if (t->nlo) pcspk_set_pdm(t->nlo, t->nhi);
    ok = (t->clk == 'R') ? pcspk_open_rtc(t->nlo ? 2 : 0, gen_cb) : pcspk_open((u32)t->rate, t->nlo ? 2 : 0, gen_cb);
    logf2("after open");
    if (!ok) { printf("cannot open driver\n"); return 0; }
    pcspk_set_assist(1, 32); logf2("assist set"); pcspk_set_water(1024); logf2("water set");
    if (t->nest) pcspk_set_nest(1);
    if (t->dbg & 16) pcspk_lat_reset();
    pcspk_set_dbg(t->dbg);
    pcspk_set_gain(t->gain ? t->gain : 256);
    pcspk_set_bias(t->bias);
    if (t->mask0) { outp(0x21, (unsigned char)(inp(0x21) | 0x01)); g_masked = 1; }
    if (t->prio) { outp(0x20, 0xC1); g_prio = 1; }                                       /* OCW2 set priority: IRQ1 lowest -> IRQ2 (slave PIC incl. RTC IRQ8) highest */
    printf("\n[%d/%d] %s\n      any key = next, Q = quit ", idx + 1, NT, t->what); fflush(stdout);
    logf2("printed");
    { u32 t0 = BIOS_TICKS, need = (u32)t->rate * g_auto_secs; while (!g_abort) { if (kbhit()) { key = getch(); break; } if (g_auto_secs && (pcspk_stats()->isr_count >= need || (BIOS_TICKS - t0) * 55UL >= g_auto_secs * 1000UL)) break; } }
    logf2("loop done");
    if (t->dbg & 16) print_lat();
    st = pcspk_stats();
    printf("\n      underruns=%lu  isr=%lu\n", st->underruns, st->isr_count);
    lab_restore();
    pcspk_close();
    logf2("closed");
    return (g_abort || key == 'q' || key == 'Q') ? 0 : 1;
}

int main(int argc, char **argv)
{
    int i, only = (argc > 1) ? atoi(argv[1]) : 0;
    if (argc > 2) g_auto_secs = (u32)atol(argv[2]);
    atexit(cleanup); brk_install();
    printf("NLAB - PC speaker noise lab. Listen to each test and write down: clean / hiss / whine / crackle.\n");
    calibrate();
    if (only >= 1 && only <= NT) { run(only - 1); return 0; }
    for (i = 0; i < NT; i++) if (!run(i)) break;
    cleanup();
    printf("done.\n");
    return 0;
}
