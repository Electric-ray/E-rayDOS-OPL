/* SPDX-License-Identifier: GPL-3.0-or-later */
// E-ray DOS OPL Emulator - DBOPL (DOSBox OPL3, via VSBHDA's Watcom-buildable copy) backend.
// Same oplc_* API as opl_core.cpp (YMFM) / opal_core.c (Opal). Link exactly one backend.
// DBOPL here is the int32-accumulating variant produced by tools/gen_dbopl32.py (original int16 build wraps on overload).
#include <stddef.h>
#include "opl_core.h"
#include "DBOPL.H"

#ifdef EOPL_OPL2_ONLY                   /* TSR build: OPL2 mono only -> half-size block, no stereo buffer */
#define BLK 128u
static int32_t g_tmp[BLK];
#else
#define BLK 256u                        /* samples per engine call: stereo needs BLK*2 int32 */
static int32_t g_tmp[BLK * 2];          /* exact size (the engine memsets 4*samples (*2 if stereo) bytes) */
#endif

static DBOPL::Chip *g_chip;
static char g_chip_buf[sizeof(DBOPL::Chip)];     /* static storage: a TSR must not depend on the near heap (it would grow past the kept memory) */
static u16 g_idx[2];
static int g_mode;

/* tiny placement new (no <new> dependency, no heap) */
struct oplc_place { };
inline void *operator new(size_t, void *p, oplc_place) { return p; }

u32 OPLC_API oplc_init(int mode, u32 out_rate)
{
    g_mode = mode;
    g_chip = new ((void *)g_chip_buf, oplc_place()) DBOPL::Chip(mode == OPLC_OPL3);
    g_chip->Setup(out_rate);                 // DBOPL resamples internally (linear) to out_rate
    g_idx[0] = g_idx[1] = 0;
    return 49716;
}

void OPLC_API oplc_write_port(u32 off, u8 v)
{
    if (g_mode != OPLC_OPL3) off &= 1;
    switch (off & 3) {
    case 0: g_idx[0] = (u16)g_chip->WriteAddr(0x388, v); break;
    case 1: g_chip->WriteReg(g_idx[0], v); break;
    case 2: g_idx[1] = (u16)g_chip->WriteAddr(0x38A, v); break;
    case 3: g_chip->WriteReg(g_idx[1], v); break;
    }
}

u8 OPLC_API oplc_read_status(void) { return 0; }   // TODO: timers/status are faked outside the chip in VSBHDA (VOPL3)

static inline s16 sat16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : (s16)v); }

u32 OPLC_API oplc_generate_mono(s16 *out, u32 count)
{
    u32 done = 0;
    while (done < count) {
        u32 n = count - done; if (n > BLK) n = BLK;
        g_chip->Generate(g_tmp, n);
#ifdef EOPL_OPL2_ONLY
        for (u32 i = 0; i < n; i++) out[done + i] = sat16(g_tmp[i]);
#else
        if (g_chip->opl3Active) {            // stereo interleaved -> mono average
            for (u32 i = 0; i < n; i++) out[done + i] = sat16((g_tmp[2*i] + g_tmp[2*i+1]) / 2);
        } else {
            for (u32 i = 0; i < n; i++) out[done + i] = sat16(g_tmp[i]);
        }
#endif
        done += n;
    }
    return count;
}

u32 OPLC_API oplc_native_rate(void) { return 49716; }
u32 OPLC_API oplc_arena_used(void)  { return (u32)sizeof(DBOPL::Chip); }
