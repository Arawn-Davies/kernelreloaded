# kernelreloaded

Reviving [kernelloader](https://github.com/citronalco/kernelloader) — the PS2
Linux bootloader, last touched upstream in March 2017 — on a modern toolchain.

**This file is tracked and public.** It was local-only via `.git/info/exclude`
until 2026-08-13, on the reasoning that project instructions are private
scaffolding. That was wrong: everything in here is about how to work on *this
tree* — the gotchas, the conventions, why the layout is what it is — and a
contributor needs it as much as an assistant does. Write it for both.

Nothing personal or credentialed goes in this file. Notes about other machines,
private repos or anything with a secret in it belong in `.git/info/exclude`d
scratch, not here.

## Why the project exists

FMCB occupies `mc0:`, so kernelloader lives on `mc1:` — but it hardcodes its
config path:

```c
#define CONFIG_DIR "mc0:kloader"   /* -> mc0:kloader/config.txt */
```

The goal is a `mc?:`-style search across both slots, the way wLaunchELF maps
any card. Getting there first required making the 2003-era source compile at
all under gcc 15.

## Layout

**The kernelloader source IS the repo.** `Makefile`, `kernel/`, `TGE/` and
`loader/` sit at the top level, so `make` here just works and there is exactly
one source tree.

The four EE-side directories stay at the root because everything else installs
*into* `loader/` — `TGE/sbios/Makefile` does `install ../../loader/TGE/`, every
IOP module does `cp $(IOP_BIN) ../../loader`, `kernel/Makefile` writes
`../loader/kernel.elf`. Burying `loader/` a level deeper buys tidiness and costs
fifteen install paths. Everything that does *not* have that coupling was grouped
on 2026-08-13; the root went from 37 tracked entries to 18.

| Path | What it is |
|---|---|
| `Makefile`, `config.mk` | upstream's top-level build, drives everything below |
| `build.sh` | builds the tree inside the Docker toolchain |
| `Dockerfile` | toolchain image on `ghcr.io/ps2dev/ps2dev` |
| `loader/` | the loader itself — UI, config, file browsers → `kloader.elf` |
| `kernel/` | EE kernel *stub* (`kernel.elf`), not Linux |
| `TGE/` | the SBIOS; `TGE/iop/intrelay/` builds the intrelay IRXs |
| `RTE/` | Sony's SBIOS, built only if the Linux Kit disc is mounted |
| `iop/` | every IOP module: `sharedmem`, `smaprpc`, `dev9init`, `SMSUTILS`, `SMSCDVD`, `eromdrvloader`. Its `Makefile` builds all six; they were four separate `make -C` lines at the root |
| `include/` | the two headers shared between EE and IOP sides |
| `linux/` | **everything about the guest OS, nothing about the loader** — `patches/` (upstream's Linux patch set), `kernelconfig`, `phase1/` (our from-source kernel build), the three `driver_*` trees, `buildlinux.sh`, `buildtoolchain.sh` |
| `tools/` | host-side helpers: `bin2s` (POSIX sh replacement for a tool modern ps2sdk dropped), `crc32gen`, `png2rgb`, `ppm2rgb`, `hello`, `pcsx2/`, the deploy scripts |
| `assets/` | shipped textures; `assets/src/` holds unshipped source artwork; `assets/mcicons/` the memory-card icon |
| `docs/` | internals, build-environment, porting-notes; `docs/upstream/` holds upstream's own `readme.txt`, `install.txt`, `history.txt`, `TODO.txt` and `KNOWNPROBLEMS.txt` |

**This was a git submodule until 2026-08-12, then briefly a nested `loader/`
wrapper. It is neither now.** Upstream (citronalco/kernelloader) has been dead
since 2017 and nothing here can be pushed to it, so a submodule pointer only
ever referenced commits that existed on one machine — cloning got an empty
directory and GitHub's link 404'd. The old
`patches/0001-modern-toolchain.patch` is gone too; its changes are in the
source, and its content survives in history at `708c8ff`.

**Do not re-add a submodule, a `.gitmodules`, a patch-application step, or a
wrapper directory.**

That includes splitting `linux/` out into a repo of its own, which looks
tempting now that it is one directory. It is coupled to this tree, not merely
adjacent: `linux/phase1/patches/romcons-input.patch` exists because TGE
implements `SB_PUTCHAR` as `sio_putc()`, so a change to SBIOS call numbering is
a change to that patch. Across a submodule boundary that is two commits and a
pointer bump, and `git bisect` can no longer cross the seam that broke you. The
whole pitch is `git clone && ./build.sh`. If the kernel ever becomes a shipped
artifact with consumers other than this loader, `linux/` is the seam — and
large binaries want a GitHub Release attachment, not a submodule or LFS.

## Build

```sh
./build.sh                                  # output lands in bin/
KERNELRELOADED_IMAGE=other:tag ./build.sh   # override the image
```

**`kloader.elf` and `loader.elf` are the same program.** `loader/Makefile`
links `loader.elf`; `crc32gen` patches the `.text`/`.rodata` checksums into its
`.crc32` section; `ps2-packer` compresses that into a self-extracting
`kloader.elf` (6.5 MB → ~985 KB). Only `kloader.elf` boots — its entry point is
the packer stub, and it has no symbols, so `loader.elf` is what you point
`nm`/`addr2line` at. `build.sh` puts the deliverable in `bin/` alone and the two
intermediates in `bin/debug/`, so there is never a question about which file to
copy onto a card. Do not "tidy" them back into one directory.

**Never `git add -f` a build artifact.** The build drops ~45 binaries and 17
generated headers across the tree, all covered by the second half of
`.gitignore`. They were committed by accident once already. Everything ignored
is reproducible from source — including the four `TGE/iop/intrelay` outputs,
which look like vendored blobs (ps2sdk does not ship them) but are built from
`TGE/iop/intrelay/src`.

Textures under `loader/*.png` are also ignored where `assets/` is their source
of truth; `build.sh` copies them in. The five still tracked there
(`back`, `folder`, `selected`, `unselected`, `up`) are upstream's originals,
which have no `assets/` counterpart. **Edit artwork in `assets/`, never in
`loader/`** — the latter is overwritten on every build.

To check nothing needed is being ignored, delete it all and rebuild. Name the
source directories explicitly; a bare `git clean -Xdf` at the root would also
wipe `dist/`, which holds a downloaded kernel and initrd that take a while to
reassemble:

```sh
git clean -Xdf kernel TGE RTE loader iop tools
./build.sh
```

That misses one thing `git clean` cannot see: `loader/.depend/` and its
siblings are written by the container as **root**, so a host-side `rm -rf` gets
`Permission denied` and the stale `.d` files survive. They record absolute
prerequisite paths, so anything that moves a source directory leaves them
pointing at a path that no longer exists — `No rule to make target
'../crc32gen/crc32gen.h'`, from a Makefile that no longer says that. Clear them
from inside the image:

```sh
docker run --rm -v "$PWD:/work" -w /work kernelreloaded:local \
  bash -c 'find . -name .depend -type d -not -path "./dist/*" -exec rm -rf {} + ; true'
```

## Status

The whole tree builds, and `./build.sh` produces a `kloader.elf` that boots
Linux. The four original link blockers are long gone; see `docs/porting-notes.md`
for what they were.

What works, as of 2026-08-13:

- **PS2 Linux boots to an interactive shell**, on real hardware and under
  PCSX2. On hardware `bash` runs as a login shell, sources `/etc/profile` and
  gives coloured `ls`.
- **Under PCSX2 it needs `tools/pcsx2/ee-tlb-fixes.patch`** — seven emulator
  bugs, six in the EE MMU and one in BIOS-syscall emulation. Stock PCSX2 stops
  dead at `Freeing unused kernel memory` in an endless TLB refill loop.
  `tools/pcsx2/redgreen.sh` demonstrates the difference in one command.
- **The kernel is built from source** by `linux/phase1/build-kernel.sh`, with
  our own Tux logo and `linux/phase1/patches/romcons-input.patch`, which makes
  the ROM console tty readable — without it every shell reads EOF and exits
  silently.
- **Boot to a shell takes about 19 seconds** under emulation, down from 61.

Known not to work: `cdfs:` cannot read the PS2 Linux Live DVD, so its kernel
and initrd have to be loaded from `host:` or a card. See the end of
`docs/emulator-testing.md` — including what was already tried and reverted.

## Pretending to have more RAM

`FAKE_EXTRA_RAM` in `config.mk` (default 64) makes the loader tell Linux the
console has more than a retail PS2's 32MB. The chain is short: `loader.c` puts
it in `bootinfo.maxmem`, and the kernel's `arch/mips/ps2/prom.c` does
`add_memory_region(0, ps2_bootinfo->maxmem & PAGE_MASK, BOOT_MEM_RAM)`. No
kernel change is involved -- it is simply what Linux is told.

**The memory has to actually exist.** Under PCSX2 that means `ExtraMemory=true`,
which maps the 128MB T10K devkit layout; 64 sits inside it. A retail console has
32MB and nothing else, and told otherwise the kernel hands out pages that are
not there. Hence the opt-in flag and the `kprintf` on every boot: a default
build cannot carry the lie onto hardware by accident. Blank the variable for a
stock build.

Boots as `On node 0 totalpages: 16383`, i.e. 16383 x 4K = 64MB, against
`25268k/32764k` before.

## Hard-won gotchas

**n32 `long` is 32-bit.** The single most important fix. `-mips3` used to imply
`-mgp64`, making `long` 64-bit; it doesn't now. So `typedef unsigned long int u64`
silently became 32-bit and corrupted everything. Fixed in `kernel/stdint.h` and
`TGE/include/tge_types.h` as `unsigned long long`. `kernel/Makefile` uses
`-march=r5900`, not `-mips3`.

**Cast-lvalue rewrites need the RHS parenthesised.** `SMSCDVD.c` has 18 of
these. Correct:

```c
p = (struct dirTocEntry *)(cache + n);   /* bytes */
```

Wrong — scales by `sizeof`, silently:

```c
p = (struct dirTocEntry *)cache + n;
```

**Stale objects lie to you.** Two flag fixes looked like no-ops because `make`
relinked without recompiling. Wipe objects when a `CFLAGS` change appears to do
nothing.

## Assets

`png2rgb` accepts **only** 24-bit RGB or 32-bit RGBA, and inverts alpha as
`0x80 - alpha`. GS limits: max texture 1024×1024, 4 MB VRAM. Output is
640×448 (NTSC) / 640×512 (PAL) / 640×480 (VGA) — about 2.3× coarser than
`assets/mockup.png` suggests.

Current: `starfield.png` 731×512 RGB (replaced the original clouds),
`penguin.png` 128×128 RGBA. `loader/graphic.cpp` refers to `texStarfield` /
`getTexture("starfield.rgb")`.

**Dynamic text can't be restyled.** It goes through `gsKit_fontm_print_scaled`
using `GSFONTM`, the PS2 BIOS ROM font — scalable, one typeface, not
swappable. Matching the mockup's typography means pre-rendering a glyph atlas
and writing a small renderer over `gsKit_prim_sprite_texture`. Static chrome
(title lockup, menu highlight, info panel, ×/△/○) needs no new code — it's
just textures.

## Conventions

- **No attribution in commits** — no `Co-Authored-By`, no "generated by".
- Mermaid diagrams need explicit `color:#1a1a1a`; the default text colour is
  unreadable against the node fills in dark mode.
- Docs are prose-first with tables for reference material, not bullet dumps.

## Related

`ps2bdmshite` — a separate repo of the same author's: NHDDL/Neutrino USB payload
(`ps2-usb/`), BlackRhino NFS root (`brl-nfs/`), VMC builder (`vmc-build/`).

It has uncommitted work that is deliberately left alone, and content its
`.gitignore` exists to keep out of version control. Do not copy anything from
it into this repo, and do not commit on its behalf.
