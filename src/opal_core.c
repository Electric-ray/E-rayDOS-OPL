/* SPDX-License-Identifier: GPL-3.0-or-later */
/* E-ray DOS OPL Emulator - Opal (C11) backend, same oplc_* API as opl_core.cpp (YMFM).
 * Build: link either opl_core.o+ymfm_opl_min.o OR opal_core.o+opal.o (never both). */
#include "opl_core.h"
#include "opal/opal.h"

static Opal g_opal;
static u8   g_addr, g_addr_hi;
static int  g_mode;

u32 OPLC_API oplc_init(int mode, u32 out_rate)
{
    g_mode = mode; g_addr = g_addr_hi = 0;
    opalInit(&g_opal, (int)out_rate);         /* Opal resamples internally (49716 -> out_rate) */
    return OPAL_OPL3_SAMPLE_RATE;
}

void OPLC_API oplc_write_port(u32 off, u8 v)
{
    if (g_mode != OPLC_OPL3) off &= 1;        /* OPL2: ports 2/3 alias 0/1 (same as YMFM path) */
    switch (off & 3) {
    case 0: g_addr = v; break;
    case 1: opalWriteReg(&g_opal, g_addr, v); break;
    case 2: g_addr_hi = v; break;
    case 3: opalWriteReg(&g_opal, (u16)(0x100 | g_addr_hi), v); break;
    }
}

u8 OPLC_API oplc_read_status(void) { return opalReadStatus(&g_opal); }

u32 OPLC_API oplc_generate_mono(s16 *out, u32 count)
{
    u32 i;
    for (i = 0; i < count; i++) {
        s16 l, r;
        opalSample(&g_opal, &l, &r);
        out[i] = (s16)(((int)l + (int)r) / 2);
    }
    return count;
}

u32 OPLC_API oplc_native_rate(void) { return OPAL_OPL3_SAMPLE_RATE; }
u32 OPLC_API oplc_arena_used(void)  { return (u32)sizeof(Opal); }
