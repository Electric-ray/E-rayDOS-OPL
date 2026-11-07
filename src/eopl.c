/* SPDX-License-Identifier: GPL-3.0-or-later */
/* E-ray DOS OPL Emulator - resident TSR (Phase 3b: QEMM port trapping + diagnostics)
 * AdLib (OPL2, ports 388h/389h) -> DBOPL 16-bit synthesis -> PC speaker PWM. ALL synthesis happens inside the sample-clock ISR.
 * Sample clock: PIT ch0 (default, any rate) or the RTC periodic interrupt (/RTC, 8192 Hz, leaves PIT ch0 / INT 8 untouched).
 * Port trapping (QEMM QPI, V86 callback; derived from ADLCOM, GPLv3) + status/timer emulation + IN AL,DX run-time patching.
 * Control API through INT 2Fh multiplex (id chosen at install, scanned like AMIS):
 *   AL=00 install check -> AL=FFh, BX=version, CX='EO'(4F45h), DX='PL'(4C50h)
 *   AL=01 OPL write      BH=bank(0/1), BL=register, CL=value        (test/diagnostic path)
 *   AL=02 status         ES:BX -> pcspk_stats_t, CX=sample rate, DX=resident PSP
 *   AL=03 uninstall      AL=0 ok / 1 vector hooked by someone else / 2 INT 2Fh hooked by someone else / 3 QEMM chain could not be unhooked
 *   AL=04 trap info      ES:BX -> eopl_info_t          AL=05  far address of the trap handler (self test)
 * usage: EOPL [/RTC (default)|/PIT|/R:rate|/R:AUTO] [/Y:subblock] [/G:gain] [/C:chunk] [/W:lowwater] [/NOPATCH] [/NOTRAP] [/DIAG]
 *        | /S status | /D diagnostics dump | /U uninstall */
#include <dos.h>
#include <i86.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include "opl_core.h"
#include "pcspk.h"

#define VER    0x0005
#define SIG_CX 0x4F45
#define SIG_DX 0x4C50

/* _psp (PSP segment) is declared in <stdlib.h> by the Watcom runtime */

typedef struct {                       /* returned by AL=04; NEW FIELDS ARE ONLY APPENDED (test programs read a prefix) */
    unsigned trap_mode;                /* 0 none (API only), 1 QEMM QPI */
    unsigned patch_enabled;
    unsigned long writes, reads, status_reads, patched;
    unsigned last_addr;
    /* --- diagnostics --- */
    unsigned diag;                     /* /DIAG: SB ports 220h-22Fh trapped + PIT ch0 monitoring */
    unsigned long w388, w389, keyon, sb_access;
    unsigned sb_ports;                 /* bit n = port 220h+n was accessed */
    unsigned ring_pos;
    unsigned char reg_seen[32];        /* bitmap: OPL registers that were written at least once */
    unsigned char ring_a[32], ring_v[32];   /* last 32 register writes */
    unsigned vec_ours;                 /* 1 if INT 8 / INT 70h still points at our ISR (refreshed by AL=02/04) */
} eopl_info_t;

static unsigned char mx_id;
static void (__interrupt __far *old2f)(void);
static unsigned my_psp, rate_sel, top_sp;
static eopl_info_t info;
static void __far *qpi_entry;          /* QEMM QPI entry point (valid when trap_mode == 1) */

#pragma pack(push, 1)
struct iisp_header {                   /* IBM Interrupt Sharing Protocol header (same as the one in trapglue.asm) */
    unsigned short jump_to_start;
    void __far *next_handler;
    unsigned short signature;
    char eoi_flag;
    char jump_to_reset[2];
    char reserved[7];
};
#pragma pack(pop)
extern void __cdecl __far qemm_handler(void);       /* trapglue.asm */

/* ---- console output without stdio (keeps the resident image small) ---- */
static void out(const char *s) { unsigned n = 0, w; while (s[n]) n++; _dos_write(1, s, n, &w); }
static void outnum(unsigned long v)
{
    char b[12]; int i = 11; b[i] = 0;
    if (!v) b[--i] = '0';
    while (v) { b[--i] = (char)('0' + (unsigned)(v % 10)); v /= 10; }
    out(b + i);
}
static void outhex2(unsigned v)
{
    char b[3]; static const char h[] = "0123456789ABCDEF";
    b[0] = h[(v >> 4) & 15]; b[1] = h[v & 15]; b[2] = 0; out(b);
}

static void gen_cb(s16 *d, u32 n) { oplc_generate_mono(d, n); }

/* ================= QEMM QPI wrappers (ADLCOM) ================= */
int qemm_get_qpi_entry_point(int handle, void __far **qpi_entry);
#pragma aux qemm_get_qpi_entry_point = "mov ax, 0x4402" "mov cx, 4" "int 0x21" "sbb ax, ax" parm [bx] [dx] modify [bx cx dx si di]
int qpi_get_version(void __far **qpi_entry);
#pragma aux qpi_get_version = "mov ah, 3" "call dword ptr [si]" parm [si] value [bx] modify [ax]
void __far *qpi_get_io_callback(void __far **qpi_entry);
#pragma aux qpi_get_io_callback = "mov ax, 0x1A06" "call dword ptr [si]" parm [si] value [es di] modify [ax]
void qpi_set_io_callback(void __far **qpi_entry, void __far *callback);
#pragma aux qpi_set_io_callback = "mov ax, 0x1A07" "call dword ptr [si]" parm [si] [es di] modify [ax dx]
char qpi_get_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_get_port_trap = "mov ax, 0x1A08" "call dword ptr [si]" parm [si] [dx] value [bl] modify [ax]
void qpi_set_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_set_port_trap = "mov ax, 0x1A09" "call dword ptr [si]" parm [si] [dx] modify [ax]
void qpi_clear_port_trap(void __far **qpi_entry, int port);
#pragma aux qpi_clear_port_trap = "mov ax, 0x1A0A" "call dword ptr [si]" parm [si] [dx] modify [ax]

/* ================= AdLib port emulation (runs on the driver's private stack, IF=0) ================= */
static unsigned t_port, t_flags, t_ax, t_ipoff, t_ipseg;
static unsigned char op_addr, op_timer, cfg_patch = 1;

static void log_write(unsigned char a, unsigned char v)
{
    info.ring_a[info.ring_pos & 31] = a; info.ring_v[info.ring_pos & 31] = v; info.ring_pos++;
    info.reg_seen[a >> 3] |= (unsigned char)(1u << (a & 7));
    if ((a & 0xF0) == 0xB0 && (a & 0x0F) < 9 && (v & 0x20)) info.keyon++;
}

static void trap_body(void)
{
    if (t_port >= 0x220 && t_port <= 0x22F) {               /* diagnostics: somebody is probing a Sound Blaster */
        info.sb_access++; info.sb_ports |= (unsigned)(1u << (t_port & 15));
        if (!(t_flags & 4)) t_ax |= 0xFF;                   /* IN: open bus */
        return;
    }
    if (t_flags & 4) {                                      /* OUT */
        info.writes++;
        if ((t_port & 1) == 0) { op_addr = (unsigned char)t_ax; info.last_addr = op_addr; info.w388++; }
        else {
            info.w389++;
            if (op_addr == 4) op_timer = (unsigned char)t_ax;   /* timer control: needed for the status emulation */
            log_write(op_addr, (unsigned char)t_ax);
            oplc_write_port(0, op_addr);
            oplc_write_port(1, (u8)t_ax);
        }
    } else {                                                /* IN: AdLib status register */
        unsigned ax = t_ax & ~0xFFu;
        info.reads++;
        if ((op_timer & 0xC1) == 1) ax |= 0xC0;             /* timer 1 running + not masked -> "expired" (OPL detection) */
        if ((op_timer & 0xA2) == 2) ax |= 0xA0;             /* timer 2 */
        ax |= 0x06;                                         /* OPL2 low bits */
        t_ax = ax;
        info.status_reads++;
        if (cfg_patch && op_addr >= 0x20) {                 /* after detection: NOP out the game's polling IN AL,DX (delay loops) */
            unsigned char __far *opc = (unsigned char __far *)MK_FP(t_ipseg, t_ipoff - 1);
            if (*opc == 0xEC) { *opc = 0x90; info.patched++; }
        }
    }
}

/* called from trapglue.asm (DS = DGROUP); work is done on the private stack with IF=0 */
unsigned __cdecl eopl_trap(unsigned port, unsigned flags, unsigned ax, unsigned ip_off, unsigned ip_seg)
{
    t_port = port; t_flags = flags; t_ax = ax; t_ipoff = ip_off; t_ipseg = ip_seg;
    pcspk_call_private(trap_body);
    return t_ax;
}

/* ================= QEMM chain management (resident, used by uninstall) ================= */
static int iisp_unhook(struct iisp_header __far *h, unsigned our_seg)
{
    for (;;) {
        struct iisp_header __far *next;
        if (h->jump_to_start != 0x10EB || h->signature != 0x424B || h->jump_to_reset[0] != (char)0xEB) return 0;
        next = (struct iisp_header __far *)h->next_handler;
        if (FP_SEG(next) == our_seg) { h->next_handler = next->next_handler; return 1; }
        h = next;
    }
}

/* returns 0 on success, 1 if the chain cannot be unhooked (nothing is changed in that case) */
static int qemm_remove(void)
{
    struct iisp_header __far *mine = (struct iisp_header __far *)qemm_handler;
    struct iisp_header __far *top = (struct iisp_header __far *)qpi_get_io_callback(&qpi_entry);
    int p;
    if (FP_SEG(top) == FP_SEG(mine)) qpi_set_io_callback(&qpi_entry, mine->next_handler);
    else if (!iisp_unhook(top, FP_SEG(mine))) return 1;
    qpi_clear_port_trap(&qpi_entry, 0x388);
    qpi_clear_port_trap(&qpi_entry, 0x389);
    if (info.diag) for (p = 0x220; p <= 0x22F; p++) qpi_clear_port_trap(&qpi_entry, p);
    info.trap_mode = 0;
    return 0;
}

/* ---- INT 2Fh API (resident) ---- */
static void __interrupt __far mx_handler(union INTPACK r)
{
    if (r.h.ah != mx_id) _chain_intr(old2f);
    switch (r.h.al) {
    case 0:  r.h.al = 0xFF; r.x.bx = VER; r.x.cx = SIG_CX; r.x.dx = SIG_DX; break;
    case 1:  pcspk_oplwrite(r.h.bh, r.h.bl, r.h.cl); r.h.al = 0; break;
    case 2: {
        const pcspk_stats_t *s = pcspk_stats();
        void __far *fp = (void __far *)s;
        r.x.es = FP_SEG(fp); r.x.bx = FP_OFF(fp); r.x.cx = rate_sel; r.x.dx = my_psp; r.h.al = 0;
        break; }
    case 4: {
        void __far *fp = (void __far *)&info;
        info.vec_ours = (unsigned)pcspk_is_top();
        r.x.es = FP_SEG(fp); r.x.bx = FP_OFF(fp); r.h.al = 0;
        break; }
    case 5: {                                           /* far address of the trap handler (for the simulated-QEMM self test) */
        void __far *fp = (void __far *)qemm_handler;
        r.x.es = FP_SEG(fp); r.x.bx = FP_OFF(fp); r.h.al = 0;
        break; }
    case 3: {
        void (__interrupt __far *cur)(void) = _dos_getvect(0x2F);
        r.x.es = my_psp;
        if (cur != (void (__interrupt __far *)(void))mx_handler) { r.h.al = 2; break; }
        if (!pcspk_is_top()) { r.h.al = 1; break; }
        if (info.trap_mode == 1 && qemm_remove()) { r.h.al = 3; break; }
        pcspk_try_close();
        _dos_setvect(0x2F, old2f);
        r.h.al = 0;
        break; }
    default: r.h.al = 0xFE; break;
    }
}

/* ---- transient helpers (install / status / uninstall) ---- */
static void call2f(unsigned char id, unsigned char fn, union REGS *o, struct SREGS *s)
{
    union REGS i;
    segread(s);
    i.h.ah = id; i.h.al = fn; i.x.bx = 0; i.x.cx = 0; i.x.dx = 0;
    int86x(0x2F, &i, o, s);
}
static int find_installed(void)           /* returns id or -1 */
{
    unsigned id; union REGS o; struct SREGS s;
    for (id = 0xC0; id <= 0xFF; id++) {
        call2f((unsigned char)id, 0, &o, &s);
        if (o.h.al == 0xFF && o.x.cx == SIG_CX && o.x.dx == SIG_DX) return (int)id;
    }
    return -1;
}
static int find_free(void)
{
    unsigned id; union REGS o; struct SREGS s;
    for (id = 0xC0; id <= 0xFF; id++) { call2f((unsigned char)id, 0, &o, &s); if (o.h.al == 0) return (int)id; }
    return -1;
}

/* install-time benchmark: highest rate whose worst-case (9 channels sustained) synthesis stays under ~47% of the CPU */
static void bw(unsigned reg, unsigned val) { oplc_write_port(0, (u8)reg); oplc_write_port(1, (u8)val); }
static unsigned pick_rate(void)
{
    static const unsigned char opm[9] = { 0x00,0x01,0x02,0x08,0x09,0x0A,0x10,0x11,0x12 };
    static const unsigned fn[7] = { 0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA, 0x1E5 };
    s16 buf[128];
    unsigned c; u32 t0, t1, done = 0, cap;
    volatile u32 far *ticks = (volatile u32 far *)MK_FP(0x40, 0x6C);

    oplc_init(OPLC_OPL2, 11025);
    bw(0x01, 0x20);
    for (c = 0; c < 9; c++) {
        unsigned m = opm[c], f = fn[(c * 2) % 7];
        bw(0x20 + m, 0x21); bw(0x40 + m, 0x1C); bw(0x60 + m, 0xF0); bw(0x80 + m, 0x77);
        bw(0x23 + m, 0x21); bw(0x43 + m, 0x08); bw(0x63 + m, 0xC2); bw(0x83 + m, 0x15);
        bw(0xC0 + c, 0x00); bw(0xA0 + c, f & 0xFF); bw(0xB0 + c, 0x30 | (f >> 8));
    }
    t0 = *ticks; while (*ticks == t0) { } t0 = *ticks;
    while (*ticks - t0 < 18) { oplc_generate_mono(buf, 128); done += 128; }
    t1 = *ticks - t0; if (!t1) t1 = 1;                      /* ~1 s */
    cap = (done * 18207UL) / (t1 * 1000UL);                 /* samples/s the CPU can synthesize in the worst case */
    cap = (cap * 47UL) / 100UL;                             /* leave about half of the CPU for the game */
    if (cap >= 22050UL) return 22050;
    if (cap >= 16000UL) return 16000;
    if (cap >= 11025UL) return 11025;
    return 8000;
}

static unsigned parse_num(const char *s) { unsigned v = 0; while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0'); return v; }
static int starts_with(const char *a, const char *b)       /* case-insensitive prefix test */
{ while (*b) { if (((*a) | 0x20) != ((*b) | 0x20)) return 0; a++; b++; } return 1; }

/* QEMM detection + trap installation. returns 1 installed, 0 not available (silent), -1 conflict/error (message printed) */
static int setup_qemm(int diag)
{
    int handle, ver, p;
    struct iisp_header __far *hdr;
    if (_dos_open("QEMM386$", O_RDONLY, &handle)) return 0;
    if (qemm_get_qpi_entry_point(handle, &qpi_entry)) { _dos_close(handle); return 0; }
    _dos_close(handle);
    ver = qpi_get_version(&qpi_entry);
    if (ver < 0x0703) { out("EOPL: QEMM 7.03 or later is required for port trapping.\r\n"); return -1; }
    if (qpi_get_port_trap(&qpi_entry, 0x388) || qpi_get_port_trap(&qpi_entry, 0x389)) {
        out("EOPL: another program is already intercepting the AdLib ports (ADLCOM?).\r\n"); return -1;
    }
    hdr = (struct iisp_header __far *)qemm_handler;
    hdr->next_handler = qpi_get_io_callback(&qpi_entry);
    qpi_set_io_callback(&qpi_entry, hdr);
    qpi_set_port_trap(&qpi_entry, 0x388);
    qpi_set_port_trap(&qpi_entry, 0x389);
    if (diag) for (p = 0x220; p <= 0x22F; p++) qpi_set_port_trap(&qpi_entry, p);
    info.trap_mode = 1;
    return 1;
}

/* ---- /S and /D (transient) ---- */
static int popcount_range(const unsigned char __far *bm, unsigned lo, unsigned hi)
{
    int n = 0; unsigned a;
    for (a = lo; a <= hi; a++) if (bm[a >> 3] & (1u << (a & 7))) n++;
    return n;
}
static void show_status(const pcspk_stats_t __far *st, const eopl_info_t __far *in, unsigned rate)
{
    out("EOPL resident. clock="); out(st->clock_rtc ? "RTC IRQ8" : "PIT ch0"); out("  rate="); outnum(rate);
    out(" Hz  PWM levels="); outnum(st->levels);
    out("\r\n  IRQ count="); outnum(st->isr_count); out("  underruns="); outnum(st->underruns);
    out("  ISR synthesis chunks="); outnum(st->assists); out("  ISR stack used="); outnum(st->istack_used);
    out("\r\n  AdLib trap: "); out(in->trap_mode == 1 ? "QEMM QPI" : "none (API only)");
    out("  patching: "); out(in->patch_enabled ? "on" : "off");
    out("\r\n  port writes="); outnum(in->writes); out("  status reads="); outnum(in->status_reads);
    out("  patched IN instructions="); outnum(in->patched); out("  last address="); outnum(in->last_addr);
    out("\r\n  AdLib: 388h writes="); outnum(in->w388); out("  389h writes="); outnum(in->w389); out("  note-on (B0-B8 bit5) writes="); outnum(in->keyon);
    out("\r\n  speaker guard: checks="); outnum(st->guard_checks); out("  port61h fixed="); outnum(st->fix61);
    out("  PIT ch2 fixed="); outnum(st->fixpit2); out("  RTC rate fixed="); outnum(st->rtc_off);
    if (st->fix61) { out("  last61="); outhex2((unsigned)st->last61); }
    if (st->fixpit2) { out("  lastch2="); outhex2((unsigned)st->last_pit2); }
    out("\r\n  synthesis sub-block="); outnum(st->sub); out(" samples  interrupt windows="); outnum(st->yields);
    out("  write-stack used="); outnum(st->wstack_used);
    if (st->rate_batches && st->rate_sum && st->rate_max && st->rate_min) {
        out("\r\n  measured IRQ rate: avg="); outnum(st->rate_batches * 149148UL / st->rate_sum);
        out(" Hz  slowest="); outnum(149148UL / st->rate_max);
        out(" Hz  fastest="); outnum(149148UL / st->rate_min);
        out(" Hz  (nominal 8192, BIOS-tick resolution ~5%, batches="); outnum(st->rate_batches); out(")");
    }
    out("\r\n  sample-clock vector still ours: "); out(in->vec_ours ? "yes" : "NO (a program took over INT 8/70h)");
    if (in->diag) {
        out("\r\n  [diag] Sound Blaster port accesses (220h-22Fh)="); outnum(in->sb_access); out("  ports seen mask="); outhex2(in->sb_ports >> 8); outhex2(in->sb_ports);
        if (!st->clock_rtc) {
            out("\r\n  [diag] PIT ch0 samples="); outnum(st->pit0_samples); out("  max counter seen="); outnum(st->pit0_max);
            out(" (our divisor="); outnum(st->T); out(")  status="); outhex2(st->pit0_status); out("  REPROGRAMMED samples="); outnum(st->pit0_bad);
        }
    }
    out("\r\n");
}
static void show_dump(const eopl_info_t __far *in)
{
    unsigned n, i, start;
    out("OPL registers written (distinct): ");
    outnum(popcount_range(in->reg_seen, 0x20, 0x35)); out(" mult/ksr(20-35), ");
    outnum(popcount_range(in->reg_seen, 0x40, 0x55)); out(" level(40-55), ");
    outnum(popcount_range(in->reg_seen, 0x60, 0x75)); out(" attack(60-75), ");
    outnum(popcount_range(in->reg_seen, 0x80, 0x95)); out(" sustain(80-95),\r\n  ");
    outnum(popcount_range(in->reg_seen, 0xA0, 0xA8)); out(" freq(A0-A8), ");
    outnum(popcount_range(in->reg_seen, 0xB0, 0xB8)); out(" key-on/block(B0-B8), ");
    outnum(popcount_range(in->reg_seen, 0xC0, 0xC8)); out(" feedback(C0-C8), ");
    outnum(popcount_range(in->reg_seen, 0xE0, 0xF5)); out(" waveform(E0-F5), ");
    outnum(popcount_range(in->reg_seen, 0xBD, 0xBD)); out(" rhythm(BD)\r\n");
    out("last register writes (addr:value, oldest first): ");
    n = in->ring_pos > 32 ? 32 : in->ring_pos;
    start = in->ring_pos > 32 ? in->ring_pos - 32 : 0;
    for (i = 0; i < n; i++) {
        if (i % 8 == 0) out("\r\n  ");
        outhex2(in->ring_a[(start + i) & 31]); out(":"); outhex2(in->ring_v[(start + i) & 31]); out(" ");
    }
    out("\r\n");
}

int main(int argc, char **argv)
{
    int i, id, do_u = 0, do_s = 0, do_d = 0, notrap = 0, use_rtc = 0, diag = 0, want_pit = 0;
    unsigned rate = 0, chunk = 0, water = 0, paras, dg, env, ysub = 0, ysub_set = 0, gain = 0;
    union REGS o; struct SREGS sr;

    _asm { mov top_sp, sp }
    my_psp = _psp;
    info.patch_enabled = 1;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] != '/' && a[0] != '-') continue;
        if (starts_with(a + 1, "nopatch")) { cfg_patch = 0; info.patch_enabled = 0; continue; }
        if (starts_with(a + 1, "notrap")) { notrap = 1; continue; }
        if (starts_with(a + 1, "diag")) { diag = 1; continue; }
        if (starts_with(a + 1, "rtc")) { use_rtc = 1; continue; }
        if (starts_with(a + 1, "pit")) { want_pit = 1; continue; }
        switch (a[1] | 0x20) {
        case 'u': do_u = 1; break;
        case 's': do_s = 1; break;
        case 'd': do_d = 1; break;
        case 'r': want_pit = 1; rate = ((a[3] | 0x20) == 'a') ? 0 : parse_num(a + 3); break;
        case 'c': chunk = parse_num(a + 3); break;
        case 'w': water = parse_num(a + 3); break;
        case 'y': ysub = parse_num(a + 3); ysub_set = 1; break;
        case 'g': gain = parse_num(a + 3); break;
        }
    }
    id = find_installed();
    if (!want_pit && !rate) use_rtc = 1;                    /* default: RTC clock (games reprogram PIT ch0); /PIT or /R:n selects the PIT clock */

    if (do_u) {
        if (id < 0) { out("EOPL: not installed.\r\n"); return 1; }
        call2f((unsigned char)id, 3, &o, &sr);
        if (o.h.al == 0) { _dos_freemem(sr.es); out("EOPL: uninstalled, memory freed.\r\n"); return 0; }
        out(o.h.al == 1 ? "EOPL: cannot uninstall - the timer interrupt was hooked by another program after us.\r\n" :
            o.h.al == 2 ? "EOPL: cannot uninstall - INT 2Fh was hooked by another program after us.\r\n" :
                          "EOPL: cannot uninstall - the QEMM I/O callback chain could not be unhooked.\r\n");
        return 2;
    }
    if (do_s || do_d) {
        const pcspk_stats_t __far *st; const eopl_info_t __far *in; unsigned r;
        if (id < 0) { out("EOPL: not installed.\r\n"); return 1; }
        call2f((unsigned char)id, 2, &o, &sr);
        st = (const pcspk_stats_t __far *)MK_FP(sr.es, o.x.bx); r = o.x.cx;
        call2f((unsigned char)id, 4, &o, &sr);
        in = (const eopl_info_t __far *)MK_FP(sr.es, o.x.bx);
        if (do_s) show_status(st, in, r);
        if (do_d) show_dump(in);
        return 0;
    }

    if (id >= 0) { out("EOPL: already installed (use /U to remove).\r\n"); return 1; }
    id = find_free();
    if (id < 0) { out("EOPL: no free INT 2Fh multiplex id.\r\n"); return 3; }
    mx_id = (unsigned char)id;

    if (use_rtc) rate = 8192;
    else if (!rate) { out("EOPL: measuring CPU speed...\r\n"); rate = pick_rate(); }
    rate_sel = rate;
    oplc_init(OPLC_OPL2, rate);
    if (!(use_rtc ? pcspk_open_rtc(0, gen_cb) : pcspk_open(rate, 0, gen_cb))) { out("EOPL: cannot start the speaker driver.\r\n"); return 4; }
    pcspk_set_assist(1, chunk);
    if (ysub_set) pcspk_set_sub(ysub);
    if (gain) pcspk_set_gain((u32)(gain > 16 ? 16 : gain) * 256UL);
    if (water) pcspk_set_water(water);
    if (diag) { pcspk_set_diag(1); info.diag = 1; }
    old2f = _dos_getvect(0x2F);
    _dos_setvect(0x2F, (void (__interrupt __far *)(void))mx_handler);

    if (!notrap) {
        int t = setup_qemm(diag);
        if (t <= 0) {
            if (t == 0) out("EOPL: no supported memory manager (QEMM) found; AdLib port trapping unavailable (EMM386 support is in progress).\r\n"
                            "      Use /NOTRAP to install the speaker engine without trapping.\r\n");
            pcspk_try_close(); _dos_setvect(0x2F, old2f);
            return 5;
        }
    }

    /* free the environment block (the TSR does not need it) */
    env = *(unsigned far *)MK_FP(my_psp, 0x2C);
    if (env) { _dos_freemem(env); *(unsigned far *)MK_FP(my_psp, 0x2C) = 0; }

    dg = FP_SEG((void __far *)&rate_sel);                    /* DGROUP segment */
    paras = (dg - my_psp) + ((top_sp + 80u) >> 4) + 1;       /* PSP .. end of DGROUP (incl. the small startup stack) */
    out("EOPL installed: OPL2 -> PC speaker, "); out(use_rtc ? "RTC clock " : "PIT clock "); outnum(rate); out(" Hz, ");
    out(info.trap_mode ? "AdLib trapped via QEMM QPI" : "no port trap"); if (diag) out(", DIAG on");
    out(", multiplex id "); outnum((unsigned long)id); out(", resident "); outnum((unsigned long)paras * 16UL); out(" bytes.\r\n");
    _dos_keep(0, paras);
    return 0;
}

