/* SPDX-License-Identifier: GPL-3.0-or-later */
/* E-ray DOS OPL Emulator - OPL core API (C linkage, cdecl)
 * YMFM(C++14) wrapper. Watcom/DOS side code uses only this C API. */
#ifndef OPL_CORE_H
#define OPL_CORE_H
#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;   /* 32bit flat model: unsigned long == 32bit */
typedef short          s16;

#define OPLC_API __cdecl      /* common to Watcom/GCC: stack args, caller cleanup, '_' prefix */
#define OPLC_OPL2 0
#define OPLC_OPL3 1

/* init. out_rate: output sample rate (Hz). returns YMFM native rate (~49716) */
u32  OPLC_API oplc_init(int mode, u32 out_rate);

/* write to AdLib/SB port offset (0..3 from 0x388): 0=addr, 1=data, 2=addr_hi(OPL3), 3=data_hi */
void OPLC_API oplc_write_port(u32 offset, u8 value);
u8   OPLC_API oplc_read_status(void);

/* generate mono PCM (resampled to out_rate, linear interpolation). returns sample count */
u32  OPLC_API oplc_generate_mono(s16 *out, u32 count);

/* debug/verification */
u32  OPLC_API oplc_native_rate(void);
u32  OPLC_API oplc_arena_used(void);

#ifdef __cplusplus
}
#endif
#endif
