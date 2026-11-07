/* SPDX-License-Identifier: GPL-3.0-or-later */
/* E-ray DOS OPL Emulator - PC Speaker PCM driver (Phase 2 prototype, v3: selectable ISR-assist strategy for bisecting crashes)
 *  - PIT ch2 mode 0 (0x90, LSB only): count write -> OUT low N+1 ticks then high => duty-cycle PWM; samples scaled to 0..T-2.
 *  - PIT ch0 at the sample rate; ISR writes one count/tick, chains BIOS INT 8 every 65536 PIT ticks (16.16 accumulator).
 *  - Ring buffer. Foreground pcspk_pump() tops it up; if it does not (game hogging CPU) the ISR may assist:
 *      amode 0 : no assist (ring only)
 *      amode 1 : generate `achunk` samples inside the ISR with IF=0 (before EOI)
 *      amode 2 : EOI first, STI, generate with nesting allowed (nested IRQ0 only outputs a sample; lock prevents re-entry)
 *    The assist runs on a private stack (g_istack) - measured use ~300 bytes. */
#pragma off (check_stack)

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdlib.h>
#include <string.h>
#include "pcspk.h"

#define RING      2048u
#define MASK      (RING - 1)
#define CHUNK     128u          /* max samples per generation step */
#define LOW       768u          /* ISR assist threshold (fill below this) */

extern unsigned irq_save(void);
#pragma aux irq_save = "pushfd" "pop eax" "cli" value [eax] modify [eax];
extern void irq_restore(unsigned);
#pragma aux irq_restore = "push eax" "popfd" parm [eax];

static struct {
    volatile u8      ring[RING];
    volatile u32     rd, wr;            /* free-running counters; fill = wr - rd */
    volatile u8      running, lock, last;
    u32              T, accum;
    int              mode, amode;
    u32              achunk;
    pcspk_pcm_cb     cb;
    void (__interrupt __far *prev)(void);
    pcspk_stats_t    st;
} P;

static u32 g_gain = 256;        /* 8.8 fixed-point software gain (256 = x1) */

#define ISTACK 2048u            /* measured high-water mark of the assist path: ~312 bytes (was 16 KB) */
static u8 g_istack[ISTACK + 16];

extern void call_on_stack(void (*fn)(void), void *top);
#pragma aux call_on_stack = \
    "mov edx, esp" "mov esp, ebx" "push edx" "call eax" "pop esp" \
    parm [eax] [ebx] modify [eax ebx ecx edx esi edi];

/* generate n (<= CHUNK) samples at wr. Caller guarantees exclusive access to the OPL engine (lock held). */
static void gen_n(u32 n)
{
    s16 tmp[CHUNK];
    u32 i, span = P.T - 2, w = P.wr;
    P.cb(tmp, n);
    for (i = 0; i < n; i++) {
        long s = ((long)tmp[i] * (long)g_gain) >> 8;
        u32 level;
        if (s > 32767L) { s = 32767L; P.st.clips++; } else if (s < -32768L) { s = -32768L; P.st.clips++; }
        level = ((u32)(s + 32768L) * span) >> 16;
        P.ring[(w + i) & MASK] = (u8)(P.mode == 0 ? (span - level) : (2 + level));
    }
    P.wr = w + n;
}
static void gen_chunk(void)  { gen_n(CHUNK); }
static void assist_gen(void) { gen_n(P.achunk); }
/* amode 1: IF=0 + private stack;  amode 3: IF=0 on DOS/4GW's own interrupt stack (original v2 behaviour) */
static void run_assist(void)
{
    if (P.amode == 1) call_on_stack(assist_gen, g_istack + ISTACK - 16);
    else              assist_gen();
}

static void __interrupt __far pcspk_isr(void)
{
    u8 c; int chain, need;
    if (!P.running) { outp(0x20, 0x20); return; }
    if (P.wr != P.rd) { c = P.ring[P.rd & MASK]; P.rd++; P.last = c; }
    else { c = P.last; P.st.underruns++; }
    outp(0x42, c);
    P.st.isr_count++;

    P.accum += P.T;
    chain = (P.accum >= 0x10000UL);
    if (chain) P.accum -= 0x10000UL;
    need = P.amode && !P.lock && (P.wr - P.rd) < LOW;

    if (chain) {                                            /* BIOS tick: keep this path short; assist (IF=0 modes) first */
        if (need && P.amode != 2) { P.lock = 1; run_assist(); P.lock = 0; P.st.assists++; }
        _chain_intr(P.prev);                                /* does not return; BIOS handler sends the EOI */
    }
    if (need && P.amode != 2) {                             /* amode 1/3: generate with IF=0, then EOI */
        P.lock = 1; run_assist(); P.lock = 0; P.st.assists++;
        outp(0x20, 0x20);
    } else if (need) {                                      /* amode 2: EOI, allow nesting (stay on DOS/4GW's stack!), generate */
        outp(0x20, 0x20);
        P.lock = 1; _enable();
        assist_gen();
        _disable(); P.lock = 0; P.st.assists++;
    } else outp(0x20, 0x20);
}

int pcspk_open(u32 rate, int mode, pcspk_pcm_cb cb)
{
    unsigned fl; u8 mid;
    if (P.running || !cb || rate < 4000 || rate > 44100) return 0;
    memset(&P, 0, sizeof P);
    memset(g_istack, 0xA5, sizeof g_istack);
    P.T = 1193182UL / rate; P.mode = mode; P.cb = cb; P.amode = 1; P.achunk = CHUNK;
    if (P.T < 16 || P.T > 250) return 0;
    P.st.T = P.T; P.st.levels = P.T - 1;
    while (P.wr - P.rd <= RING - CHUNK) gen_chunk();       /* prefill */
    mid = (u8)((P.T - 2) / 2); P.last = mid;

    P.prev = _dos_getvect(0x08);
    _dos_setvect(0x08, pcspk_isr);
    outp(0x61, inp(0x61) | 0x03);
    fl = irq_save();
    outp(0x43, mode == 0 ? 0x90 : 0x96);
    outp(0x42, mid);
    outp(0x43, 0x36);
    outp(0x40, (u8)(P.T & 0xFF)); outp(0x40, (u8)(P.T >> 8));
    P.running = 1;
    irq_restore(fl);
    return 1;
}

void pcspk_lock(void)   { unsigned fl; for (;;) { fl = irq_save(); if (!P.lock) { P.lock = 1; irq_restore(fl); return; } irq_restore(fl); } }
void pcspk_unlock(void) { P.lock = 0; }
void pcspk_set_assist(int amode, u32 achunk)
{
    P.amode = amode;
    P.achunk = (achunk >= 8 && achunk <= CHUNK) ? achunk : CHUNK;
}
void pcspk_set_gain(u32 gain_256) { g_gain = gain_256 ? gain_256 : 256; }

void pcspk_pump(void)
{
    if (!P.running) return;
    while ((P.wr - P.rd) <= RING - CHUNK) {
        pcspk_lock();
        if ((P.wr - P.rd) <= RING - CHUNK) { gen_chunk(); P.st.refills++; }
        pcspk_unlock();
    }
}

void pcspk_close(void)
{
    unsigned fl;
    if (!P.prev) return;
    fl = irq_save();
    P.running = 0;
    outp(0x21, inp(0x21) | 0x01);
    _dos_setvect(0x08, P.prev);
    outp(0x43, 0x36); outp(0x40, 0); outp(0x40, 0);
    outp(0x61, inp(0x61) & ~0x03);
    outp(0x43, 0xB6); outp(0x42, 0); outp(0x42, 0);
    outp(0x21, inp(0x21) & ~0x01);
    P.prev = 0;
    irq_restore(fl);
}

const pcspk_stats_t *pcspk_stats(void)
{
    u32 i = 0;
    while (i < sizeof g_istack && g_istack[i] == 0xA5) i++;
    P.st.istack_used = sizeof g_istack - i;
    return &P.st;
}
