# Brief: reproduce the PS2 Linux boot on native Windows

You are running on a Windows development machine. Everything below was done on a
separate WSL2/Linux box; the point of this exercise is to find out whether any of
it was a Linux-only artefact.

Read `CLAUDE.md` at the repo root first — it explains the project. This file is
only about the Windows job.

## The goal, in one sentence

Build PCSX2 with this repo's patches using MSVC, boot PS2 Linux from `DISC.BIN`,
and report whether it behaves the same as it did on Linux.

## Why it matters

Seven EE TLB/MMU/syscall fixes plus five newer patches live in `tools/pcsx2/`.
They are candidates for an upstream PCSX2 PR. Two fair objections stand against
them: they were only ever built and tested on Linux, and WSL2 adds a layer of
virtualisation between emulator and host. Reproducing on native Windows answers
both. **Do not open a PR** — the maintainer wants to review first.

## What to run

```powershell
cd D:\kernelreloaded\tools\pcsx2
git pull
.\build-windows.ps1 -InstallPrereqs
```

The script is self-documenting; read it before running it. It checks
prerequisites, adds the Visual Studio C++ workload if missing, clones PCSX2 at
the pinned commit `337daf7ed`, applies the patches in dependency order, builds
the Qt/FFmpeg dependency tree, then builds PCSX2.

`build-dependencies.bat` takes hours. That is expected, not a hang. On later
runs pass `-SkipDeps`.

## State when this was written

- VS 2022 **Community** is installed but **without the C++ workload**, so
  `VC\Auxiliary\Build\vcvars64.bat` does not exist. `-InstallPrereqs` fixes this
  by running `vs_installer modify --add Microsoft.VisualStudio.Workload.NativeDesktop`.
  Verify with `Test-Path` afterwards; it has silently not-installed twice.
- The clone is at `D:\kernelreloaded`, the build goes to `D:\ps2dev`.
- `DISC.BIN` is already on this machine.
- Nothing has been built successfully here yet.

## Traps already paid for

- **`vswhere` queries differ.** `build-dependencies.bat` uses
  `-version "[17,18)" -latest` with **no** `-requires`, so it picks the newest
  VS2022 whether or not it has the C++ workload. Check the install *it* selects,
  not one you would.
- **`vs_installer` needs `--passive` or `--quiet`.** Given neither it opens the
  GUI and returns immediately, so `--wait` waits on nothing.
- **Line endings.** Clone PCSX2 with `core.autocrlf=false`. Git for Windows
  otherwise rewrites the tree to CRLF and every LF patch fails on line endings
  alone, with a symptom that reads like a corrupt patch. `.gitattributes` in this
  repo already protects the patch files themselves.
- **`build-dependencies.bat` hardcodes** `C:\Program Files\7-Zip\7z.exe` and
  `C:\Program Files\Git\usr\bin\{patch,bash}.exe` with no search. Full Git for
  Windows, not MinGit.

## Booting PS2 Linux once PCSX2 builds

You need `kloader.elf`, `vmlinux_net.gz`, `DISC.BIN` and a PS2 BIOS. Build
`kloader.elf` from this repo (`./build.sh`, needs Docker) or copy it over.
`vmlinux_net.gz` is a kernel built by `linux/phase1/build-kernel.sh` with
`CONFIG_IP_PNP` and `CONFIG_ROOT_NFS`; copy it rather than rebuilding.

In PCSX2's settings before booting:

- **DEV9 → HDD**: enable, pointing at `DISC.BIN`. The setting lives in
  **`[DEV9/Hdd]`** in `PCSX2.ini`. A flat `[DEV9]` section is an older layout
  that current builds ignore *silently* — this cost most of a day to find.
- **CPU**: the EE recompiler was off on Linux (`EnableEE = false`) because of a
  recompiler crash predating the TLB fixes. Try it **on** here; if it holds, it
  is a large speedup and worth reporting.

`config.txt` beside `kloader.elf`:

```
KernelFileName=host:vmlinux_net.gz
KernelParameter=root=/dev/hda rw romcons console=romcons console=tty0
AutoBootTime=1
```

`DISC.BIN` is a bare ext2 filesystem with **no partition table**, so it is
`/dev/hda`, never `hda1`.

## What to report back

1. Does it build clean under MSVC? Any warnings or errors the Linux build did
   not produce are the single most valuable output of this exercise.
2. Does the HDD mount **read-write**, with no `no DRQ after issuing WRITE`?
   That exercises `ata-pio-write.patch`, which implements ATA PIO data-out that
   PCSX2 lacks entirely.
3. Does init reach `INIT: Entering runlevel: 2` without SIGSEGV in `rcS` or
   `grep`? That exercises `syscall-guard.patch`.
4. Is the log around 15,000 lines rather than 268,000? That is the two
   rate-limit patches working.
5. Does the EE recompiler work?

## Rules

- **Do not open a pull request** against PCSX2, or push to any remote other than
  this repo's `origin`, without being asked.
- Commit fixes to this repo as you go; no attribution lines in commit messages
  (`CLAUDE.md` explains).
- If a step fails, prefer fixing `build-windows.ps1` so the fix is durable, over
  a one-off command in the terminal.
- Be honest about what was and was not verified. The value here is evidence,
  and an overstated result is worse than none.
