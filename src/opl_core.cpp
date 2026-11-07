/* SPDX-License-Identifier: GPL-3.0-or-later */
// E-ray DOS OPL Emulator - YMFM wrapper (C++14, freestanding-oriented)
#include "opl_core.h"
#include "ymfm_opl.h"

namespace {

struct opl_if : public ymfm::ymfm_interface {};   // default no-op interface (timers/IRQ unused)

alignas(8) char g_chip_buf[sizeof(ymfm::ymf262) > sizeof(ymfm::ym3812) ? sizeof(ymfm::ymf262) : sizeof(ymfm::ym3812)];
alignas(8) char     g_if_buf[sizeof(opl_if)];   // constructed manually in oplc_init (no global ctors in freestanding/DOS)
ymfm::ym3812       *g_opl2 = nullptr;
ymfm::ymf262       *g_opl3 = nullptr;
int                 g_mode;
u32                 g_native, g_out;
u32                 g_pos;            // 16.16 phase
u32                 g_step;           // native/out ratio in 16.16
s16                 g_s0, g_s1;       // previous/current native samples for interpolation
bool                g_primed;

inline s16 clamp16(int v){ return v > 32767 ? 32767 : (v < -32768 ? -32768 : (s16)v); }

s16 native_sample()
{
    if (g_mode == OPLC_OPL3) {
        ymfm::ymf262::output_data o;
        g_opl3->generate(&o, 1);
        return clamp16((o.data[0] + o.data[1] + o.data[2] + o.data[3]) / 2);
    } else {
        ymfm::ym3812::output_data o;
        g_opl2->generate(&o, 1);
        return clamp16(o.data[0]);
    }
}

} // namespace

// ---- simple bump arena (replaces new/delete) ----
static char  g_arena[4 * 1024];   // measured: OPL2 1656 B, OPL3 3312 B
static u32   g_arena_pos;
void *operator new(unsigned int n)  { n = (n + 7) & ~7u; void *p = g_arena + g_arena_pos; g_arena_pos += n; return p; }
void *operator new[](unsigned int n){ return operator new(n); }
void  operator delete(void *) noexcept {}
void  operator delete[](void *) noexcept {}
void  operator delete(void *, unsigned int) noexcept {}

extern "C" {

u32 OPLC_API oplc_init(int mode, u32 out_rate)
{
    g_arena_pos = 0;
    g_mode = mode; g_out = out_rate;
    opl_if *intf = new (g_if_buf) opl_if;
    if (mode == OPLC_OPL3) { g_opl3 = new (g_chip_buf) ymfm::ymf262(*intf); g_opl3->reset(); g_native = g_opl3->sample_rate(14318180); }
    else                   { g_opl2 = new (g_chip_buf) ymfm::ym3812(*intf); g_opl2->reset(); g_native = g_opl2->sample_rate(3579545);  }
    g_step = (g_native << 16) / out_rate;   // 49716<<16 < 2^32 so plain 32bit division (no __udivdi3)
    g_pos = 0; g_s0 = g_s1 = 0; g_primed = false;
    return g_native;
}

void OPLC_API oplc_write_port(u32 off, u8 v)
{
    off &= 3;
    if (g_mode == OPLC_OPL3) g_opl3->write(off, v); else g_opl2->write(off & 1, v);
}

u8 OPLC_API oplc_read_status(void) { return g_mode == OPLC_OPL3 ? g_opl3->read_status() : g_opl2->read_status(); }

u32 OPLC_API oplc_generate_mono(s16 *out, u32 count)
{
    if (!g_primed) { g_s0 = native_sample(); g_s1 = native_sample(); g_primed = true; }
    for (u32 i = 0; i < count; i++) {
        while (g_pos >= 0x10000u) { g_pos -= 0x10000u; g_s0 = g_s1; g_s1 = native_sample(); }
        int frac = (int)(g_pos & 0xFFFF);
        out[i] = (s16)(g_s0 + (((int)g_s1 - (int)g_s0) * frac >> 16));
        g_pos += g_step;
    }
    return count;
}

u32 OPLC_API oplc_native_rate(void) { return g_native; }
u32 OPLC_API oplc_arena_used(void)  { return g_arena_pos; }

} // extern "C"
