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

### HostFs is off by default, and nothing works without it

`[EmuCore] HostFs = false` is the default in a freshly generated
`PCSX2.ini`. With it off, every `host:` path silently fails: the loader
finds no `host:config.txt`, so no configuration is loaded and `AutoBootTime`
never applies; and `cannot open host:vmlinux.gz` follows if a config is
supplied another way.

The failure is quiet and easy to misread as a broken build, a broken
`host:` implementation, or a hung emulator. It cost hours here. A
long-configured `PCSX2.ini` may well have `HostFs = true` already, which
makes the difference between two machines baffling until you diff the inis.

```
HostFs = true
```

When it is on, the log states the root explicitly, which is worth checking:

```
HLE Host: Set 'host:' root path to: /path/to/kltest
```

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

`VFS: Mounted root` means the kernel has found and mounted the initrd, and
`Freeing unused kernel memory` immediately precedes the `execve` of `init` --
so between them they show the loader's job is finished and the handoff was
clean.

Under PCSX2 the screen currently stops there: the last line is `88k freed` and
no userspace output follows. On real hardware the same build carries on to a
shell prompt. See *What is still emulator-only*, below.

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

### The GPU is reachable under WSLg, and not worth using

Mesa defaults to `llvmpipe` — a CPU rasteriser — so PCSX2's "hardware"
renderer is software too unless told otherwise. The real GPU is reachable:
`/dev/dxg` exists, `d3d12_dri.so` ships, and three variables switch to it.

```sh
GALLIUM_DRIVER=d3d12 MESA_LOADER_DRIVER_OVERRIDE=d3d12 \
MESA_D3D12_DEFAULT_ADAPTER_NAME=Intel ./pcsx2-qt.AppImage ...
```

`GL_RENDERER` then reports `D3D12 (Intel(R) Graphics)` instead of `llvmpipe`.
The adapter name matters: a Parsec virtual display can enumerate first and is
not the real GPU.

It renders **incorrectly**, though: models come out with black striped blocks
over them, a texture-cache artefact of running PCSX2's GL backend on a
translation layer. Ratchet and Clank managed about 48% speed that way. The
software renderer (`Renderer = 13`) is correct and was already configured for
this reason — treat that setting as deliberate, not stale.

For actually playing anything, use the stock Windows install: it gets the GPU
natively and carries none of the instrumentation in our build, which costs
cycles on every event test.

### Input

A physical controller needs `usbipd-win` to attach the USB device into WSL,
which is rarely worth it. PCSX2's default keyboard bindings are enough to drive
the loader menu: **arrow keys** to move, **K** for Cross, Return for Start.

## SBIOS calls 191-194: not cosmetic

An earlier version of this document called these gaps optional and
cosmetic, on the evidence of a kernel that called them once and carried
on. That was wrong.

`SBR_CDVD_OPENCONFIG` (191) and `CLOSECONFIG` (192) were dispatch-table
zeros, so `sbios()` returned -1. PS2 Linux's `ps2sysconf` retries them.
Booting the BlackRhino 2.4 live kernel produced **101 calls to 191 and
100 to 192**, all failing, immediately followed by a SIF command
interrupt storm (`fid 0x80000009` at microsecond intervals) that the
boot never recovered from. A 100-iteration retry followed by collapse is
a retry limit being exhausted.

They are implemented now (see `TGE/sbios/scmd.c`), against ps2sdk's
protocol in `ee/rpc/cdvd/src/scmd.c` -- the same RPC server
(`CD_SERVER_SCMD 0x80000593`) and the same command numbers, which
`scmd.c` had defined and never used. Measured on the same boot:

| | before | after |
|---|---|---|
| log volume | 1.2 MB | 54 KB |
| `191 not implemented` | 101 | 0 |
| `192 not implemented` | 100 | 0 |
| SIF storm | thousands | 0 |

`WRITECONFIG` (194) is deliberately still absent: it writes the
console's stored configuration through undersized shared buffers with a
field mapping that is inferred rather than documented.

### The buffer that has to be its own

`READCONFIG` receives **0x408 bytes**. `sCmdRecvBuff` is **64**, because
no other S-command needs more than 16. Transplanting ps2sdk's size
without checking overruns it by 968 bytes into the SBIOS's fixed
`0xEFE0` region and wedges the kernel the instant the reply DMA lands --
with no fault reported, the trace simply stopping at the call after
`READCONFIG`. It has a dedicated buffer now.

---

## poweroff.irx binds forever after handoff

A SIF interrupt storm runs from partway through Linux's boot onwards.
It is `SIF_CMD_RPC_BIND` (`SIF_CMD_ID_SYSTEM | 9`), and the raw packet
identifies the culprit exactly:

```
w2  80000009   fid (RPC_BIND)
w6  00000515   rpc_id -- increments per packet, so these are distinct
w7  00072670   client -- a low IOP address, so IOP -> EE
w8  09090900   sid
```

`0x9090900` is `PWROFF_IRX` (ps2sdk `common/include/pwroff_rpc.h`).
`poweroff.irx`, which the loader loads, binds to an **EE-side** server
that only exists while kernelloader is running:

```c
while (sceSifBindRpc(&client, PWROFF_IRX, 0) < 0 || client.server == NULL)
        DelayThread(500);
```

There is no bail-out, so once kernelloader hands over, the module
retries for eternity and fires a SIF interrupt at the EE every ~7 ms,
matching that `DelayThread`. Survivable -- hardware boots fine with it --
but it is real, and it makes traces hard to read.

Note the sid is only visible by dumping the packet: Linux registers its
own handler for this command, so TGE's `sifrpc` `request_end()`, which
would print it, never sees these.

---

## What is still emulator-only

With the **2010** BlackRhino kernel, PCSX2 reaches
`Freeing unused kernel memory: 88k freed` and stops. Userspace never
starts, with either initrd -- including one whose `/sbin/init` is a shell
script that prints immediately, so a successful `execve` could not be
missed.

The same `kloader.elf`, kernel, initrd and `CONFIG.TXT`, written to a
USB stick, **boot to a shell on a real PS2**. So this is a PCSX2
artefact, not a loader or SBIOS fault.

Ruled out along the way, each by measurement rather than argument:
memory pressure (identical 32 MB on both), `init=` not arriving (it is
logged verbatim now), a PCSX2 regression since 2.6.3 (every data point
was taken with `HostFs` off and no kernel loaded), the CDVD config gaps
(fixed, still stalls) and `ps2ip` (still stalls without it).

### The 2012 kernel is worse, not better

Pairing a kernel and initrd from the same release sounds obviously right
and was a wrong turn. A 2x2 settles which component is at fault:

| kernel | initrd | result |
|---|---|---|
| 2010 | 2.2-era | reaches `88k freed` |
| 2010 | 2012 | reaches `88k freed` |
| 2012 | 2012 | stalls at `NET4:`, never finds the initrd |

The kernel is the variable. Use the **2010** kernel for emulator work.

---

## Two traps that are not PCSX2's fault

**`/mnt/c` is case-insensitive.** Copying `VMLINUX.GZ` next to an
existing `vmlinux.gz` silently replaces it, so a run you believe is
testing one kernel may be testing another. Give test payloads distinct
names (`vmlinux_old.gz`, `vmlinux_new.gz`), never case variants.

**PCSX2 escapes `timeout`.** The AppImage mounts its squashfs and
re-execs; the real process is reparented away from the wrapper, its argv
no longer contains the AppImage name, and its `comm` is truncated to 15
characters. `timeout`, `pkill -f <appimage>` and `pkill -x pcsx2-qt` all
fail to reap it. Use `tools/killpcsx2.sh`.
