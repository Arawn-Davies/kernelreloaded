# Patches

Modern-toolchain fixes for kernelloader, applied to the submodule by
`../build.sh` before each build.

They live here rather than in the submodule so that `kernelloader/` stays a
clean checkout of upstream and can be rebased on it. Nothing here is pushed
anywhere but this repository.

| Patch | Base commit | Files |
|---|---|---|
| `0001-modern-toolchain.patch` | `d4b88dd` (citronalco/kernelloader master) | 18 |

If a patch stops applying, the submodule has moved off that base commit. Either
reset the submodule to it, or rebase the patch onto the new upstream.

## What 0001 changes, and why

kernelloader was last built against a 2010-era toolchain. The current ps2dev
image is gcc 15 with a modern ps2sdk, and essentially none of the drift is
kernelloader's fault. Grouped by cause:

> **Base note.** The submodule tracks
> [citronalco/kernelloader](https://github.com/citronalco/kernelloader), not
> rickgaiser's. Upstream's last commit is 2017-03-01; citronalco carries five
> further commits (2018–2020) including `loader: fix std & stlport related
> compilation errors` and a libpng port of `png2rgb`. Basing on it removed
> `png2rgb` from this patch entirely — an independent fix that turned out to
> match citronalco's almost line for line — and supplies the STLport fix that
> `loader/` needs.

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
  element type); a wrapper struct/union does not help, gcc propagates the
  alignment to the member.

  This loses the cache-aliasing protection the comment above it asks for — but
  **less than it first appears**. Comparing against the original TGE
  ([ps2homebrew/TGE](https://github.com/ps2homebrew/TGE), 2004), that array
  carries *no alignment attribute at all*:

  ```c
  static ee_dmatag_t  sif1_dmatags[32];   /* upstream TGE */
  static iop_dmatag_t iop_dmatags[32];    /* upstream TGE */
  ```

  The `aligned(64)` was added later by kernelloader. So `aligned(16)` is still
  **stricter than the original design**, and matches the qword alignment SIF
  DMA actually requires. Worth knowing if SIF1 DMA ever misbehaves, but not the
  regression it looked like.
- `TGE/sbios/Makefile`: `-O2` → `-Os` for the `old` variant. gcc 15 emits more
  code, and the corrected 64-bit `u64` enlarged static data; together they
  overflowed the fixed `0xEFE0` region (see `TGE/sbios/linkfile` — it cannot
  grow, the PS2 model-number string sits at the end). The Makefile already
  applied `-Os` to the `new` variant for the same reason.

### IOP modules (`modules/SMSCDVD`)

- `fio_dirent_t` → `io_dirent_t`. The old SDK's name for the same struct;
  modern `common/include/io_common.h` defines it with an identical shape
  (`io_stat_t stat; char name[256]; void *privdata;`). `fio_dirent_t` exists
  nowhere in current ps2sdk, and the "request for member 'stat' in something
  not a structure" errors were cascading from that unknown type.
- **18 cast-as-lvalue assignments** on `struct dirTocEntry *`, same removed GNU
  extension as `sifrpc.c`. Note the trap: the RHS **must** be parenthesised.
  `p = (struct dirTocEntry *)cache + n` silently scales `n` by
  `sizeof(struct dirTocEntry)`, where the original `(char *)p = cache + n` did
  byte arithmetic. The patch uses
  `p = (struct dirTocEntry *)(cache + n)` throughout.

### `loader/`

- `-Werror` disabled. With `-W` (`-Wextra`) it fails inside **ps2sdk's own
  headers** — `rom0_info.h` and `osd_config.h` trip `-Wunused-parameter` about
  twenty times before any kernelloader source is reached.
  `-Werror-implicit-function-declaration` is kept, since that catches genuine
  missing-header bugs.

### Removed language extensions and APIs

- `png2rgb/png2rgb.c`: **no longer in this patch.** libpng made `png_info`
  opaque in 1.5 and dropped `png_infopp_NULL`/`png_voidp_NULL` in 1.6, so this
  needed porting to the accessor API — but citronalco already did it upstream
  of us. (This port independently reproduced the same fix before the fork was
  found; the two agreed almost line for line, which was reassuring.)
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

  Provenance: `mc.c` does **not** exist in the original TGE
  ([ps2homebrew/TGE](https://github.com/ps2homebrew/TGE) ships 12 files;
  kernelloader's copy has 30). Memory card, CDVD, pad, sound and fileio support
  were all added by kernelloader, so this is its bug rather than TGE's — which
  is where a fix or an upstream report should go.

## Build status at the time of writing

Builds: `ppm2rgb`, `png2rgb`, `hello`, `kernel/kernel.elf`, `sharedmem`,
`TGE/sbios` (both variants), `modules/`, `crc32gen`.

`loader/` is reached and compiling — STLport is no longer the blocker, thanks
to citronalco's fix. Four categories of genuine work remain:

- missing `<stdlib.h>`/`<malloc.h>` — `memalign`, `free`, `realloc`, `atoi`
- `fioExit()`, a removed ps2sdk fileio API needing a modern equivalent
- three incompatible-pointer-type call sites, which need reading rather than
  suppressing
- `loader/stdint.h` colliding with newlib's, because some files include
  `"stdint.h"` and others `<stdint.h>` while `-I.` is in effect

Note that the config-path change this port exists to enable — making
kernelloader auto-load `mc1:kloader/config.txt` as well as `mc0:` — lives
entirely in `loader/`. Everything built so far is prerequisite to linking it.
