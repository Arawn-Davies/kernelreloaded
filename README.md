# kernelreloaded

**A continuation of [kernelloader](https://github.com/citronalco/kernelloader),
the PlayStation 2 Linux bootloader** — building again on a modern toolchain.

<sub>Looking for <i>kernelloader ps2 build</i>, <i>kernelloader modern
toolchain</i>, or <i>ps2 linux bootloader</i>? You're in the right place.</sub>

![KernelReloaded UI design target](assets/mockup.png)

> **A design target, not a screenshot.** The starfield and Tux are real and in
> the build; the rest is where the UI is headed. What that costs splits neatly
> in two:
>
> **Artwork alone** gets the title lockup, the rounded menu highlight, the
> System Info panel and the ×/△/○ glyphs — each becomes a texture drawn with
> the `gsKit_prim_sprite_texture` calls the loader already makes. No new
> rendering code.
>
> **New code** is needed for the menu typography. Dynamic text goes through
> `gsKit_fontm_print_scaled`, and that font lives in the PS2's own BIOS ROM:
> one typeface, scalable, not swappable. Matching the mockup means
> pre-rendering a glyph atlas and writing a small text renderer over
> `sprite_texture`.
>
> And it lands **coarser than it looks** — the PS2 outputs 640×448 (NTSC),
> 640×512 (PAL) or 640×480 (VGA), roughly 2.3× smaller than the image above.

## Why this exists

kernelloader is the standard way to boot Linux on a PlayStation 2. Release 3.0
dates from May 2014, upstream's last commit was **March 2017**, and it does not
build against a current toolchain. This repository provides:

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
| [`patches/README.md`](patches/README.md) | Summary of the 18 patched files, grouped by cause |

## Which kernelloader?

The submodule tracks **[citronalco/kernelloader](https://github.com/citronalco/kernelloader)**,
not rickgaiser's.

Upstream is dead: last commit **2017-03-01**, release 3.0 from May 2014, no open
issues, not archived — just abandoned. Of its seven forks, only citronalco's has
real work: five commits from 2018–2020, including
`loader: fix std & stlport related compilation errors` and a libpng port of
`png2rgb`.

That STLport fix matters. `loader/Makefile` linked `-lstlport`, and modern
ps2sdk no longer ships STLport at all; the fix drops the library and adds `std::`
to nine `vector<` uses, since `-D_STLP_NO_NAMESPACES` had put STLport's `vector`
in the global namespace. Without it, `loader/` cannot link.

Pleasingly, citronalco is also the author of the 2010 ps2dev.org thread on
booting PS2 Linux with an NFS root — the same person, twice, a decade apart.

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
`TGE/sbios` (both variants), `modules/`, `crc32gen`.

`loader/` is reached and compiling. STLport is no longer the blocker; what
remains is ordinary porting work — missing `<stdlib.h>`/`<malloc.h>`
declarations, the removed `fioExit()` API, three incompatible-pointer-type call
sites, and `loader/stdint.h` colliding with newlib's. See
[`docs/porting-notes.md`](docs/porting-notes.md).

One fix carries a caveat, though a smaller one than it first looked:
`sif1_dmatags` alignment was reduced 64 → 16, because gcc 15 will not express
the original intent. That drops a cache-aliasing protection the comment asks
for — but the original TGE
([ps2homebrew/TGE](https://github.com/ps2homebrew/TGE), 2004) declares that
array with **no alignment attribute at all**, so 16 is still stricter than the
design it shipped with. Flagged in-code and in
[`patches/README.md`](patches/README.md).

## Roadmap

- Get `loader/` building; land the `mc0:`/`mc1:` config search
- Refreshed GUI configurator for kernelloader
- Work toward the UI at the top of this file: textured chrome first, then a
  glyph-atlas text renderer to escape the BIOS ROM font
- Longer term, fold kernelloader's functions into a unified kernelreloaded UI —
  booting Linux, and possibly other \*nixes (a BSD build reportedly exists)

## Credits

kernelloader is by Mega Man, maintained by
[rickgaiser](https://github.com/rickgaiser/kernelloader); releases live on
[SourceForge](https://sourceforge.net/projects/kernelloader/files/Kernelloader/),
where 3.0 is the latest. That SourceForge project also hosts the BlackRhino
Linux distribution, the PS2 Live Linux DVD/USB images, and Debian 5.0 mipsel.

The Docker layout follows the conventions of my
[ps2oom](https://github.com/Arawn-Davies/ps2oom) PS2 Doom port.
