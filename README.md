# ps2bootopia

Kernelloader-based PS2 Linux bootloader, plus a reproducible modern-toolchain
build environment for [kernelloader](https://github.com/rickgaiser/kernelloader).

## Why this exists

kernelloader 3.0 is the standard way to boot Linux on a PlayStation 2. It is
also from 2012, last touched against a 2017-era ps2sdk, and does not build with
a current toolchain. This repository provides:

- a **Docker build environment** that compiles it with today's ps2dev image
  (gcc 15, modern ps2sdk, gsKit)
- **patches** carrying the fixes that requires, kept out of the submodule so
  upstream stays clean — see [`patches/README.md`](patches/README.md)
- room for the original goal: a refreshed configurator UI

## The problem that motivated it

kernelloader auto-loads its configuration from exactly two places, hardcoded in
`loader/configuration.h`:

```c
#define CONFIG_DIR       "mc0:kloader"      /* -> mc0:kloader/config.txt */
#define DVD_CONFIG_FILE  "cdfs:config.txt"
```

`CONFIG_DIR2` (`"mc1:kloader"`) is defined but **only ever used by
`saveMcIcons()`** — never for loading. There is one `loadConfiguration()` call
at startup, on a `configfile` that is `strcpy`'d from `CONFIG_FILE`. So if
FreeMcBoot occupies mc0 and you want everything Linux on mc1, the config is
never found automatically.

You can still get there by hand — Advanced Menu → File Menu → Select Config
File → **"Memory Card 2"** (a real file browser rooted at `mc1:`, so no typing)
→ `kloader/config.txt` → Load Selected Config → Boot — but that is per boot,
because `configfile` is not itself a persisted config item.

**And no, the ELF cannot take a config path as an argument.** `main.cpp`
accepts exactly `-d`, `--no-cdvd` and `--fix-no-disc`; there is no path
option, so a bootloader cannot hand it one. (That answers the open question in
this README's earlier draft.)

The fix is small — a config search list, `mc0:` then `mc1:`, which is what
wLaunchELF and OSDMenu do — but it lives in `loader/`, which does not yet
build. Hence the toolchain work.

## Documentation

| Document | Covers |
|---|---|
| [`docs/kernelloader-internals.md`](docs/kernelloader-internals.md) | Boot chain, SBIOS vs kernel stub vs vmlinux, the config system in full (search order, all keys, device prefixes, the `CONFIG_DIR2` trap), build components, the SBIOS memory budget, `config.mk` switches |
| [`docs/build-environment.md`](docs/build-environment.md) | Every Dockerfile layer and why it exists; how ps2sdk's `Rules.make` behaves out-of-tree; the `IOP_INCS` / `IOP_WARNFLAGS` / `PS2SDKSRC` hooks; `build.sh` usage; the stale-object trap |
| [`docs/porting-notes.md`](docs/porting-notes.md) | Every build failure in order, with the actual error and root cause — including the two real bugs gcc 15 uncovered |
| [`patches/README.md`](patches/README.md) | Summary of the 16 patched files, grouped by cause |

## Build

Requires Docker. Nothing else — the toolchain lives in the image.

```bash
git submodule update --init --recursive
./build.sh
```

Output lands in `bin/` (gitignored), the same path on WSL2, Linux or Cygwin.

```
./build.sh              # build
./build.sh clean        # make clean + clear bin/
./build.sh shell        # interactive shell in the container
./build.sh <target>     # any other make target
REBUILD_IMAGE=1 ./build.sh   # force the toolchain image to rebuild
```

`build.sh` applies `patches/*.patch` to the submodule first, skipping any
already applied.

## What the environment provides

The upstream `ghcr.io/ps2dev/ps2dev` image ships the cross-toolchain and ps2sdk
but is minimal Alpine. The [`Dockerfile`](Dockerfile) adds:

| | Why |
|---|---|
| `make bash gcc musl-dev` | no `make` in the base image; kernelloader also builds **host** tools (`ppm2rgb`, `png2rgb`) |
| `libpng-dev zlib-dev tiff-dev` | `png2rgb` links libpng; loader wants tiff/zlib |
| `ee-*` symlinks | Makefiles hardcode `ee-gcc`; ps2sdk renamed everything to `mips64r5900el-ps2-elf-*` |
| `tools/bin2s` on `PATH` | modern ps2sdk dropped `bin2s` (ships `bin2c`, different interface) — 8 call sites need it |
| ps2sdk **source** clone + `PS2SDKSRC` | IOP modules `include $(PS2SDKSRC)/iop/Rules.make`, which ships only in the source repo, not the installed SDK |
| `IOP_INCS` | the source tree keeps IOP headers per-module; out-of-tree modules need the installed flattened `iop/include` |
| `IOP_WARNFLAGS` | `Rules.make` uses `?=`, so the environment can add `-Wno-pointer-sign` without patching every module |

The last two are worth noting: both are environment-level hooks that fix
*every* IOP module at once instead of patching each Makefile.

## Status

Building: `ppm2rgb`, `png2rgb`, `hello`, `kernel/kernel.elf`, `sharedmem`,
`TGE/sbios` (`sbios_old.elf` + `sbios_new.elf`).

Not yet: `modules/` (ps2sdk fileio structs changed shape), then `crc32gen` and
`loader/` — the largest component, still linking STLport (which modern ps2sdk
no longer ships) with nine years of gsKit drift on top.

One fix carries a real caveat: `sif1_dmatags` alignment was reduced 64 → 16,
losing cache-aliasing protection the original code asks for, because gcc 15
will not express the original intent. It is flagged in-code and in
[`patches/README.md`](patches/README.md).

## Roadmap

- Get `loader/` building; land the `mc0:`/`mc1:` config search
- Refreshed GUI configurator for kernelloader
- Longer term, fold kernelloader's functions into a unified ps2bootopia UI —
  booting Linux, and possibly other \*nixes (a BSD build reportedly exists)

## Credits

kernelloader is by Mega Man, maintained by
[rickgaiser](https://github.com/rickgaiser/kernelloader); releases live on
[SourceForge](https://sourceforge.net/projects/kernelloader/files/Kernelloader/),
where 3.0 is the latest. That SourceForge project also hosts the BlackRhino
Linux distribution, the PS2 Live Linux DVD/USB images, and Debian 5.0 mipsel.

The Docker layout follows the conventions of my
[ps2oom](https://github.com/Arawn-Davies/ps2oom) PS2 Doom port.
