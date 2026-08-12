# Minimal userspace tests

Each of these is static, libc-free and relocation-free, built with the phase 1
toolchain and booted as PID 1 via `init=`. They exist because "bash does not
work" is not a diagnosis, and every structural theory about *why* turned out to
be wrong. Each test rules one out permanently.

```sh
ee-gcc -nostdlib -static -mno-abicalls -fno-pic -G0 -o NAME NAME.S
```

`-mno-abicalls -fno-pic` is required: without it `la` becomes a GOT load through
`$gp`, which nothing sets up at process entry with `-nostdlib`. binutils 2.9
also will not evaluate a cross-section length expression, so string lengths are
hardcoded.

Install into an initrd and boot with `init=/NAME`. `statictest` additionally
wants `/dev/ttyS0`, which the stock initrd lacks — romcons is `TTY_MAJOR` with
`minor_start` 64:

```sh
debugfs -w image <<< $'cd /dev\nmknod ttyS0 c 4 64\nsif ttyS0 mode 020666'
```

| test | proves | result |
|---|---|---|
| `statictest.S` | userspace runs at all; both consoles reachable | prints on screen and in the log, spins, no faults |
| `forktest.S` | `fork()` and copy-on-write | all three markers, no faults |
| `ttytest.S` | the console tty answers `TCGETS` | `TCGETS-OK` |

Stock binaries fill in the rest, run the same way:

| binary | size | result |
|---|---|---|
| `/sbin/init` | 44 KB dynamic | runs |
| `/bin/ls` | 123 KB dynamic | lists the initrd, exits (kernel then panics, as it must) |
| `/sbin/e2fsck` | 632 KB dynamic | prints usage cleanly, exits |
| `/bin/bash` | 944 KB dynamic | ~880,000 faults on address `0x3` |

So it is not size, not dynamic linking, not `fork`, not copy-on-write, and not
terminal ioctls. Everything a shell needs works in isolation.

A useful trick: a binary that **exits** is easier to read than one that hangs,
because the kernel must panic with "Attempted to kill init" when PID 1 leaves —
and that is a printk, so it reaches the log through romcons. Silence means the
process is still alive; a panic means it ran to completion.

Note `argv[0]` is `"init"` for whatever the kernel execs, so error messages come
out prefixed `init:` regardless of which binary is running. `init: write error`
from `ls` is `ls` failing to write, not init.
