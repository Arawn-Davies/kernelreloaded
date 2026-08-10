# Patches

Modern-toolchain fixes for kernelloader, applied to the submodule by
`../build.sh` before each build.

They live here rather than in the submodule so that `kernelloader/` stays a
clean checkout of upstream and can be rebased on it. Nothing here is pushed
anywhere but this repository.

| Patch | Base commit | Files |
|---|---|---|
| `0001-modern-toolchain.patch` | `ce0fb430` (rickgaiser/kernelloader master) | 16 |

If a patch stops applying, the submodule has moved off that base commit. Either
reset the submodule to it, or rebase the patch onto the new upstream.

## What 0001 changes, and why

kernelloader was last built against a 2010-era toolchain. The current ps2dev
image is gcc 15 with a modern ps2sdk, and essentially none of the drift is
kernelloader's fault. Grouped by cause:

### The 64-bit type bug (the important one)

`kernel/stdint.h` and `TGE/include/tge_types.h` both declared `u64`/`int64_t`
as `unsigned long`. That is 64-bit only when the compiler runs with `-mgp64`,
which the old `-mips3` flag implied. Under the n32 ABI that modern ps2dev gcc
targets, `long` is 32-bit — so `u64` silently became half-width.

The visible consequence: `ee_dmatag_t` (a SIF DMA tag, which the hardware
defines as a 16-byte qword: `id_qwc` + `addr` + 8 bytes padding) shrank to 12
bytes. Both files now use `long long`, which is 64-bit under either ABI.

This is a correctness fix, not a portability tweak.

### ABI / ISA flags

- `kernel/Makefile`: `-mips3` → `-march=r5900`. On gcc 15 `-mips3` forces
  `-mgp64`, which collides with the r5900 target's default `-mno-odd-spreg`
  ("unsupported combination"). `-march=r5900` is also required for the
  assembler to accept the MMI opcodes gcc emits.
- `kernel/Makefile`: `+= -fno-builtin`. gcc rewrites `printf("…\n")` into
  `puts()`, which does not exist in this `-nostdlib` kernel.
- `kernel/graphic.c`: inline asm `.set mips3` → `.set arch=r5900`; it uses
  `pextlw`, an r5900 MMI instruction that mips3 rejects.
- `TGE/sbios/Makefile`: `-D_R5900`. `tge_types.h` gates `u128`/`s128` behind
  `#if defined(R5900) || defined(_R5900)`, which old ee-gcc predefined; modern
  gcc defines `_MIPS_ARCH_R5900` instead, so `u128` vanished.
- `TGE/sbios/dve_reg.S`: `.set at` around an absolute-address store, which the
  assembler expands via `$at` — forbidden by the file's `.set noat`.

### Struct layout and alignment

- `kernel/gs.h`, `loader/gs.h`: dropped ~600 **member-level**
  `__attribute__((packed))` from `uint64_t` bitfields. Modern gcc treats that
  as "use the smallest allocation unit", shrinking the underlying type so a
  63-bit field no longer fits. Struct-level `packed` is retained.
- `TGE/sbios/sifdma.c`: `sif1_dmatags` alignment 64 → 16. gcc 15 rejects
  `aligned(64)` on an array of 16-byte elements (the attribute lands on the
  element type). **This loses the cache-aliasing protection the original
  comment asks for** — a wrapper struct/union does not help, gcc propagates the
  alignment to the member. If SIF1 DMA misbehaves on hardware, suspect this.
- `TGE/sbios/Makefile`: `-O2` → `-Os` for the `old` variant. gcc 15 emits more
  code, and the corrected 64-bit `u64` enlarged static data; together they
  overflowed the fixed `0xEFE0` region (see `TGE/sbios/linkfile` — it cannot
  grow, the PS2 model-number string sits at the end). The Makefile already
  applied `-Os` to the `new` variant for the same reason.

### Removed language extensions and APIs

- `png2rgb/png2rgb.c`: ported to the libpng 1.6 accessor API. `png_info`
  became opaque in 1.5, and `png_infopp_NULL`/`png_voidp_NULL` were removed.
  Also adds `<string.h>` for `memset`.
- `TGE/sbios/sifrpc.c`: `(u8 *)packet += 64` was a cast-as-lvalue, a GNU C
  extension removed in gcc 4.0.
- `TGE/sbios/strcmp.c`: **new**. The SBIOS links `-nostdlib` and ships its own
  string routines one per file, but never had `strcmp`; older toolchains
  satisfied it from libgcc or a builtin.
- `sharedmem/sharedmem.c`: `#include <stdio.h>` for `printf`, which modern
  ps2sdk headers no longer pull in transitively.

### Warning suppressions (`TGE/sbios/Makefile`)

- `-fno-builtin-snprintf -fno-builtin-vsnprintf` — the local freestanding
  `stdio.h` declares an `int` length where the builtins expect `size_t`.
- `-Wno-pointer-sign` — 2003 code assigns freely between `u32*`/`int*`.
- `-fno-strict-aliasing` — the SBIOS type-puns through pointer casts
  throughout (hardware registers, SIF packets, sound regs).
- `-Wno-error=array-bounds` — **demoted, deliberately not silenced.**
  `mc.c:1140` indexes `mcRpcCmd[2][17]` with `MC_FUNC_CHG_PRITY` (`0x14` = 20),
  the wrong enum family; that table is indexed by `MC_RPCCMD_*` everywhere else
  and has no `CHG_PRITY` entry. This is a **pre-existing out-of-bounds read**
  that gcc 15 newly detects, not something this port introduced. Fixing it
  needs the correct MCSERV command byte, so it is left visible as a warning.

## Build status at the time of writing

Builds: `ppm2rgb`, `png2rgb`, `hello`, `kernel/kernel.elf`, `sharedmem`,
`TGE/sbios` (both `sbios_old.elf` and `sbios_new.elf`).

Not yet building: `modules/` (ps2sdk fileio structs changed shape —
`lpBuf->stat.mode` no longer resolves), then `crc32gen` and `loader/`. `loader/`
is the largest component and still links against STLport, which modern ps2sdk
no longer ships, plus nine years of gsKit drift.

Note that the config-path change this port exists to enable — making
kernelloader auto-load `mc1:kloader/config.txt` as well as `mc0:` — lives
entirely in `loader/`. Everything built so far is prerequisite to linking it.
