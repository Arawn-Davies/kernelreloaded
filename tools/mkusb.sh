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
# Nothing else is needed. The SBIOS, the kernel stub, every IOP module and the
# four intrelay variants are all embedded in kloader.elf as ROM blobs, so there
# is no TGE/ directory to copy: loader.c asks for "host:TGE/intrelay-direct.irx"
# but the ROM is searched first, by the path with the device prefix stripped,
# and that is where it is found. That matters on a console booted from USB,
# where host: does not exist at all.
#
# Usage:
#   ./tools/mkusb.sh [kernel] [initrd]        ramdisk root (default)
#   ./tools/mkusb.sh --nfs [kernel]           NFS root, no initrd
#
# Defaults point at the artifacts this project has been tested with. Override
# by passing a path.
#
# NFS mode edits nothing on the console: the same stick boots, but CONFIG.TXT
# points root at an NFS export instead of the ramdisk, so no initrd is copied.
# Set the addresses with the environment variables below, or edit CONFIG.TXT on
# the stick afterwards -- it is plain text.
#
#   PS2_IP      this console          (default 10.0.0.121)
#   NFS_IP      the NFS server        (default 10.0.0.120)
#   GATEWAY     your router           (default 10.0.0.1)
#   NETMASK                           (default 255.255.0.0)
#   NFS_EXPORT  path in /etc/exports  (default /srv/ps2root)

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${HERE}/dist/usb"

MODE=ramdisk
if [ "${1:-}" = "--nfs" ]; then
    MODE=nfs
    shift
fi

KERNEL="${1:-${HOME}/ps2bdmshite/brl-nfs/artifacts/vmlinux.gz}"
INITRD="${2:-${HERE}/dist/initrd.gz}"

PS2_IP="${PS2_IP:-10.0.0.121}"
NFS_IP="${NFS_IP:-10.0.0.120}"
GATEWAY="${GATEWAY:-10.0.0.1}"
NETMASK="${NETMASK:-255.255.0.0}"
NFS_EXPORT="${NFS_EXPORT:-/srv/ps2root}"

ELF="${HERE}/bin/kloader.elf"

die() { echo "[X] $*" >&2; exit 1; }

[ -f "$ELF" ]    || die "no ${ELF} -- run ./build.sh first"
[ -f "$KERNEL" ] || die "kernel not found: ${KERNEL}"
[ "$MODE" = nfs ] || [ -f "$INITRD" ] || die "initrd not found: ${INITRD}
    Fetch it from the kernelloader SourceForge project:
      Initial RAM Disc / Initrd for general testing and installing BlackRhino
      -> initrd.gz-ps2-20071224"

rm -rf "$OUT"
mkdir -p "$OUT"
install -m 644 "$ELF"    "${OUT}/kloader.elf"
install -m 644 "$KERNEL" "${OUT}/vmlinux.gz"
[ "$MODE" = nfs ] || install -m 644 "$INITRD" "${OUT}/initrd.gz"

# CONFIG.TXT is generated rather than kept as a static file so the device
# prefixes can never drift from the layout above.
#
# KernelParameter must NOT carry video settings: getKernelParameter() appends
# the separate VideoParameter item to it, and kernelloader fills that in for
# the selected mode. Duplicating crtmode=/video= here would produce two
# conflicting sets on one command line.

if [ "$MODE" = nfs ]; then
cat > "${OUT}/CONFIG.TXT" <<EOF
# kernelreloaded -- USB boot, NFS root.
#
# Auto-loaded from mass0:CONFIG.TXT at startup. Uppercase, at the stick root.
# The kernel comes off the stick; everything above / lives on the NFS server,
# so there is no initrd here.

KernelFileName=mass0:vmlinux.gz

# ip=<client>:<server>:<gateway>:<netmask>:<hostname>:<device>:<autoconf>
#
# The client address MUST be static. This kernel has CONFIG_IP_PNP but DHCP,
# BOOTP and RARP are all compiled out, so a DHCP reservation can never assign
# it -- the console never asks.
#
# v3   MUST be spelled "v3", not "nfsvers=3". The in-kernel nfsroot parser only
#      understands v2/v3/udp/tcp/rsize/wsize; "nfsvers=" is a userspace
#      mount.nfs option and is silently ignored here, leaving the client on its
#      2.4 default of NFSv2 -- which modern servers no longer offer. The symptom
#      is "server not responding, still trying" over an ESTABLISHED connection.
# tcp  without it the MOUNT RPC falls back to UDP.
#
# If root mounts but the boot then stalls, add nolock:
#     nfsroot=${NFS_IP}:${NFS_EXPORT},v3,tcp,nolock
KernelParameter=ip=${PS2_IP}:${NFS_IP}:${GATEWAY}:${NETMASK}::eth0 root=/dev/nfs rw nfsroot=${NFS_IP}:${NFS_EXPORT},v3,tcp

# Seconds before booting the current config unattended. 0 disables.
AutoBootTime=5
EOF
else
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
# rd_start= and rd_size= are appended automatically by the loader once the
# initrd is in memory, so they must not be listed here.
KernelParameter=root=/dev/ram0 rw ramdisk_size=16384

# Seconds before booting the current config unattended. 0 disables.
AutoBootTime=5
EOF
fi

echo "[/] dist/usb ready (${MODE} root) -- copy its CONTENTS to the stick root:"
ls -l "$OUT" | sed 's/^/    /'
echo
echo "    On the console: wLaunchELF -> mass0: -> kloader.elf"
