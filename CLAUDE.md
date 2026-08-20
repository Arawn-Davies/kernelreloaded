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
| `linux/` | **submodule** → [`ps2linux`](https://github.com/Arawn-Davies/ps2linux). Everything about the guest OS and nothing about the loader: the Linux patch set, `kernelconfig`, `phase1/` (our from-source kernel build), the three `driver_*` trees |
| `ps2facts/` | **submodule** → [`ps2facts`](https://github.com/Arawn-Davies/ps2facts). Asks a console what it is and prints the answers; useful well beyond this project, hence its own repo |
| `tools/` | host-side helpers: `bin2s` (POSIX sh replacement for a tool modern ps2sdk dropped), `crc32gen`, `png2rgb`, `ppm2rgb`, `hello`, `pcsx2/`, the deploy scripts |
| `whiterhino/` | **submodule** → [`pcsx2-whiterhino`](https://github.com/Arawn-Davies/pcsx2-whiterhino), branch `whiterhino` (the repo's default; upstream's inherited `master` was dropped, since nothing here tracks it). A PCSX2 fork carrying WhiteRhino: EE TLB/MMU accuracy work, and `kload`/`dload`, two ways to boot PS2 Linux without a real console. `kload` embeds this repo's own `kloader.elf`, built fresh at `whiterhino/`'s build time — see `whiterhino/tools/build-windows.ps1` and `whiterhino/tools/build-kloader-resource.sh` |
| `assets/` | shipped textures; `assets/src/` holds unshipped source artwork; `assets/mcicons/` the memory-card icon |
| `docs/` | internals, build-environment, porting-notes; `docs/upstream/` holds upstream's own `readme.txt`, `install.txt`, `history.txt`, `TODO.txt` and `KNOWNPROBLEMS.txt` |

**The loader source itself was a submodule until 2026-08-12, then briefly a
nested `loader/` wrapper. It is neither now, and must not become either
again.** Upstream (citronalco/kernelloader) has been dead since 2017 and
nothing here can be pushed to it, so that pointer only ever referenced commits
that existed on one machine — cloning got an empty directory and GitHub's link
404'd. The old `patches/0001-modern-toolchain.patch` is gone too; its changes
are in the source, and its content survives in history at `708c8ff`.

**Do not re-add a submodule for the loader source, a patch-application step, or
a wrapper directory.** `Makefile`, `kernel/`, `TGE/`, `loader/`, `iop/` and
`include/` are this repo and stay in it.

**`linux/`, `ps2facts/` and `whiterhino/` are the three exceptions, the
first two added on 2026-08-16 and the third on 2026-08-17.** `ps2facts/` moved
here from `tools/ps2facts/` on 2026-08-18, to sit next to the other two
submodules instead of being the one nested a level deeper for no reason other
than history — it shares their exact status (its own repo, pushed and public)
and nothing reads it from inside `tools/`. An earlier version
of this file forbade splitting `linux/` out at all. That was wrong for the
reason kernelloader itself demonstrates: upstream never tracked a kernel tree
either — `buildlinux.sh` downloaded the source and patched it, exactly as
`phase1/build-kernel.sh` still does. Carrying the patch set next to the loader
was our addition, and it put 8,000 lines of `unitable.h` in a repo whose build
never reads a byte of it. The objection that mattered — that these pointers
would reference commits on one machine — does not apply, because all three are
pushed and public.

`whiterhino/` is not this project's build target — `./build.sh` never reads it,
same as `linux/` — but it is the one submodule that reads *this* repo's own
build output. `kernelreloaded` is the hub deliberately: it is the smallest,
most central piece of the whole project, and everything else (the guest OS,
the console-identification tool, now the emulator fork) hangs off it, not the
other way round.

**The coupling that argued against it is real and did not go away.**
`linux/phase1/patches/romcons-input.patch` exists because TGE implements
`SB_PUTCHAR` as `sio_putc()`, so renumbering SBIOS calls in `TGE/` is a change
to that patch — now two commits in two repos and a pointer bump, and `git
bisect` no longer crosses that seam on its own. Both sides say so in their
docs. Treat any SBIOS call-numbering change as a cross-repo change, and expect
to bisect the two halves separately.

Clone with `--recurse-submodules`, or `git submodule update --init` after the
fact. `./build.sh` does not read `linux/` at all, so a forgotten submodule
breaks the kernel build, never the loader build.

Large built artifacts — a `vmlinux`, an initrd — belong on a GitHub Release,
not in either repo and not in LFS.

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

What works, as of 2026-08-16:

- **PS2 Linux boots to an interactive shell**, on real hardware and under
  PCSX2. On hardware `bash` runs on the GS framebuffer console with a USB
  keyboard, with the full shared library set — `ld.so.1`, `libc`, `libncurses`,
  `libdl`. Confirmed on an SCPH-70003.
- **A phat boots too, since 2026-08-16** — SCPH-30003R, PAL, network adapter
  fitted, booting from USB. It needs **`EnableDev9=0`** in `config.txt`: the
  EE-side DEV9 register read hangs that console, and 0 is the only setting that
  reaches neither the probe nor `ps2dev9_init()`. See "Debugging on hardware".
  Two loader bugs stood in the way and are fixed — `ps2dev9.irx` was selected
  on the wrong axis so a phat with an adapter started the DEV9 interrupt relay
  without its driver, and `dev9Matches()` probed DEV9 hardware from inside
  `startModules()`.
- **Under PCSX2 it needs `tools/pcsx2/ee-tlb-fixes.patch`** — seven emulator
  bugs, six in the EE MMU and one in BIOS-syscall emulation. Stock PCSX2 stops
  dead at `Freeing unused kernel memory` in an endless TLB refill loop.
  `tools/pcsx2/redgreen.sh` demonstrates the difference in one command.
- **The kernel is built from source** by `linux/phase1/build-kernel.sh`, with
  our own Tux logo and `linux/phase1/patches/romcons-input.patch`, which makes
  the ROM console tty readable — without it every shell reads EOF and exits
  silently.
- **Boot to a shell takes about 19 seconds** under emulation, down from 61.
- **The loader shows its own log on screen** during the load, so a hang has a
  named last line instead of a frozen picture. See "Debugging on hardware".
- **`linux/phase2/` (the gcc 15-ported kernel, as opposed to phase1's period
  gcc 2.95.2 build) also boots to interactive `bash`, since 2026-08-20** —
  via WhiteRhino's kload, user-confirmed. Four real gcc-15-port bugs stood in
  the way, all in the fork/syscall/uaccess machinery, none in kernel logic;
  see `linux/phase2/README.md`'s "Boot bugs, found and fixed" for the full
  writeup. `bash` itself execs and runs, but the console it reads from
  currently delivers keystrokes one at a time instead of a full line —
  open, not yet root-caused, see that same README's "Not yet done".

Known not to work: `cdfs:` cannot read the PS2 Linux Live DVD, so its kernel
and initrd have to be loaded from `host:` or a card. See the end of
`docs/emulator-testing.md` — including what was already tried and reverted.

**`reboot` needs a power cycle on hardware.** `ab86e9e` fixed `sbcall_halt()`
discarding its mode — all three of halt, power off and restart used to power the
console off, which is upstream's `KNOWNPROBLEMS.txt` PR#26 from the other side.
Restart now jumps to the BIOS entry at `0xBFC00000`, and under PCSX2 that works
because the fork turns the jump into a full VM reset. On a real console it
restarts the EE while the IOP still holds Linux's state, and the result is a
black screen. Finishing it means issuing the `SifIopReset` sequence — a SIF DMA
reset packet plus the SMFLAG, RPCINIT and SUBADDR writes — from TGE, which
today has no SIF access of any kind. Confirmed on the SCPH-30003R.

Open, unstarted: a mode change that the display cannot sync to leaves a black
screen with no way back (`gsKit_init_screen()` is applied with no confirmation
or timeout — it wants the monitor-style revert-after-N-seconds); and there is no
DHCP, so the System Info panel's IP row shows `getMyIP()`'s 192.168.0.10 default
rather than a real lease. Under WhiteRhino specifically: guest-triggered
`reboot`/`restart` resets straight to normal BIOS/CDVD boot rather than
re-running the same kload boot (same kernel, initrd, cmdline) it started
with — needs a hook in `whiterhino`'s own reset path, not this repo.

## Debugging on hardware

**Never read the DEV9 revision register from the EE. Not on a slim, and not on
a phat either.** It hangs the console dead — not slowly, not returning nonsense
— wherever in the boot it is done: cold, after `ps2dev9.irx` has started, or
from the paint path. PCSX2 answers 0 and carries on, so none of it is visible
under emulation.

This was first found on a slim and written up as a slim problem, on the
reasoning that a slim has the adapter built in so only a phat is ambiguous and
worth probing. An SCPH-30003R disproved that on 2026-08-16: the same read hangs
it too. `dev9Matches()` therefore probes nothing at all now — it consults
`ps2dev9_probed()`, the cached answer, and a phat falls through to DEV9-absent
and the `intrelay-direct` path, which is what upstream and rickgaiser's fork
both do anyway.

The only EE-side read left is upstream's own `ps2dev9_init()`, in the non-slim
branch of `real_loader()` and guarded by `enableDev9`. On a phat that cannot
survive it, **`EnableDev9=0` in `config.txt` is the answer**: it skips both that
call and everything downstream of it. The cost is `bootinfo.pccard_type`, which
`arch/mips/ps2/setup.c` turns into `ps2_pccard_present` — so Linux sees no HDD
and no ethernet, and networking on such a console would have to come from
IOP-side `smaprpc` the way rickgaiser does it.

**Where a hardware probe is called matters as much as whether.** `dev9Matches()`
is evaluated at the top of `startModules()`' loop, *before* the
`graphic_setStatusMessage()` and `kprintf()` that name the module. So when it
hung, the boot log stopped on whatever module happened to load last, and
disabling modules made the hang appear to move earlier — poweroff.irx, then
`rom1:SDRDRV`, then `rom0:PADMAN` — when it was the same instruction every
time. Three boots were spent gating innocent modules before the loop was read.
If a log line names the last thing that *succeeded* rather than the thing that
failed, suspect the code between the two.

**ps2link cannot log a boot.** It is an IOP module, and `real_loader()` calls
`SifIopReset()` about a second in, so the forwarder dies long before anything
interesting happens. `ps2client execee host:kloader.elf` is still worth having
— it puts a new build on the console without touching the USB stick — but
expect zero output. Build `ps2client` from `ps2dev/ps2client` on the host:
the copy inside the container is musl-linked and will not run on a glibc host,
and Docker Desktop's VM cannot route to the LAN, so `--network host` is not a
way round it.

`kprintf()` goes to SIO, which needs a hardware modification to read, so the
loader draws its last 16 lines in a panel during the load (`loader/bootlog.c`).
Two things about it are load-bearing. It repaints on every `kprintf()` rather
than only when the progress bar moves — otherwise a stage that reports no
percentage leaves the screen frozen on the previous stage's last line, and a
hang reads as a hang in the stage before it. And it draws from primitives, not
a texture, so it still works when what is broken is a texture upload.

`tools/pcsx2/emu.sh` finds, stops and launches emulator instances. Use it
rather than `pkill`: `comm` is truncated to 15 characters so `pkill -x` matches
nothing while reporting success, `pkill -f` matches the caller's own command
line and kills the shell, and one AppImage is two PIDs.

Video on a component/HDMI adapter: `crtmode=dtv` with
`video=ps2fb:dtv,720x480-32` is progressive and does not shimmer. The loader's
own PAL default stays interlaced `GS_FIELD` because it must also serve
composite — `GS_FRAME` reads the buffer at half vertical resolution and
squashes the UI into the top half of the screen.

## Pretending to have more RAM

`FAKE_EXTRA_RAM` in `config.mk` (default 64) makes the loader tell Linux the
console has more than a retail PS2's 32MB. The chain is short: `loader.c` puts
it in `bootinfo.maxmem`, and the kernel's `arch/mips/ps2/prom.c` does
`add_memory_region(0, ps2_bootinfo->maxmem & PAGE_MASK, BOOT_MEM_RAM)`. No
kernel change is involved -- it is simply what Linux is told.

**The memory has to actually exist.** Under PCSX2 that means `ExtraMemory=true`,
which maps the 128MB T10K devkit layout; 64 sits inside it. A retail console has
32MB and nothing else, and told otherwise the kernel hands out pages that are
not there.

**It is not opt-in, whatever an earlier version of this file said.**
`config.mk` ships `FAKE_EXTRA_RAM = 64`, so a plain `./build.sh` produces a
loader that tells a retail console it has 64MB. The `kprintf` on every boot is
the only guard, and a guard you have to read is not a default. For hardware,
build with the variable blanked:

```sh
./build.sh FAKE_EXTRA_RAM=
```

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
`assets/src/mockup.png` suggests. A real screenshot is in the README.

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
