# Phase 1 — building the PS2 Linux kernel in a container

Everything that stalled the emulator work stalled for the same reason: once
kernelloader hands over, PS2 Linux writes its console to the GS framebuffer and
nothing it says reaches any log. Diagnosis meant reading messages off a screen
and relaying them by hand, which is slow, lossy, and produced at least two
confident conclusions that later turned out to be wrong.

The kernel source solves that outright, and better than the patch route this
plan originally proposed. `CONFIG_PS2_SERIAL_CONSOLE` builds
`arch/mips/ps2/romcons.c`, whose `romcons_console_write()` emits each character
with `sbios(SB_PUTCHAR)`. That is SBIOS call 3, which TGE implements in
`TGE/sbios/misc.c` as `sbcall_putc()` -> `sio_putc()` -- the same SIO path
kernelloader's own messages already take into the PCSX2 log:

```
printk -> romcons -> sbios(SB_PUTCHAR) -> TGE sbcall_putc() -> sio_putc() -> log
```

Every link already exists. It needs no patch, no IOP module, and it is a stock
config option already set in the repo's `kernelconfig`, switched on at boot by
a `romcons` kernel argument (`__setup("romcons", ...)`). `romcons` also reads
via `SB_GETCHAR`, which TGE serves from the EE SIO RX FIFO, so console *input*
is wired too -- though whether PCSX2 ever feeds that FIFO is untested.

But a config option is only useful if we build the kernel, and so far we have
been booting a prebuilt BlackRhino image we cannot change.

So phase 1 is not really about the kernel. It is about being able to see.

## Why a container

`buildlinux.sh` at the repo root already encodes the whole recipe — fetch a
period cross-toolchain and the 2.4.17 PS2 source, patch, build. It is just
written for a 2009 Debian box: it wants `sudo`, installs into `/usr/local/ps2`,
and falls back to `dchroot`.

That `dchroot` is the tell. `gcc-2.95.2-cross.tar.gz` contains **32-bit x86
binaries**, so on a modern 64-bit host they will not run at all without an i386
runtime. A container solves precisely that — `dpkg --add-architecture i386`
plus `libc6:i386` and the 2001 binaries run unmodified — while also keeping the
toolchain out of your real `/usr/local` and making the build reproducible.

This is the same argument as the root `Dockerfile`, which exists so the loader
builds identically on WSL2, Linux, or anywhere else.

## What gets built

Source is `linux-2.4.17_ps2.tar.bz2` from the kernelloader SourceForge project.
All three upstream artefacts were confirmed downloadable while writing this;
sizes are recorded so a mirror can be checked against them.

| artefact | size |
|---|---|
| `binutils-2.9EE-cross.tar.gz` | 4,089,969 |
| `gcc-2.95.2-cross.tar.gz` | 14,312,385 |
| `linux-2.4.17_ps2.tar.bz2` | 21,458,335 |

Patches divide into the three `buildlinux.sh` already applies, the two we need,
and the rest, which are for hardware we are not targeting yet.

| patch | what it does | phase 1 |
|---|---|---|
| `all_fat_and_slim` | base PS2 support, fat and slim | yes, as upstream |
| `no-bwlinux-check` | drops the BlackRhino signature check in `prom.c` | yes, as upstream |
| `nfsroot-via-tcp` | NFS root over TCP rather than UDP | yes, as upstream |
| `initrd` | required to compile with the embedded ramdisk disabled | yes — we boot an initrd |
| `printk` | `printk` to the IOP via `sharedmem.irx` | no — superseded by `romcons` |
| `iop-debug` | adds `/proc/ps2iopdebug`, EE↔IOP debug RPC | later |
| `sbios_debug` | TGE SBIOS output visible in `dmesg`; needs `CALLBACK_DEBUG = yes` | later |
| `irq-fix` | interrupt arriving before its handler is registered | no — slim PSTwo |
| `rpc-irq` | slim PSTwo interrupts; needs `intrelay*rpc.irx` | no — slim PSTwo |

## Why not the printk patch

It was the original plan here, and `romcons` is strictly better. The patch's own
header warns that with it applied and `sharedmem.irx` *not* loaded the result is
a deadlock and a black screen — which is indistinguishable from most of the
failures we are trying to diagnose, and couples the kernel build to loader
configuration. `romcons` has no such coupling and no such failure mode.

Keep the patch in reserve for anything `romcons` cannot show, since it reports
from the IOP side rather than the EE.

## Layout

```
phase1/
    README.md         this plan
    build-kernel.sh   the whole build, start to finish
```

`build-kernel.sh` is buildlinux.sh's recipe with the 2009-isms removed — no
sudo, no dchroot, no writing to a workstation's real `/usr/local`. It wants a
disposable container, and an LXC serves as well as Docker: the only hard
requirement is an i386 runtime for the 2001 toolchain binaries. A Dockerfile is
still worth adding for people without one to hand, but it would only wrap this
script.

Built and verified on 10.0.1.202 (Ubuntu 24.04 LXC), where the raw filesystem
beats drvfs badly on a tree of this shape.

## Build flow

Fetch the three tarballs once and cache them (they are ~40 MB together and the
source never changes). Unpack the toolchain into `/usr/local/ps2` in the image
so `CROSS_COMPILE=/usr/local/ps2/bin/ee-` matches what `buildlinux.sh` expects.
Unpack the kernel, apply the patch set above, seed `.config` from the repo's
`kernelconfig` — 864 lines, generated by `make menuconfig` against this tree —
run `make oldconfig` to absorb any drift, then `make dep` and build.

## Two things the tarball does not contain

`fs/Makefile` has `subdir-$(CONFIG_PS2_FS) += ps2fs` and the same for
`unionfs`. With those symbols unset the lines become `subdir-`, and 2.4's
`Rules.make` folds `subdir-y`, `-m`, `-n` and `-` alike into `ALL_SUB_DIRS`,
which `make dep` descends into regardless of configuration. So both directories
must exist even when nothing selects them.

They ship in this repo, as `driver_ps2fs/ps2fs` and `driver_unionfs/unionfs`,
and are copied into `fs/` before building.

## The host-compiler trap that wasn't

This section predicted the first failure and got it wrong, which is worth
keeping. The reasoning was sound — a cross-compiled kernel still builds *host*
tools with the *host* compiler, and 2.4's `scripts/` are 2001-era C that modern
GCC should reject over implicit declarations.

It simply did not happen. gcc 13.3.0 built `mkdep`, `conmakehash` and
`split-include` without complaint, and `make dep` finished with zero errors and
229 `.depend` files. The host tools are small and plain enough to have survived
two decades of C standards tightening.

What actually broke was all build-system trivia, none of it compiler-related:

| symptom | cause |
|---|---|
| `arch/x86_64/Makefile: No such file` | 2.4 does `ARCH := $(shell uname -m)`; a makefile assignment beats the environment, so `ARCH` must be passed on the command line |
| `ps2fs: No such file or directory` | `subdir-` lands in `ALL_SUB_DIRS` even unconfigured, and `make dep` descends into it |
| `No rule to make target 'smaprpc.o'` | `CONFIG_PS2_ETHER_SMAP=y` needs `driver_slim_smaprpc/` copied in |

## How we will know it worked

In order, each step meaningful on its own:

1. ~~The toolchain runs at all.~~ **Done** — `ee-gcc --version` reports 2.95.2
   on Ubuntu 24.04 once `libc6:i386` is installed.
2. ~~The kernel compiles.~~ **Done** — `vmlinux`, 3,969,094 bytes, 32-bit MIPS
   ELF, entry `0x80010490`. That entry is *identical* to the prebuilt
   BlackRhino image, which is a good sign the build is sound. It is also not
   stripped, so `System.map` resolves any address the PCSX2 EE state dump
   reports back to a function name — something we never had before.
   `romcons_console_write` is present at `0x801026d0`.
3. It boots under PCSX2 through kernelloader and reaches at least
   `Freeing unused kernel memory` — i.e. no worse than the prebuilt kernel,
   which is the control.
4. Booted with `romcons` on the command line, kernel messages appear **in the
   PCSX2 log**. That is the deliverable.

Step 4 is what unblocks the bash investigation, where the open question is why
a shell fails while smaller binaries exec fine.

## Not in scope

Modern GCC. That is phase 2, and it needs a `mipsel-linux-gnu` toolchain rather
than ps2dev's bare-metal `mips64r5900el-ps2-elf`, plus a real porting pass. The
value of doing phase 1 first is that it produces a known-good kernel to diff
against, so a phase 2 failure can be told apart from a miscompile.
