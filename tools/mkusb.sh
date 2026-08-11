#!/usr/bin/env bash
#
# Assemble a self-contained USB payload for launching kernelreloaded from
# wLaunchELF / uLaunchELF.
#
# WHY THIS WORKS WITHOUT A MEMORY CARD
# kernelloader's startup search already includes "mass0:CONFIG.TXT" -- step 3
# of the fallback chain in loadLoaderModules(). So a stick carrying CONFIG.TXT
# at its ROOT is picked up automatically, with no memory card present and no
# menu action. Note the name really is uppercase and really is at the root;
# mass0:kloader/config.txt is NOT searched.
#
# Layout produced (copy the CONTENTS of dist/usb/ to the stick root):
#
#   kloader.elf     the loader -- launch this from wLaunchELF
#   CONFIG.TXT      auto-loaded at startup
#   vmlinux.gz      the kernel
#   initrd.gz       the initial ramdisk
#
# Usage:
#   ./tools/mkusb.sh [kernel] [initrd]
#
# Defaults point at the artifacts this project has been tested with. Override
# either by passing a path.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${HERE}/dist/usb"

KERNEL="${1:-${HOME}/ps2bdmshite/brl-nfs/artifacts/vmlinux.gz}"
INITRD="${2:-${HERE}/dist/initrd.gz}"

ELF="${HERE}/bin/kloader.elf"

die() { echo "[X] $*" >&2; exit 1; }

[ -f "$ELF" ]    || die "no ${ELF} -- run ./build.sh first"
[ -f "$KERNEL" ] || die "kernel not found: ${KERNEL}"
[ -f "$INITRD" ] || die "initrd not found: ${INITRD}
    Fetch it from the kernelloader SourceForge project:
      Initial RAM Disc / Initrd for general testing and installing BlackRhino
      -> initrd.gz-ps2-20071224"

mkdir -p "$OUT"
install -m 644 "$ELF"    "${OUT}/kloader.elf"
install -m 644 "$KERNEL" "${OUT}/vmlinux.gz"
install -m 644 "$INITRD" "${OUT}/initrd.gz"

# CONFIG.TXT is generated rather than kept as a static file so the device
# prefixes can never drift from the layout above.
#
# KernelParameter must NOT carry video settings: getKernelParameter() appends
# the separate VideoParameter item to it, and kernelloader fills that in for
# the selected mode. Duplicating crtmode=/video= here would produce two
# conflicting sets on one command line.
#
# rd_start= and rd_size= are appended automatically by the loader once the
# initrd is in memory (loader.c), so they must not be listed either.
cat > "${OUT}/CONFIG.TXT" <<'EOF'
# kernelreloaded -- self-contained USB configuration.
#
# Auto-loaded from mass0:CONFIG.TXT at startup. Uppercase, at the stick root.
#
# Device prefixes: mc0: mc1: mass0: cdfs: host:   (no slash after the colon)

KernelFileName=mass0:vmlinux.gz
InitrdFileName=mass0:initrd.gz

# root=/dev/ram0    boot the initrd itself as root, rather than an NFS export
# ramdisk_size      in KiB. The initrd is a 12 MB ext2 image, so 16384 (16 MB)
#                   gives it room; the kernel's built-in default is only 4 MB
#                   (CONFIG_BLK_DEV_RAM_SIZE=4096) which is too small.
#
# No crtmode=/video= here -- VideoParameter is appended automatically.
KernelParameter=root=/dev/ram0 rw ramdisk_size=16384

# Seconds before booting the current config unattended. 0 disables.
AutoBootTime=5
EOF

echo "[/] dist/usb ready -- copy its CONTENTS to the stick root:"
ls -l "$OUT" | sed 's/^/    /'
echo
echo "    On the console: wLaunchELF -> mass0: -> kloader.elf"
