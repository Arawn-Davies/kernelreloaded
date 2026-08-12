# Testing under PCSX2

Booting kernelreloaded in an emulator catches a useful class of fault without
touching hardware — the loader UI, the config search, ELF loading, the SBIOS
handoff and the first seconds of Linux. It cannot replace a console, and a
couple of the failures below are the emulator's rather than ours, which is
exactly why they are written down: each one cost an hour to identify and looks
like a kernelreloaded bug until you find the evidence.

Everything here was established against **PCSX2 2.6.3** on Windows with a PAL
`SCPH-30004R` BIOS.

---

## What it can and cannot tell you

PS2 Linux writes its console to the **GS framebuffer**, not to SIO. So `printk`
output never reaches the PCSX2 log: there is no `Linux version`, no `VFS:` line
to grep for. Everything after the handoff has to be read off the screen.

kernelloader's own output *does* reach the log, via SIO, provided
`EnableEEConsole = true`. Turning that off to quieten the log also removes every
`Status:` line and `Jump to kernel!` — easy to mistake for a regression.

| Question | Emulator can answer it? |
|---|---|
| Does the loader draw, and are textures present? | yes |
| Does the config search find the right file? | yes — logged |
| Do kernel and initrd load, at the right addresses? | yes — logged |
| Does the SBIOS install and hand off? | yes — `Jump to kernel!` |
| Does Linux mount root and reach userspace? | only on screen |
| Does networking / USB / HDD work? | no — see below |
| Is DMA timing right on real hardware? | no |

---

## Staging a boot

PCSX2 exposes a `host:` filesystem rooted at the directory the ELF was loaded
from, which is by far the easiest route: no memory-card image to build, no ISO
to master. Put everything in one folder:

```
kltest/
    kloader.elf     copied from bin/
    config.txt      loaded via host:config.txt
    vmlinux.gz
    initrd.gz
```

```
pcsx2-qt.exe -logfile <path>\boot.txt -fastboot -- <path>\kloader.elf
```

An initrd-root config, which needs no network and so suits emulation:

```
KernelFileName=host:vmlinux.gz
InitrdFileName=host:initrd.gz
KernelParameter=root=/dev/ram0 rw ramdisk_size=16384
AutoBootTime=5
```

`AutoBootTime` is worth setting for unattended runs — otherwise the loader waits
at the menu for a pad press.

### Which config path actually works

Of the startup search chain (see
[`kernelloader-internals.md`](kernelloader-internals.md)), only some are
reachable under emulation:

| Path | Under PCSX2 |
|---|---|
| `mc0:` / `mc1:` | works, but you must write files into a memory-card image first |
| `mass0:` | no — PCSX2 emulates no USB mass storage kernelloader can mount |
| `cdfs:` | no — gated on `isDVDVSupported()`, which returns `eromdrvSupport`, and `rom1:EROMDRV` fails to load under emulation |
| `host:` | **yes** — the practical choice |

That `cdfs:` dependency is not obvious: mastering an ISO with `config.txt` on it
looks like it should work, and the file is simply never read because the EROM
driver never loaded.

---

## Emulator settings that matter

### The EE recompiler crashes; use the interpreter

With the default recompiler, PCSX2 dies about two seconds after `Jump to
kernel!`, mid-TLB-storm. It is a hard crash, and the Windows Application event
log names the cause:

```
Faulting application name: pcsx2-qt.exe, version: 2.6.3.0
Faulting module name: unknown, version: 0.0.0.0
Exception code: 0xc0000005
```

`Faulting module: unknown` means the fault is in **JIT-generated code**, not in
any PCSX2 binary — the EE recompiler mishandling what Linux does with the TLB.
Games never exercise the MMU this way, so it is unsurprising and it is not our
bug.

| `[EmuCore/CPU/Recompiler]` | Result |
|---|---|
| `EnableEE = true`, `EnableFastmem = true` (default) | crash ~2 s after handoff |
| `EnableEE = true`, `EnableFastmem = false` | crash ~6 s after handoff |
| **`EnableEE = false`** | **boots to userspace** |

Set `EnableEE = false` in `inis/PCSX2.ini`. Fastmem alone is not enough; it only
moves the crash a few seconds later.

### The interpreter then floods the log

The EE interpreter emits `COP0_TLBWR` on **every TLB write**, and no setting in
`PCSX2.ini` suppresses it — `[EmuCore/TraceLog]` is entirely `false` and
`EnableVerbose = false` changes nothing. A two-minute run produced:

```
623 MB logfile, 16 million lines, 8 million of them COP0_TLBWR
```

The emulator ends up spending its time formatting log lines: speed drops to
about **1%**, and the Qt log window stops responding, which reads as a hang in
the guest but is not one. Disable `EnableLogWindow`, and disable the file/system
console sinks when you do not need them.

### "Copying files and start..." is not a hang

The loader's on-screen status freezes on this message and never updates again.
It looks exactly like a lock-up. It is not — it is the last frame the loader
ever paints. The line immediately before it in the log explains why:

```
Flush TLBs (printf will not work after this).
TLBs flushed.
Jump to kernel!
```

`loader.c:2349` prints that status and then disables interrupts, enters kernel
mode, flushes the caches and calls `jump2kernelspace()`. From that point the
loader cannot draw, so whatever was on screen stays there until Linux brings up
its own framebuffer console. If Linux never appears, suspect the emulator's
renderer, not the loader — check the log for `Jump to kernel!` first, which
proves the handoff succeeded.

### Do not pass `-gameargs`

`-gameargs` makes PCSX2 inject argv via `eeloadHook2`, and that write trips
kernelloader's self-check:

```
kloader ELF integrity check failed in section .text.
```

The check is correct — memory really was modified. Without the flag the same
build reports `kloader section .text CRC32 OK`. kernelloader only accepts `-d`,
`--no-cdvd` and `--fix-no-disc` anyway, so there is rarely a reason to use it.

### rom1 / rom2

PCSX2 logs `BIOS rom1 module not found, skipping` unless the extension ROMs sit
beside the BIOS image, named after it — `<bios name>.rom1` and `.rom2`, from the
usual `EROM.BIN` and `ROM2.BIN`. Supplying them lets kernelloader get as far as
opening `rom1:EROMDRV`; the module still fails to load under emulation, which is
survivable because the loader no longer blocks on it.

---

## What a good boot looks like

From the log, in order:

```
kloader section .text CRC32 OK
kloader section .rodata CRC32 OK
No config at "mc0:kloader/config.txt", trying "mc1:kloader/config.txt".
Loaded configuration from "host:config.txt".
host:vmlinux.gz size 3052368
13389824 bytes for initrd available.
host:initrd.gz size 4162392
initrd_start 0x8033b000 0x003f8358
Patched sbios_iopaddr 0x00019600
Jump to kernel!
```

Then, on screen only:

```
RAMDISK: Compressed image found at block 0
Freeing initrd memory: 4064k freed
VFS: Mounted root (ext2 filesystem).
Freeing unused kernel memory: 88k freed
```

`VFS: Mounted root` and `Freeing unused kernel memory` together mean the kernel
has handed control to userspace — the point at which the loader's job is
demonstrably finished.

### Messages that are expected, not faults

| On screen | Why |
|---|---|
| `PlayStation 2 HDD/Ethernet device NOT present` | no DEV9 hardware emulated |
| `IP-Config: No network devices available` | follows from the above |
| `ps2sysconf: can't open osd` | see the SBIOS gap below |

---

## Building a patched PCSX2 to silence the interpreter

The `COP0_TLBWR` flood above cannot be configured away, because
`pcsx2/COP0.cpp` logs it with `DevCon.Warning` rather than the trace-log
system:

```cpp
void TLBWR() {
    ...
    DevCon.Warning("COP0_TLBWR %d:%x,%x,%x,%x\n", ...);   // always on
```

Note `TLBWI` directly above uses the trace-gated `COP0_LOG` instead — the
inconsistency is upstream's, and it is why only `TLBWR` floods.

Commenting out that one call (all three lines of it — the statement wraps, and
commenting only the first leaves an unmatched paren that will not compile) is
the entire fix:

| | Stock 2.6.3 | Patched |
|---|---|---|
| `COP0_TLBWR` lines in one boot | 8,026,426 | 0 |
| Logfile size | 623 MB | 45 KB |
| Reaches `Jump to kernel!` | yes | yes |

PCSX2 does not use system Qt — `build-dependencies-qt.sh` compiles Qt, FFmpeg,
SDL3, shaderc, KDDockWidgets and more into `$HOME/deps`, which is the bulk of
the build. Producing an AppImage via `appimage-qt.sh` makes the result portable
to a different machine, provided it is built on the same distro release as the
one that will run it.

### Running the AppImage under WSLg

WSLg's GL stack routes through Zink/d3d12 and can fail to initialise:

```
MESA: error: ZINK: failed to choose pdev
libEGL warning: egl: failed to create dri2 screen
```

When that happens the window keeps displaying a stale frame while emulation
continues underneath — which reads as a hang, especially combined with the
frozen "Copying files and start..." status. Force software GL:

```bash
LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe \
MESA_LOADER_DRIVER_OVERRIDE=llvmpipe ./pcsx2-qt.AppImage -- kloader.elf
```

There is no real cost: with the EE interpreter the emulator is CPU-bound at
roughly 1% regardless of renderer.

### Input

A physical controller needs `usbipd-win` to attach the USB device into WSL,
which is rarely worth it. PCSX2's default keyboard bindings are enough to drive
the loader menu: **arrow keys** to move, **K** for Cross, Return for Start.

## A real gap: SBIOS calls 191–194

Repeated during boot:

```
sbios_rpc: RPC failed, func=191 result=-1
```

`sbios_rpc` is Linux-side, so this is PS2 Linux asking our SBIOS for a call it
does not implement. **191 is `SBR_CDVD_OPENCONFIG`** (`kernel/sbcall.h:585`),
and TGE implements none of the family:

| Call | Number |
|---|---|
| `SBR_CDVD_OPENCONFIG` | 191 |
| `SBR_CDVD_CLOSECONFIG` | 192 |
| `SBR_CDVD_READCONFIG` | 193 |
| `SBR_CDVD_WRITECONFIG` | 194 |

They postdate the TGE sources — `sbcall.h`'s own change history lists them as a
later addition. The consequence is the `ps2sysconf: can't open osd` line
immediately after: Linux cannot read the console's OSD configuration, so
language, timezone and screen settings fall back to defaults.

This is **cosmetic** — the boot completes regardless, as the milestones above
show — but it is a genuine hole in `TGE/sbios/cdvd.c` if anyone wants
`ps2sysconf` working.
