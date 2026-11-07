/* Test client for the resident EOPL TSR.
 *   eopltest <seconds>        "game" that writes OPL registers through the INT 2Fh API and burns CPU (no pump anywhere).
 *                             Run once BEFORE installing EOPL as a baseline (loops/s), then again with EOPL installed.
 *   eopltest trap [seconds]   simulates QEMM's I/O callback: calls the resident trap handler exactly like QEMM does
 *                             (DX=port, CX=flags bit2=OUT, AX=data, user CS:IP in the stack frame) and runs
 *                             (1) the standard AdLib detection sequence, (2) IN-instruction patching, (3) playback via port writes.
 *                             Works with `EOPL /NOTRAP` in DOSBox-X (no QEMM needed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <conio.h>
#include <i86.h>

#define SIG_CX 0x4F45
#define SIG_DX 0x4C50
#define TICKS (*(volatile unsigned long far *)MK_FP(0x40, 0x6C))

typedef struct {                 /* must match pcspk_stats_t in src/pcspk.h */
    unsigned long T, isr_count, underruns, refills, assists, clips, istack_used, levels;
} stats_t;
typedef struct {                 /* must match eopl_info_t in src/eopl.c */
    unsigned trap_mode, patch_enabled;
    unsigned long writes, reads, status_reads, patched;
    unsigned last_addr;
    unsigned diag;
    unsigned long w388, w389, keyon, sb_access;
    unsigned sb_ports, ring_pos;
    unsigned char reg_seen[32], ring_a[32], ring_v[32];
} info_t;
typedef struct {                 /* must match pcspk_stats_t in src/pcspk.h (u32 fields) */
    unsigned long T, isr_count, underruns, refills, assists, clips, istack_used, levels;
    unsigned long clock_rtc, pit0_samples, pit0_max, pit0_status, pit0_bad;
} stats2_t;

static volatile int g_abort;
static void __interrupt __far brk_isr(void) { g_abort = 1; }

static int mx_id = -1;

static void call2f(unsigned char fn, union REGS *i, union REGS *o, struct SREGS *s)
{
    i->h.ah = (unsigned char)mx_id; i->h.al = fn;
    segread(s);
    int86x(0x2F, i, o, s);
}
static int find_id(void)
{
    unsigned id; union REGS i, o; struct SREGS s;
    for (id = 0xC0; id <= 0xFF; id++) {
        i.h.ah = (unsigned char)id; i.h.al = 0; i.x.bx = 0; i.x.cx = 0; i.x.dx = 0;
        segread(&s); int86x(0x2F, &i, &o, &s);
        if (o.h.al == 0xFF && o.x.cx == SIG_CX && o.x.dx == SIG_DX) return (int)id;
    }
    return -1;
}
static void __far *get_far(unsigned char fn, unsigned *cx_out)
{
    union REGS i, o; struct SREGS s;
    i.x.bx = 0; i.x.cx = 0; i.x.dx = 0;
    call2f(fn, &i, &o, &s);
    if (cx_out) *cx_out = o.x.cx;
    return MK_FP(s.es, o.x.bx);
}
static void opl(unsigned bank, unsigned reg, unsigned val)
{
    union REGS i, o; struct SREGS s;
    i.h.bh = (unsigned char)bank; i.h.bl = (unsigned char)reg; i.h.cl = (unsigned char)val; i.h.ch = 0; i.x.dx = 0;
    call2f(1, &i, &o, &s);
}

/* ---------- simulated QEMM callback ---------- */
static void __far *g_handler;
static unsigned fake_qemm(unsigned port, unsigned flags, unsigned ax_in, unsigned ip, unsigned cs_user)
{
    unsigned r;
    void __far *h = g_handler;
    _asm {
        push cs_user                 /* [entry sp+0Ah] interrupted CS */
        push ip                      /* [entry sp+08h] interrupted IP */
        push 0                       /* filler (QEMM's own frame words) */
        push 0
        mov  ax, ax_in
        mov  cx, flags
        mov  dx, port
        call dword ptr h
        add  sp, 8
        mov  r, ax
    }
    return r;
}
#define OUT_PORT(p, v)   fake_qemm((p), 4, (v), 0, 0)
static unsigned in_port(unsigned p, unsigned char far *code_after_in)   /* ip points right after the IN instruction */
{
    return fake_qemm(p, 0, 0, FP_OFF(code_after_in), FP_SEG(code_after_in));
}
static void adlib_write(unsigned reg, unsigned val) { OUT_PORT(0x388, reg); OUT_PORT(0x389, val); }

static int trap_selftest(const info_t __far *in)
{
    static unsigned char code[4] = { 0xEC, 0x90, 0x90, 0x90 };       /* IN AL,DX */
    unsigned s1, s2, ok = 1;
    unsigned char far *after = (unsigned char far *)&code[1];
    printf("--- AdLib detection sequence (as a game does it) ---\n");
    adlib_write(4, 0x60); adlib_write(4, 0x80);                          /* reset timers + IRQ */
    s1 = in_port(0x388, after) & 0xFF;
    adlib_write(2, 0xFF); adlib_write(4, 0x21);                          /* timer 1 = 0xFF, start timer 1 */
    s2 = in_port(0x388, after) & 0xFF;
    adlib_write(4, 0x60); adlib_write(4, 0x80);
    printf("  status after reset         = %02X  (expected (s & E0) == 00)  %s\n", s1, (s1 & 0xE0) == 0x00 ? "OK" : "FAIL");
    printf("  status after timer1 start  = %02X  (expected (s & E0) == C0)  %s\n", s2, (s2 & 0xE0) == 0xC0 ? "OK" : "FAIL");
    if ((s1 & 0xE0) != 0x00 || (s2 & 0xE0) != 0xC0) ok = 0;
    printf("  => OPL2 %s\n", ok ? "DETECTED" : "NOT detected");

    printf("--- run-time patching of IN AL,DX ---\n");
    adlib_write(0x01, 0x20);                                              /* address < 0x20 right now: must NOT patch */
    OUT_PORT(0x388, 0x01);
    in_port(0x388, after);
    printf("  IN opcode with address 01h : %02X (expected EC, not patched)  %s\n", code[0], code[0] == 0xEC ? "OK" : "FAIL");
    if (code[0] != 0xEC) ok = 0;
    OUT_PORT(0x388, 0xA0);                                                /* address >= 0x20: patch */
    in_port(0x388, after);
    printf("  IN opcode with address A0h : %02X (expected 90 if patching is on)  %s\n", code[0],
           (in->patch_enabled ? code[0] == 0x90 : code[0] == 0xEC) ? "OK" : "FAIL");
    if (in->patch_enabled ? code[0] != 0x90 : code[0] != 0xEC) ok = 0;
    if (in->diag) {
        unsigned r;
        printf("--- diag: Sound Blaster probing (ports 220h-22Fh) ---\n");
        OUT_PORT(0x226, 1); OUT_PORT(0x226, 0);
        r = in_port(0x22A, after) & 0xFF;
        printf("  DSP read port 22Ah returns %02X (expected FF = no Sound Blaster)  %s\n", r, r == 0xFF ? "OK" : "FAIL");
        if (r != 0xFF) ok = 0;
    }
    return ok;
}

static void setup_patch_ports(void)
{
    static const unsigned char opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
    int c;
    adlib_write(0x01, 0x20);
    for (c = 0; c < 9; c++) {
        unsigned m = opm[c];
        adlib_write(0x20 + m, 0x01); adlib_write(0x40 + m, 0x1C); adlib_write(0x60 + m, 0xF0); adlib_write(0x80 + m, 0x77);
        adlib_write(0x23 + m, 0x01); adlib_write(0x43 + m, 0x08); adlib_write(0x63 + m, 0xC2); adlib_write(0x83 + m, 0x15);
        adlib_write(0xC0 + c, 0x00);
    }
}
static void chord_ports(int step)
{
    static const unsigned fn[7] = { 0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5 };
    int c;
    for (c = 0; c < 9; c++) {
        unsigned f = fn[(step * 2 + c) % 7];
        adlib_write(0xB0 + c, 0x10 | (f >> 8));
        adlib_write(0xA0 + c, f & 0xFF);
        adlib_write(0xB0 + c, 0x30 | (f >> 8));
    }
}

/* ---------- API-mode "game" (baseline / CPU share) ---------- */
static void setup_patch_api(void)
{
    static const unsigned char opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
    int c;
    opl(0, 0x01, 0x20);
    for (c = 0; c < 9; c++) {
        unsigned m = opm[c];
        opl(0, 0x20 + m, 0x01); opl(0, 0x40 + m, 0x1C); opl(0, 0x60 + m, 0xF0); opl(0, 0x80 + m, 0x77);
        opl(0, 0x23 + m, 0x01); opl(0, 0x43 + m, 0x08); opl(0, 0x63 + m, 0xC2); opl(0, 0x83 + m, 0x15);
        opl(0, 0xC0 + c, 0x00);
    }
}
static void chord_api(int step)
{
    static const unsigned fn[7] = { 0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5 };
    int c;
    for (c = 0; c < 9; c++) {
        unsigned f = fn[(step * 2 + c) % 7];
        opl(0, 0xB0 + c, 0x10 | (f >> 8));
        opl(0, 0xA0 + c, f & 0xFF);
        opl(0, 0xB0 + c, 0x30 | (f >> 8));
    }
}

/* ---------- PIT ch0 reprogramming detection test (needs: EOPL /DIAG with the PIT clock) ---------- */
static unsigned char cmos_sec(void) { outp(0x70, 0); return (unsigned char)inp(0x71); }
static void wait_secs(unsigned n)            /* RTC seconds as time base: BIOS ticks depend on the (deliberately broken) PIT clock here */
{
    unsigned char last = cmos_sec(); unsigned got = 0;
    while (got < n && !g_abort) { unsigned char s = cmos_sec(); if (s != last) { last = s; got++; } }
}
static int pit_test(void)
{
    const stats2_t __far *st; const info_t __far *in; unsigned rate;
    mx_id = find_id();
    if (mx_id < 0) { printf("EOPL not installed (install it with /DIAG)\n"); return 2; }
    st = (const stats2_t __far *)get_far(2, &rate); in = (const info_t __far *)get_far(4, 0);
    if (!in->diag || st->clock_rtc) { printf("needs 'EOPL /DIAG' with the PIT clock (not /RTC)\n"); return 3; }
    wait_secs(2);
    printf("before: probes=%lu  max counter=%lu (our divisor %lu)  status=%02lX  reprogrammed samples=%lu\n",
           st->pit0_samples, st->pit0_max, st->T, st->pit0_status, st->pit0_bad);
    printf("reprogramming PIT ch0 to divisor 2000h (what a game does for its own timer) ...\n");
    outp(0x43, 0x36); outp(0x40, 0x00); outp(0x40, 0x20);
    wait_secs(4);
    printf("after : probes=%lu  max counter=%lu  status=%02lX  reprogrammed samples=%lu  => %s\n",
           st->pit0_samples, st->pit0_max, st->pit0_status, st->pit0_bad, st->pit0_bad ? "REPROGRAMMING DETECTED" : "not detected (FAIL)");
    outp(0x43, 0x36); outp(0x40, (unsigned char)(st->T & 0xFF)); outp(0x40, (unsigned char)(st->T >> 8));   /* restore our clock */
    return st->pit0_bad ? 0 : 1;
}

int main(int argc, char **argv)
{
    unsigned long secs, start, last_show, last_step = 0, loops = 0, lps_total = 0, shows = 0, loops_at_show = 0;
    int step = 0, trap_mode = 0;
    const stats_t __far *st = 0;
    const info_t __far *in = 0;
    void (__interrupt __far *old23)(void) = _dos_getvect(0x23), (__interrupt __far *old1b)(void) = _dos_getvect(0x1B);

    if (argc < 2) { printf("usage: eopltest <seconds>\n       eopltest trap [seconds]\n       eopltest pit\n"); return 1; }
    if (strcmp(argv[1], "pit") == 0) return pit_test();
    trap_mode = (strcmp(argv[1], "trap") == 0);
    secs = (unsigned long)atol(argv[trap_mode ? (argc > 2 ? 2 : 1) : 1]);
    if (trap_mode && argc <= 2) secs = 6;
    _dos_setvect(0x23, brk_isr); _dos_setvect(0x1B, brk_isr);

    mx_id = find_id();
    if (mx_id >= 0) {
        unsigned rate;
        st = (const stats_t __far *)get_far(2, &rate);
        in = (const info_t __far *)get_far(4, 0);
        printf("EOPL found (multiplex id %02Xh), rate %u Hz, trap mode %u, patching %s.\n", mx_id, rate, in->trap_mode, in->patch_enabled ? "on" : "off");
        if (trap_mode) {
            g_handler = get_far(5, 0);
            printf("simulated QEMM callback -> handler at %04X:%04X\n", FP_SEG(g_handler), FP_OFF(g_handler));
            if (!trap_selftest(in)) printf("*** SELF TEST FAILED ***\n"); else printf("self test passed.\n");
            setup_patch_ports(); chord_ports(0);
            printf("Playing chords through the trap handler (AdLib port writes) for %lu s ...\n", secs);
        } else {
            printf("Playing chords via the API; NO pump in this program.\n");
            setup_patch_api(); chord_api(0);
        }
    } else {
        if (trap_mode) { printf("EOPL not installed (run EOPL /NOTRAP first).\n"); return 2; }
        printf("EOPL not installed -> BASELINE mode (CPU loop count only).\n");
    }

    start = TICKS; last_show = start;
    while (TICKS - start < (unsigned long)(secs * 18.2065) && !g_abort) {
        loops++;                                                     /* the "game": burn CPU */
        if (mx_id >= 0 && TICKS - last_step >= 5) { last_step = TICKS; if (trap_mode) chord_ports(++step); else chord_api(++step); }
        if (TICKS - last_show >= 18) {
            unsigned long dl = loops - loops_at_show;
            last_show = TICKS; loops_at_show = loops; lps_total += dl; shows++;
            if (st) printf("\r %2lus loops/s=%lu  UNDERRUN=%lu  ISR chunks=%lu  stack=%lu   ", (TICKS - start) * 55UL / 1000UL, dl, st->underruns, st->assists, st->istack_used);
            else    printf("\r %2lus loops/s=%lu   ", (TICKS - start) * 55UL / 1000UL, dl);
            fflush(stdout);
        }
        if (kbhit()) { getch(); break; }
    }
    printf("\naverage loops/s = %lu\n", shows ? lps_total / shows : 0UL);
    if (st) printf("UNDERRUN=%lu  ISR chunks=%lu  IRQ0=%lu  clip=%lu\n", st->underruns, st->assists, st->isr_count, st->clips);
    if (in && trap_mode) printf("trap: writes=%lu status reads=%lu patched IN=%lu\n", in->writes, in->status_reads, in->patched);
    _dos_setvect(0x23, old23); _dos_setvect(0x1B, old1b);
    return 0;
}
