# Third-party code and credits

EOPL is distributed under the **GNU General Public License v3** (see `LICENSE`). It combines code under compatible licenses:

| Component | How it is used | Author / License | Source |
|---|---|---|---|
| **DBOPL** (OPL2/OPL3 emulator) | The `DBOPL.CPP/.H` copy from VSBHDA is **patched at build time** (`tools/gen_dbopl16.py`, `gen_dbopl32.py`, `gen_tables.py`) into a 16-bit, integer-only OPL2 engine linked into `EOPL.EXE`. The upstream files are not stored in this repository. | The DOSBox Team — GPL v2 or later | https://github.com/Baron-von-Riedesel/VSBHDA (copy of DOSBox's DBOPL), https://www.dosbox.com |
| **ADLCOM** | The QEMM QPI I/O-callback glue (`src/trapglue.asm`, from `res_glue.s`), the QPI helper calls, the AdLib port/timer-status emulation and the `IN AL,DX` run-time patching in `src/eopl.c` are **derived from ADLCOM**. | Peter De Wachter (2020), Jose Phillips (2023), Electric-ray — ISC-style notice in its README, GPL v3 `LICENSE` in the repository | https://github.com/Electric-ray/adlcom |
| **VSBHDA** | Reference for running OPL synthesis chunk-wise inside the sound ISR; DBOPL source copy (see above). | Baron-von-Riedesel — GPL v2 | https://github.com/Baron-von-Riedesel/VSBHDA |
| **hearo** | Design reference only (PC speaker output with PIT, BIOS tick preservation, safe shutdown order). **No code is copied**; its mode-3 output was found unsuitable and replaced by mode-0 PWM. | MIT | https://github.com/tonyuatkins-afk/hearo |
| **YMFM** | Alternative engine used in the early prototypes (`src/opl_core.cpp`, `tools/gen_min.py`) and for accuracy comparison. Not part of `EOPL.EXE`. | Aaron Giles — BSD-3-Clause | https://github.com/aaronsgiles/ymfm |
| **Opal** | Alternative engine evaluated and rejected for speed (`src/opal_core.c`). Not part of `EOPL.EXE`. | RealBitdancer — MIT | https://github.com/RealBitdancer/opal |
| **SBEMU**, **E-RayDSB** | Reference only (port trapping and OPL3 emulation ideas; ESP32 YMFM usage). | see upstream | https://github.com/crazii/SBEMU, https://github.com/Electric-ray/E-RayDSB |

## Notes

- The `ref/` directory (upstream sources) is **not committed**; `tools\fetch_refs.ps1` clones the pinned revisions.
- Build tools (Open Watcom 1.9, MinGW, DOSBox-X, 86Box, QEMM) are not part of this repository and keep their own licenses.
  The `dos4gw.exe` used by the early 32-bit prototypes is **not** redistributed here.
- Please check the upstream license texts when redistributing binaries; if any attribution above is wrong or incomplete, open an issue.
