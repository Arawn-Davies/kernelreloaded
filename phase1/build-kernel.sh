#!/usr/bin/env bash
#
# Build the PS2 Linux 2.4.17 kernel with the period cross-toolchain.
#
# This is buildlinux.sh's recipe with the 2009-isms removed: no sudo, no
# dchroot, no writing to a real /usr/local on your workstation. Run it in a
# disposable container -- an LXC or Docker, either works, since the only real
# requirement is an i386 runtime for the 2001 toolchain binaries.
#
# WHY gcc 2.95.2 AND NOT THE ps2dev DOCKER IMAGE: that image targets
# mips64r5900el-ps2-elf -- bare metal, newlib -- which is right for EE homebrew
# and wrong for a Linux kernel. This toolchain is mipsEEel-linux, an actual
# Linux target. Phase 2 is about doing it with a modern compiler; see README.md.
#
# Usage:  ./build-kernel.sh [kernelreloaded-repo]
#
# Output: $PREFIX/build/linux-2.4.17_ps2/vmlinux, plus System.map. The kernel
# is NOT stripped, so the map resolves any address the PCSX2 EE state dump
# reports back to a function name.

set -euo pipefail

REPO="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
PREFIX="${PHASE1:-/root/phase1}"
TOOLS=/usr/local/ps2
SF="https://sourceforge.net/projects/kernelloader/files"

msg() { printf '\n=== %s\n' "$*"; }

# ---------------------------------------------------------------- toolchain --
# The tarballs unpack to /usr/local/ps2, which is what CROSS_COMPILE points at
# below and what buildlinux.sh has always used. libc6:i386 is the whole reason
# the original script needed dchroot: these are 32-bit x86 binaries from 2001.
if [ ! -x "$TOOLS/bin/ee-gcc" ]; then
    msg "installing i386 runtime"
    dpkg --add-architecture i386
    apt-get update -qq
    apt-get install -y -qq libc6:i386

    mkdir -p "$PREFIX/src"
    for f in binutils-2.9EE-cross.tar.gz gcc-2.95.2-cross.tar.gz; do
        [ -s "$PREFIX/src/$f" ] || {
            msg "fetching $f"
            curl -sL --max-time 300 -o "$PREFIX/src/$f" \
                "$SF/Sony%20Linux%20Toolkit/Package%20Update%20Files/ps2stuff/$f/download"
        }
        tar -C /usr/local -xzf "$PREFIX/src/$f"
    done
fi
msg "toolchain: $("$TOOLS/bin/ee-gcc" --version | head -1)"

# ------------------------------------------------------------------ sources --
KSRC="$PREFIX/build/linux-2.4.17_ps2"
if [ ! -d "$KSRC" ]; then
    [ -s "$PREFIX/src/linux-2.4.17_ps2.tar.bz2" ] || {
        msg "fetching kernel source"
        curl -sL --max-time 600 -o "$PREFIX/src/linux-2.4.17_ps2.tar.bz2" \
            "$SF/Linux%202.4/Linux%202.4.17%20Kernel%20Source/linux-2.4.17_ps2.tar.bz2/download"
    }
    mkdir -p "$PREFIX/build"
    tar -C "$PREFIX/build" -xjf "$PREFIX/src/linux-2.4.17_ps2.tar.bz2"

    # The three buildlinux.sh applies. The initrd patch is NOT applied: it is
    # already present in this tarball (patch -R --dry-run confirms it), so
    # applying it fails and that failure is expected, not a problem.
    msg "patching"
    cd "$KSRC"
    for p in all_fat_and_slim no-bwlinux-check nfsroot-via-tcp; do
        patch -p1 -s <"$REPO/patches/linux-2.4.17_ps2-$p.patch"
        echo "  applied $p"
    done

    # Ours, not upstream's -- hence phase1/patches rather than patches/.
    # Without this the ROM console tty is write-only and every shell started
    # on it reads EOF and exits on the spot. See the patch header.
    for p in romcons-input; do
        patch -p1 -s <"$REPO/phase1/patches/$p.patch"
        echo "  applied $p (ours)"
    done

    # Three drivers live in this repo rather than the tarball.
    #
    # ps2fs and unionfs are needed even though nothing selects them: fs/Makefile
    # says "subdir-$(CONFIG_PS2_FS) += ps2fs", and with the symbol unset that
    # becomes plain "subdir-", which 2.4's Rules.make still folds into
    # ALL_SUB_DIRS -- and `make dep` descends into every one of those. So the
    # directory has to exist or dep dies, configured or not.
    #
    # smaprpc is a genuine dependency: CONFIG_PS2_ETHER_SMAP=y builds
    # ps2smaprpc.o, which lists smaprpc.o among its objects.
    msg "adding out-of-tree drivers"
    cp -r "$REPO/driver_ps2fs/ps2fs"     "$KSRC/fs/"
    cp -r "$REPO/driver_unionfs/unionfs" "$KSRC/fs/"
    cp "$REPO/driver_slim_smaprpc/smaprpc.c" "$REPO/driver_slim_smaprpc/smaprpc.h" \
       "$KSRC/drivers/ps2/"
fi

cd "$KSRC"

# ------------------------------------------------------------------- config --
# The repo's kernelconfig has CONFIG_PS2_SERIAL_CONSOLE=y, which builds
# arch/mips/ps2/romcons.c -- the console that writes via sbios(SB_PUTCHAR) and
# so reaches the PCSX2 log through TGE. Boot with "romcons" to register it.
[ -f .config ] || cp "$REPO/kernelconfig" .config

# ARCH must be on the COMMAND LINE, not exported: 2.4's top-level Makefile does
# "ARCH := $(shell uname -m ...)", and a makefile assignment beats the
# environment. Exported, you get arch/x86_64 and a confusing failure.
MAKEFLAGS_COMMON=(ARCH=mips CROSS_COMPILE="$TOOLS/bin/ee-")

msg "oldconfig"
yes '' | make "${MAKEFLAGS_COMMON[@]}" oldconfig >/dev/null

msg "dep"
make "${MAKEFLAGS_COMMON[@]}" dep

msg "vmlinux"
make -j"$(nproc)" "${MAKEFLAGS_COMMON[@]}" vmlinux

msg "built"
ls -la vmlinux System.map
readelf -h vmlinux | grep -E 'Machine|Entry point'
