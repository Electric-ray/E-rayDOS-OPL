/* SPDX-License-Identifier: GPL-3.0-or-later */
/* E-ray DOS OPL Emulator - PC Speaker PCM driver, 16-bit real mode / V86 (Phase 2b/3a/3b)
 *  - PIT ch2 mode 0 PWM. Sample clock = EITHER PIT ch0 (IRQ0, any rate; BIOS INT 8 chained at 18.2 Hz via a 16.16 accumulator)
 *    OR the RTC periodic interrupt (IRQ8/INT 70h, fixed 8192 Hz; PIT ch0 and INT 8 are left completely alone - robust against
 *    games that reprogram PIT ch0 for their own timing).
 *  - ring buffer consumed by the ISR; the ISR itself generates small chunks when the ring runs below the "low water" mark
 *    (TSR mode: there is no foreground pump - all synthesis happens inside the ISR with IF=0, on a PRIVATE stack)
 *  - OPL register writes from foreground/trap code go through pcspk_call_private(): IF=0 + private stack => exclusive against the ISR */
#pragma off (check_stack)

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <string.h>
#include "pcspk.h"

#define RING      2048u
#define MASK      (RING - 1)
#define CHUNK     64u           /* max samples per generation step (chunk buffer lives on the private ISR stack: 2 bytes/sample) */
#define ACHUNK    32u           /* default ISR chunk: ~2 ms of audio at 16 kHz => bounds the time spent with IF=0 */
#define LOW       1024u         /* default ISR synthesis threshold (ring fill below this) */
#define ISTACK    768u          /* private stack: chunk buffer 128B + engine ~340B measured (hwm is reported by /S) */
#define RTC_RATE  8192u
#define WSTACK    512u          /* OPL write path measured well below this (hwm reported by /S) */
#define SUB_DEF   16u           /* default synthesis sub-block (86Box listening test: 16 sounded best of 4/8/16) */

extern unsigned irq_save(void);
#pragma aux irq_save = "pushf" "pop ax" "cli" value [ax] modify [ax];
extern void irq_restore(unsigned);
#pragma aux irq_restore = "push ax" "popf" parm [ax];

static struct {
    volatile unsigned char ring[RING];
    volatile unsigned rd, wr;           /* free-running 16-bit counters (RING is a power of 2) */
    volatile unsigned char running, lock, last;
    u32              T, accum;
    int              mode, amode, rtc, diag;
    u32              achunk;
    unsigned         low, sub;          /* sub: synthesis sub-block size (samples) between interrupt windows, 0 = no windows */
    pcspk_pcm_cb     cb;
    void (__interrupt __far *prev)(void);
    pcspk_stats_t    st;
} P;

static u32 g_gain = 256;                /* 8.8 fixed point (256 = x1.0); an AGC experiment was removed again: it sounded worse in 86Box listening tests */
static unsigned char g_istack[ISTACK + 2];
static unsigned char g_wstack[WSTACK + 2];   /* second private stack: OPL writes that arrive while synthesis is suspended in an interrupt window */
static unsigned sv2_ss, sv2_sp;
static void (*s_fn)(void);              /* function to run on the private stack */
static unsigned w_hi, w_reg, w_val;
static unsigned char rtc_a, rtc_b, pic_m, pic_s;

/* generate n (<= CHUNK) samples at wr (yld: ISR synthesis, split into sub-blocks with interrupt windows in between).
 * Caller guarantees exclusive access to the OPL engine.
 * 16-bit conversion only: 8-bit precision is plenty for <= 144 PWM levels. */
/* Interrupt window used between synthesis sub-blocks (ISR synthesis only). Synthesis runs with IF=0, which would delay the next sample
 * IRQ by the whole block time (many sample periods on slow CPUs) and make the PWM output jitter ("wobble"). Between sub-blocks we
 * switch back to the interrupted program's stack (sv_ss:sv_sp; everything below it is dead), open IF for one instruction so pending
 * IRQs (our own sample IRQ first of all) are serviced there, close it again and return to the private stack. Engine state is
 * consistent at sub-block boundaries (verified: PCM is identical for any block size), OPL writes arriving in a window use g_wstack. */
static unsigned sv_ss, sv_sp, y_ss, y_sp;
static void yield_irq(void)
{
    _asm {
        push ax
        push bx
        mov  y_ss, ss
        mov  y_sp, sp
        mov  ax, sv_ss
        mov  bx, sv_sp
        mov  ss, ax
        mov  sp, bx
        sti
        nop
        cli
        mov  ax, y_ss
        mov  bx, y_sp
        mov  ss, ax
        mov  sp, bx
        pop  bx
        pop  ax
    }
}

static void gen_n(u32 n, int yld)
{
    s16 tmp[CHUNK];
    unsigned i, span = (unsigned)(P.T - 2), cnt = (unsigned)n, done = 0, sub;
    unsigned w = P.wr;
    while (done < cnt) {
        sub = cnt - done;
        if (yld && P.sub && sub > P.sub) sub = P.sub;
        P.cb(tmp, (u32)sub);
        if (g_gain != 256) {
            for (i = 0; i < sub; i++) {
                long s = ((long)tmp[i] * (long)g_gain) >> 8;
                if (s > 32767L) { s = 32767L; P.st.clips++; } else if (s < -32768L) { s = -32768L; P.st.clips++; }
                tmp[i] = (s16)s;
            }
        }
        if (P.mode == 0) {
            for (i = 0; i < sub; i++) {
                unsigned level = (((unsigned)((tmp[i] >> 8) + 128)) * span) >> 8;
                P.ring[(w + done + i) & MASK] = (unsigned char)(span - level);
            }
        } else {
            for (i = 0; i < sub; i++) {
                unsigned level = (((unsigned)((tmp[i] >> 8) + 128)) * span) >> 8;
                P.ring[(w + done + i) & MASK] = (unsigned char)(2 + level);
            }
        }
        done += sub;
        P.wr = w + done;
        if (yld && done < cnt) { yield_irq(); P.st.yields++; }
    }
}
static void gen_chunk(void)  { gen_n(CHUNK, 0); }
static void assist_body(void) { gen_n(P.achunk, 1); }
static void write_body(void)
{
    oplc_write_port(w_hi ? 2 : 0, (u8)w_reg);
    oplc_write_port(w_hi ? 3 : 1, (u8)w_val);
}

/* Run s_fn on the private stack (SS==DS==DGROUP so near pointers stay valid). MUST be entered with IF=0.
 * No locals / params here: after the stack switch, BP-relative accesses would hit the wrong stack. */
static void run_private(void)
{
    _asm {
        mov  sv_ss, ss
        mov  sv_sp, sp
        mov  ax, ds
        mov  ss, ax
        mov  sp, offset g_istack
        add  sp, 768                    /* == ISTACK (a C macro is not visible inside _asm) */
    }
    s_fn();
    _asm {
        mov  ss, sv_ss
        mov  sp, sv_sp
    }
}

/* Same, on the second stack with its own save slots: used when an OPL write arrives while the ISR synthesis is suspended in an interrupt window */
static void run_private2(void)
{
    _asm {
        mov  sv2_ss, ss
        mov  sv2_sp, sp
        mov  ax, ds
        mov  ss, ax
        mov  sp, offset g_wstack
        add  sp, 512                    /* == WSTACK */
    }
    s_fn();
    _asm {
        mov  ss, sv2_ss
        mov  sp, sv2_sp
    }
}

/* diagnostics: has somebody reprogrammed PIT channel 0 (our sample clock)? */
static void pit0_probe(void)
{
    unsigned lo, hi, cnt; unsigned char stt;
    outp(0x43, 0x00); lo = inp(0x40); hi = inp(0x40);        /* latch + read counter 0 */
    cnt = (hi << 8) | lo;
    outp(0x43, 0xE2); stt = (unsigned char)inp(0x40);        /* read-back: latch STATUS of counter 0 */
    P.st.pit0_samples++;
    if (cnt > P.st.pit0_max) P.st.pit0_max = cnt;
    P.st.pit0_status = stt;
    if (cnt > (unsigned)P.T + 2 || (stt & 0x3E) != 0x36) P.st.pit0_bad++;
}

/* speaker guard: games that drive the PC speaker themselves (beeper SFX, "silence" on exit of a sound routine) clear the
 * speaker gate (port 61h bits 0-1) or reprogram PIT ch2. Without this the PWM stays silent although synthesis runs fine. */
static void guard(void)
{
    unsigned char v, s;
    P.st.guard_checks++;
    v = (unsigned char)inp(0x61);
    if ((v & 3) != 3) { outp(0x61, (unsigned char)((v & 0x0F) | 3)); P.st.fix61++; P.st.last61 = v; }
    outp(0x43, 0xE8); s = (unsigned char)inp(0x42);          /* read-back: status of counter 2 */
    if (P.mode == 0 && (s & 0x3F) != 0x10) { outp(0x43, 0x90); outp(0x42, P.last); P.st.fixpit2++; P.st.last_pit2 = s; }
    if (P.rtc && (P.st.guard_checks & 15) == 0) {
        outp(0x70, 0x0A); v = (unsigned char)inp(0x71);
        if ((v & 0x0F) != 3) { outp(0x70, 0x0A); outp(0x71, (unsigned char)((v & 0xF0) | 3)); P.st.rtc_off++; }
    }
}

static void rate_probe(void)
{
    static unsigned long prev;
    unsigned long now = *(volatile unsigned long far *)MK_FP(0x40, 0x6C), d;
    if (prev) {
        d = now - prev;
        if (now < prev) d = 0;                                  /* midnight rollover: skip */
        else {
            P.st.rate_batches++; P.st.rate_sum += d;
            if (d > P.st.rate_max) P.st.rate_max = d;
            if (!P.st.rate_min || d < P.st.rate_min) P.st.rate_min = d;
        }
    }
    prev = now;
}

static void __interrupt __far pcspk_isr(void)
{
    unsigned char c; int chain, need;
    if (!P.running) { outp(0x20, 0x20); return; }
    if (P.wr != P.rd) { c = P.ring[P.rd & MASK]; P.rd++; P.last = c; }
    else { c = P.last; P.st.underruns++; }
    outp(0x42, c);
    P.st.isr_count++;

    P.accum += P.T;
    chain = (P.accum >= 0x10000UL);
    if (chain) P.accum -= 0x10000UL;
    need = P.amode && !P.lock && (unsigned)(P.wr - P.rd) < P.low;

    if (need) { P.lock = 1; s_fn = assist_body; run_private(); P.lock = 0; P.st.assists++; }
    if ((P.st.isr_count & 15) == 0) guard();
    if (P.diag && (P.st.isr_count & 0xFF) == 0) pit0_probe();      /* ~60 probes/s at 16 kHz; still sparse if a game slows IRQ0 down */
    if (chain) _chain_intr(P.prev);                         /* does not return; the BIOS handler sends the EOI */
    outp(0x20, 0x20);
}

/* RTC periodic interrupt (IRQ8 -> INT 70h): fixed 8192 Hz, independent of PIT ch0 / INT 8 */
static void __interrupt __far pcspk_rtc_isr(void)
{
    unsigned char c; int need = 0;
    outp(0x70, 0x0C); (void)inp(0x71);                      /* read status register C: acknowledges the RTC interrupt */
    if (P.running) {
        if (P.wr != P.rd) { c = P.ring[P.rd & MASK]; P.rd++; P.last = c; }
        else { c = P.last; P.st.underruns++; }
        outp(0x42, c);
        P.st.isr_count++;
        if ((P.st.isr_count & 0x1FFF) == 0) rate_probe();
        need = P.amode && !P.lock && (unsigned)(P.wr - P.rd) < P.low;
    }
    outp(0xA0, 0x20); outp(0x20, 0x20);                     /* EOI first (slave then master): the synthesis below opens interrupt windows, and the next RTC IRQ must be able to fire in them */
    if (P.running) {
        if (need) { P.lock = 1; s_fn = assist_body; run_private(); P.lock = 0; P.st.assists++; }
        if ((P.st.isr_count & 15) == 0) guard();
    }
}

static unsigned char cmos_rd(unsigned char r) { outp(0x70, r); return (unsigned char)inp(0x71); }
static void cmos_wr(unsigned char r, unsigned char v) { outp(0x70, r); outp(0x71, v); }

static int open_common(u32 rate, int mode, pcspk_pcm_cb cb, int use_rtc)
{
    if (P.running || !cb) return 0;
    memset(&P, 0, sizeof P);
    memset(g_istack, 0xA5, sizeof g_istack);
    memset(g_wstack, 0xA5, sizeof g_wstack);
    P.T = 1193182UL / rate; P.mode = mode; P.cb = cb; P.amode = 1; P.achunk = ACHUNK; P.low = LOW; P.rtc = use_rtc;
    P.sub = use_rtc ? SUB_DEF : 0;                          /* interrupt windows only with the RTC clock (PIT/INT 8 chaining path unchanged) */
    if (P.T < 16 || P.T > 250) return 0;
    P.st.T = P.T; P.st.levels = P.T - 1; P.st.clock_rtc = use_rtc;
    while ((unsigned)(P.wr - P.rd) <= RING - CHUNK) gen_chunk();     /* prefill */
    P.last = (unsigned char)((P.T - 2) / 2);
    return 1;
}

int pcspk_open(u32 rate, int mode, pcspk_pcm_cb cb)
{
    unsigned fl;
    if (rate < 4000 || rate > 44100) return 0;
    if (!open_common(rate, mode, cb, 0)) return 0;
    P.prev = _dos_getvect(0x08);
    _dos_setvect(0x08, pcspk_isr);
    outp(0x61, inp(0x61) | 0x03);
    fl = irq_save();
    outp(0x43, mode == 0 ? 0x90 : 0x96);
    outp(0x42, P.last);
    outp(0x43, 0x36);
    outp(0x40, (unsigned char)(P.T & 0xFF)); outp(0x40, (unsigned char)(P.T >> 8));
    P.running = 1;
    irq_restore(fl);
    return 1;
}

int pcspk_open_rtc(int mode, pcspk_pcm_cb cb)
{
    unsigned fl;
    if (!open_common(RTC_RATE, mode, cb, 1)) return 0;
    P.prev = _dos_getvect(0x70);
    _dos_setvect(0x70, pcspk_rtc_isr);
    outp(0x61, inp(0x61) | 0x03);
    fl = irq_save();
    outp(0x43, mode == 0 ? 0x90 : 0x96);                    /* PIT channel 2 only - channel 0 is NOT touched */
    outp(0x42, P.last);
    P.running = 1;
    rtc_a = cmos_rd(0x0A); rtc_b = cmos_rd(0x0B);
    cmos_wr(0x0A, (unsigned char)((rtc_a & 0xF0) | 0x03));  /* rate select 3 = 8192 Hz */
    (void)cmos_rd(0x0C);
    cmos_wr(0x0B, (unsigned char)(rtc_b | 0x40));           /* periodic interrupt enable */
    pic_m = (unsigned char)inp(0x21); pic_s = (unsigned char)inp(0xA1);
    outp(0xA1, (unsigned char)(pic_s & ~0x01));             /* unmask IRQ8 */
    outp(0x21, (unsigned char)(pic_m & ~0x04));             /* and the cascade (IRQ2) */
    irq_restore(fl);
    return 1;
}

void pcspk_set_diag(int on) { P.diag = on; }

void pcspk_call_private(void (*fn)(void))
{
    unsigned fl = irq_save();
    s_fn = fn;
    if (P.lock) run_private2(); else run_private();        /* P.lock set: ISR synthesis is suspended in an interrupt window on the first stack */
    irq_restore(fl);
}

/* OPL register write from foreground / trap code: IF=0 (so the ISR cannot interleave) + private stack. */
void pcspk_oplwrite(unsigned hi, unsigned reg, unsigned val)
{
    w_hi = hi; w_reg = reg; w_val = val;
    pcspk_call_private(write_body);
}

void pcspk_lock(void)   { unsigned fl; for (;;) { fl = irq_save(); if (!P.lock) { P.lock = 1; irq_restore(fl); return; } irq_restore(fl); } }
void pcspk_unlock(void) { P.lock = 0; }
void pcspk_set_assist(int amode, u32 achunk)
{
    P.amode = amode ? 1 : 0;
    P.achunk = (achunk >= 8 && achunk <= CHUNK) ? achunk : ACHUNK;
}
void pcspk_set_water(unsigned low) { P.low = (low >= 64 && low < RING) ? low : LOW; }
void pcspk_set_gain(u32 gain_256) { g_gain = gain_256 ? gain_256 : 256; }

void pcspk_pump(void)
{
    if (!P.running) return;
    while ((unsigned)(P.wr - P.rd) <= RING - CHUNK) {
        pcspk_lock();
        if ((unsigned)(P.wr - P.rd) <= RING - CHUNK) { gen_chunk(); P.st.refills++; }
        pcspk_unlock();
    }
}

/* 1 if our vector (INT 8, or INT 70h in RTC mode) still points at our ISR (so pcspk_try_close() would succeed) */
int pcspk_is_top(void)
{
    void (__interrupt __far *cur)(void) = _dos_getvect(P.rtc ? 0x70 : 0x08);
    return cur == (P.rtc ? pcspk_rtc_isr : pcspk_isr);
}

/* Unhook our vector and restore PIT/RTC/speaker. Refuses (returns 1) if someone hooked the vector after us. */
int pcspk_try_close(void)
{
    unsigned fl;
    if (!P.prev) return 0;
    if (!pcspk_is_top()) return 1;
    fl = irq_save();
    P.running = 0;
    if (P.rtc) {
        cmos_wr(0x0B, rtc_b); cmos_wr(0x0A, rtc_a); (void)cmos_rd(0x0C);
        outp(0xA1, (unsigned char)((inp(0xA1) & ~0x01) | (pic_s & 0x01)));
        _dos_setvect(0x70, P.prev);
    } else {
        outp(0x21, inp(0x21) | 0x01);
        _dos_setvect(0x08, P.prev);
        outp(0x43, 0x36); outp(0x40, 0); outp(0x40, 0);
    }
    outp(0x61, inp(0x61) & ~0x03);
    outp(0x43, 0xB6); outp(0x42, 0); outp(0x42, 0);
    if (!P.rtc) outp(0x21, inp(0x21) & ~0x01);
    P.prev = 0;
    irq_restore(fl);
    return 0;
}
void pcspk_close(void) { pcspk_try_close(); }

const pcspk_stats_t *pcspk_stats(void)
{
    unsigned i = 0;
    while (i < sizeof g_istack && g_istack[i] == 0xA5) i++;
    P.st.istack_used = sizeof g_istack - i;
    i = 0;
    while (i < sizeof g_wstack && g_wstack[i] == 0xA5) i++;
    P.st.wstack_used = sizeof g_wstack - i;
    P.st.sub = P.sub;
    return &P.st;
}
void pcspk_set_sub(unsigned sub) { if (P.rtc) P.sub = (sub > CHUNK) ? CHUNK : sub; }

