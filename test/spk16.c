/* Phase 2b test (16-bit real mode, no DOS/4GW): DBOPL16 (OPL2, 9ch chords) -> PC Speaker PWM via a real-mode INT 8 hook.
 * Runs the same way the final TSR will (V86 under QEMM/EMM386, or plain real mode).
 * usage: spk16 <rate> <pitmode 0|3> <seconds> [busy_ms] [assist 0|1] [gain] [achunk 8..128] [quiet 0|1]
 *        spk16 bench <rate>
 * Status line (1/s): UNDERRUN = ring ran empty (dropouts), assist = chunks generated inside the ISR, stack = ISR stack bytes used.
 * Any key, Ctrl-C or Ctrl-Break stops it cleanly (v3: INT 23h/1Bh are intercepted; before, Ctrl-C left INT 8 hooked into freed memory). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include "opl_core.h"
#include "pcspk.h"

#define BIOS_TICKS (*(volatile u32 far *)MK_FP(0x40, 0x6C))

static volatile int g_peak;
static volatile int g_abort;                     /* set by Ctrl-C / Ctrl-Break handlers */
static void (__interrupt __far *old23)(void), (__interrupt __far *old1b)(void);

/* DOS Ctrl-C (INT 23h) and BIOS Ctrl-Break (INT 1Bh): just raise a flag and return -> the main loop shuts the driver down. */
static void __interrupt __far brk_isr(void) { g_abort = 1; }

static void brk_install(void)
{
    old23 = _dos_getvect(0x23); old1b = _dos_getvect(0x1B);
    _dos_setvect(0x23, brk_isr); _dos_setvect(0x1B, brk_isr);
}
static void brk_restore(void)
{
    if (old23) { _dos_setvect(0x23, old23); old23 = 0; }
    if (old1b) { _dos_setvect(0x1B, old1b); old1b = 0; }
}
/* last line of defence for every normal exit path (exit(), return from main) */
static void cleanup(void) { pcspk_close(); brk_restore(); }

static void w(u8 reg, u8 val) { oplc_write_port(0, reg); oplc_write_port(1, val); }
static void gen_cb(s16 *dst, u32 n)
{
    unsigned i;
    oplc_generate_mono(dst, n);
    for (i = 0; i < (unsigned)n; i += 4) { int a = dst[i] < 0 ? -dst[i] : dst[i]; if (a > g_peak) g_peak = a; }   /* sampled peak */
}

static void setup_patch(void)
{
    static const u8 opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
    int c;
    w(0x01, 0x20);
    for (c = 0; c < 9; c++) {
        u8 m = opm[c];
        w(0x20 + m, 0x01); w(0x40 + m, 0x1C); w(0x60 + m, 0xF0); w(0x80 + m, 0x77);
        w(0x23 + m, 0x01); w(0x43 + m, 0x08); w(0x63 + m, 0xC2); w(0x83 + m, 0x15);
        w(0xC0 + c, 0x00);
    }
}

static void chord(int step)
{
    static const unsigned fn[7] = { 0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5 };
    int c;
    for (c = 0; c < 9; c++) {
        unsigned f = fn[(step * 2 + c) % 7];
        w(0xB0 + c, (u8)(0x10 | (f >> 8)));
        w(0xA0 + c, (u8)(f & 0xFF));
        w(0xB0 + c, (u8)(0x30 | (f >> 8)));
    }
}

static void chord_locked(int step) { pcspk_lock(); chord(step); pcspk_unlock(); }

/* ---- bench mode: separates "synthesis too slow" from "IRQ overhead too big" ---- */
static void silent_cb(s16 *d, u32 n) { memset(d, 0, (unsigned)n * 2); }
static u32 spin_ticks(u32 nticks)
{
    u32 t0, n = 0;
    t0 = BIOS_TICKS; while (BIOS_TICKS == t0) { }        /* align to a tick edge */
    t0 = BIOS_TICKS;
    while (BIOS_TICKS - t0 < nticks && !g_abort) n++;
    return n;
}
static int bench(u32 rate)
{
    s16 buf[256]; u32 L0, L1, t0, t1, done, total, isr0, isr1;
    double el, factor, ovh, per_irq_us, synth_pct;
    const pcspk_stats_t *st;

    oplc_init(OPLC_OPL2, rate); setup_patch(); chord(0);
    /* worst case: EGT (sustain) bit on for both operators so the envelope holds at the sustain level and NO channel decays
     * into silence (DBOPL skips silent channels, which made the first bench far too optimistic). */
    {
        static const u8 opm2[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
        int c2; for (c2 = 0; c2 < 9; c2++) { w(0x20 + opm2[c2], 0x21); w(0x23 + opm2[c2], 0x21); }
        chord(1);
    }
    printf("BENCH rate=%lu Hz  (OPL2, 9 channels, sustained = worst case)\n", rate);

    L0 = spin_ticks(36);                                  /* ~2 s, no IRQ, no synthesis */
    printf(" (1) idle loop count (no IRQ)          : %lu\n", L0);
    if (g_abort) return 3;

    total = rate * 4UL;                                   /* synthesis speed with IRQ off */
    t0 = BIOS_TICKS; while (BIOS_TICKS == t0) { } t0 = BIOS_TICKS;
    for (done = 0; done < total && !g_abort; done += 256) oplc_generate_mono(buf, 256);
    t1 = BIOS_TICKS;
    if (g_abort) return 3;
    el = (double)(t1 - t0) / 18.2065; factor = el > 0 ? ((double)done / (double)rate) / el : 0.0;
    synth_pct = factor > 0 ? 100.0 / factor : 999.0;
    printf(" (2) synthesis only (IRQ off)          : realtime x%.2f  => needs %.0f%% of the CPU\n", factor, synth_pct);

    if (!pcspk_open(rate, 0, silent_cb)) { printf("pcspk_open failed\n"); return 2; }
    pcspk_set_assist(0, 64);
    st = pcspk_stats(); isr0 = st->isr_count;
    L1 = spin_ticks(36);                                  /* IRQ0 running at 'rate', no synthesis */
    st = pcspk_stats(); isr1 = st->isr_count;
    pcspk_close();
    ovh = L0 ? (1.0 - (double)L1 / (double)L0) * 100.0 : 0.0;
    per_irq_us = (isr1 > isr0) ? (ovh / 100.0) * (36.0 / 18.2065) * 1e6 / (double)(isr1 - isr0) : 0.0;
    printf(" (3) IRQ0 at %lu Hz, no synthesis       : loops %lu  => IRQ overhead %.1f%% of the CPU (%.1f us per IRQ, %lu IRQs)\n",
           rate, L1, ovh, per_irq_us, isr1 - isr0);
    printf(" (4) estimated total load = %.0f%% (IRQ) + %.0f%% (synthesis) = %.0f%%   (must stay well below 100%%)\n", ovh, synth_pct, ovh + synth_pct);
    return 0;
}

int main(int argc, char **argv)
{
    u32 rate, secs, busy_ms, start, last_step = 0, last_show = 0, pumps = 0, ticks, gain, achunk;
    int mode, step = 0, assist, quiet, rc;
    const pcspk_stats_t *st;

    atexit(cleanup);
    brk_install();
    if (argc >= 3 && strcmp(argv[1], "bench") == 0) { rc = bench((u32)atol(argv[2])); cleanup(); return rc; }
    if (argc < 4) { printf("usage: spk16 <rate> <pitmode 0|3> <seconds> [busy_ms] [assist 0|1] [gain] [achunk 8..128] [quiet 0|1]\n       spk16 bench <rate>\n"); return 1; }
    rate = (u32)atol(argv[1]); mode = atoi(argv[2]); secs = (u32)atol(argv[3]);
    busy_ms = (argc > 4) ? (u32)atol(argv[4]) : 0;
    assist = (argc > 5) ? atoi(argv[5]) : 1;
    gain = (argc > 6) ? (u32)atol(argv[6]) : 1;
    achunk = (argc > 7) ? (u32)atol(argv[7]) : 64;
    quiet = (argc > 8) ? atoi(argv[8]) : 0;

    oplc_init(OPLC_OPL2, rate);
    setup_patch(); chord(0);
    if (!pcspk_open(rate, mode, gen_cb)) { printf("pcspk_open failed\n"); return 2; }
    pcspk_set_assist(assist, achunk); pcspk_set_gain(gain * 256UL);
    st = pcspk_stats();
    printf("SPK16 real-mode: rate=%lu T=%lu levels=%lu pit_mode=%d busy=%lums assist=%d achunk=%lu gain=x%lu quiet=%d   (Ctrl-C / any key = stop)\n",
           rate, st->T, st->levels, mode, busy_ms, assist, achunk, gain, quiet);

    start = BIOS_TICKS; last_show = start;
    while (BIOS_TICKS - start < (u32)(secs * 18.2065) && !g_abort) {
        pcspk_pump(); pumps++;
        if (BIOS_TICKS - last_step >= 5) { last_step = BIOS_TICKS; chord_locked(++step); }
        if (busy_ms) { u32 t0 = BIOS_TICKS; while ((BIOS_TICKS - t0) * 55UL < busy_ms && !g_abort) { } }
        if (!quiet && BIOS_TICKS - last_show >= 18) {
            last_show = BIOS_TICKS;
            st = pcspk_stats();
            printf("\r %2lus  UNDERRUN=%lu  assist=%lu  stack=%lu  peak=%d  clip=%lu  ", (BIOS_TICKS - start) * 55UL / 1000UL, st->underruns, st->assists, st->istack_used, g_peak, st->clips);
            fflush(stdout);
        }
        if (kbhit()) { getch(); break; }
    }
    ticks = BIOS_TICKS - start;
    pcspk_close();                                       /* ALWAYS restore INT 8 / PIT / speaker before leaving */
    st = pcspk_stats();

    printf("\n%s\n", g_abort ? "(stopped by Ctrl-C/Break - driver closed cleanly)" : "");
    printf("BIOS ticks=%lu (%.2f s)  ISR count=%lu (%.2f s at nominal rate)\n", ticks, ticks / 18.2065, st->isr_count, (double)st->isr_count / (double)rate);
    printf("pump refills=%lu  ISR assists=%lu  UNDERRUN=%lu  clipped=%lu  isr-stack-used=%lu/1536\n", st->refills, st->assists, st->underruns, st->clips, st->istack_used);
    printf("UNDERRUN=0 -> no dropouts. peak=%d of 32767.\n", g_peak);
    brk_restore();
    return 0;
}
