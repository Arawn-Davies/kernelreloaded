#!/usr/bin/env bash
#
# Fetch a BlackRhino PS2 Live Linux DVD and extract DISC.BIN from it, optionally
# preparing that image to boot as a read-write root under PCSX2.
#
# DISC.BIN is the rootfs: a bare ext2 filesystem with NO partition table, which
# is why it mounts as /dev/hda and never hda1. It is not in this repo and should
# not be -- 1.6 GB of binary that every write would re-store in full, assembled
# from a whole Debian-derived distribution whose provenance and per-package
# licences nobody has established. Pointing at the upstream release costs
# nothing and sidesteps all of that.
#
# Usage:
#   ./get-blackrhino-disc.sh                     # PAL, small, no modchip
#   ./get-blackrhino-disc.sh --variant ntsc_small_no_modchip
#   ./get-blackrhino-disc.sh --prepare           # also make it PCSX2-bootable
#   ./get-blackrhino-disc.sh --prepare --no-x    # ...and skip the X desktop
#
# Output lands in dist/blackrhino-live/ (gitignored).

set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-$HERE/dist/blackrhino-live}"
CACHE="${CACHE:-$OUT/cache}"
VARIANT="pal_small_no_modchip"
PREPARE=no
NO_X=no
NO_NET_DAEMONS=no

SF_PROJECT="kernelloader"
SF_DIR="BlackRhino%20Linux%20Distribution/Live%20Linux%20DVD/PS2%20Live%20Linux%20DVD%20v3"

# Sizes read from SourceForge's own headers. Used instead of an md5 because no
# published checksum exists for these archives; a size check still catches a
# truncated or interrupted download, which is the realistic failure. If you
# complete a download, record its md5 here -- it is strictly better.
size_for() {
    case "$1" in
        pal_small_no_modchip)  echo 266223309 ;;
        pal_small_modchip)     echo 264562192 ;;
        ntsc_small_no_modchip) echo 266170719 ;;
        *)                     echo 0 ;;   # unknown: skip the check
    esac
}

say()  { printf '\n=== %s\n' "$*"; }
note() { printf '    %s\n' "$*"; }
fail() { printf '\n*** %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --variant)         VARIANT="$2"; shift 2 ;;
        --prepare)         PREPARE=yes; shift ;;
        --no-x)            NO_X=yes; shift ;;
        --no-net-daemons)  NO_NET_DAEMONS=yes; shift ;;
        -h|--help)         sed -n '2,20p' "$0"; exit 0 ;;
        *)                 fail "unknown option: $1" ;;
    esac
done

IMAGE="ps2linux_live_v3_${VARIANT}"

for t in curl 7z; do
    command -v "$t" >/dev/null || fail "$t not found (apt install p7zip-full curl)"
done
if [ "$PREPARE" = yes ]; then
    for t in e2fsck tune2fs debugfs; do
        command -v "$t" >/dev/null || fail "$t not found (apt install e2fsprogs)"
    done
fi

mkdir -p "$CACHE" "$OUT"
ARCHIVE="$CACHE/$IMAGE.7z"
WANT="$(size_for "$VARIANT")"

# ------------------------------------------------------------------ download --
say "fetching $IMAGE.7z"
have=0
[ -f "$ARCHIVE" ] && have=$(stat -c %s "$ARCHIVE")
if [ "$WANT" != 0 ] && [ "$have" = "$WANT" ]; then
    note "cached copy is the expected $WANT bytes, skipping download"
else
    # SourceForge serves an interstitial HTML page at /download and the real
    # mirror link has to be pulled out of it. It also rate-limits: a run of
    # requests earns a 403 for a while, which looks like the file is missing.
    # If that happens, wait rather than assuming the URL is wrong.
    page="https://sourceforge.net/projects/$SF_PROJECT/files/$SF_DIR/$IMAGE.7z/download"
    html="$(mktemp)"; trap 'rm -f "$html"' EXIT
    curl -fsSL -A 'Mozilla/5.0' -o "$html" "$page" \
        || fail "could not reach SourceForge (403 usually means rate-limited; try later)"
    url="$(grep -oE "https://downloads\.sourceforge\.net/project/$SF_PROJECT[^\"' ]*" "$html" \
           | head -1 | sed 's/&amp;/\&/g')"
    [ -n "$url" ] || fail "no mirror URL in SourceForge's download page"

    note "downloading (resumable; just re-run if it stops)"
    curl -fL -A 'Mozilla/5.0' -C - --progress-bar -o "$ARCHIVE" "$url" || fail "download failed"

    got=$(stat -c %s "$ARCHIVE")
    if [ "$WANT" != 0 ] && [ "$got" != "$WANT" ]; then
        fail "size mismatch: got $got, expected $WANT -- delete $ARCHIVE and retry"
    fi
    note "$got bytes"
fi

# ------------------------------------------------------------------- extract --
say "extracting the ISO"
ISO="$(7z l -slt "$ARCHIVE" | sed -n 's/^Path = \(.*\.iso\)$/\1/p' | head -1)"
[ -n "$ISO" ] || fail "no .iso inside $ARCHIVE"
note "$ISO"
[ -f "$OUT/$ISO" ] || 7z e -y -o"$OUT" "$ARCHIVE" "$ISO" >/dev/null

say "extracting DISC.BIN"
# The disc is a hybrid ISO9660/UDF; DISC.BIN sits at the top level beside
# BOOT/, KLOADER.ELF and SYSTEM.CNF.
[ -f "$OUT/DISC.BIN" ] || 7z e -y -o"$OUT" "$OUT/$ISO" DISC.BIN >/dev/null
[ -f "$OUT/DISC.BIN" ] || fail "DISC.BIN not found in $ISO"
note "$(stat -c %s "$OUT/DISC.BIN") bytes"

# Cheap proof it is what we think, without mounting: the ext2 magic 0xEF53 sits
# at offset 1080 in the superblock.
magic=$(od -An -tx2 -j1080 -N2 "$OUT/DISC.BIN" | tr -d ' ')
[ "$magic" = "ef53" ] || fail "DISC.BIN is not ext2 (magic $magic, expected ef53)"
note "ext2 superblock magic ok"

if [ "$PREPARE" != yes ]; then
    say "done"
    note "DISC.BIN: $OUT/DISC.BIN"
    note "Pass --prepare to make it bootable as a read-write root under PCSX2."
    exit 0
fi

# ------------------------------------------------------------------- prepare --
# Everything below is what a stock image needs before PS2 Linux will boot from
# it read-write under PCSX2. Each step is here because it was found the hard way.
say "preparing the image for PCSX2"

# The image was built in 2012 on a modern host, so it carries ext2 features the
# guest's own 2002 e2fsck cannot parse: it refuses with "unsupported features -
# get a newer version of e2fsck", and Debian's checkroot.sh then fails the boot.
# It never mattered upstream because the Live DVD runs read-only and never fscks
# itself. ext_attr is left alone -- tune2fs cannot clear it, and e2fsprogs has
# understood it since 2001.
note "stripping dir_index and resize_inode"
tune2fs -O ^dir_index,^resize_inode "$OUT/DISC.BIN" >/dev/null
e2fsck -fyD "$OUT/DISC.BIN" >/dev/null 2>&1 || true

# A forced periodic fsck of a 1.6 GB filesystem on an emulated PS2 is punishing,
# and there is no reason for one here.
note "disabling periodic fsck"
tune2fs -c 0 -i 0 "$OUT/DISC.BIN" >/dev/null

# The image has no /etc/fstab at all -- the Live DVD generates its mounts. Its
# absence makes checkroot.sh and mountall.sh unhappy when it is a real root.
note "writing /etc/fstab"
tmp="$(mktemp)"
cat > "$tmp" <<'FSTAB'
# Minimal fstab for booting DISC.BIN directly as root under PCSX2.
# The image is a bare ext2 filesystem with no partition table, so the root
# device is /dev/hda itself, never hda1.
/dev/hda        /               ext2    defaults,errors=remount-ro      0       1
proc            /proc           proc    defaults                        0       0
none            /dev/pts        devpts  gid=5,mode=620                  0       0
FSTAB
debugfs -w "$OUT/DISC.BIN" >/dev/null 2>&1 <<EOF
rm /etc/fstab
write $tmp fstab
ln /fstab /etc/fstab
unlink /fstab
EOF
rm -f "$tmp"

if [ "$NO_NET_DAEMONS" = yes ]; then
    # These block on a network that does not exist yet: PCSX2's SMAP EEPROM
    # emulation fails the driver's checksum test, so eth0 never registers.
    # ntpdate in particular sits on long timeouts before anything later in
    # runlevel 2 gets to run.
    note "disabling network-dependent daemons"
    debugfs -w "$OUT/DISC.BIN" >/dev/null 2>&1 <<'EOF'
rm /etc/rc2.d/S22ntpdate
rm /etc/rc2.d/S23ntp
rm /etc/rc2.d/S20exim
rm /etc/rc2.d/S91apache
rm /etc/rc2.d/S19nfs-common
rm /etc/rc2.d/S20nfs-kernel-server
EOF
fi

if [ "$NO_X" = yes ]; then
    note "disabling the X display manager"
    debugfs -w "$OUT/DISC.BIN" >/dev/null 2>&1 <<'EOF'
rm /etc/rc2.d/S99xdm
EOF
fi

note "final check"
e2fsck -fp "$OUT/DISC.BIN" || fail "e2fsck reported a problem"

say "done"
note "DISC.BIN: $OUT/DISC.BIN"
cat <<'NEXT'

    Point PCSX2's DEV9 HDD at it -- and note the setting lives in [DEV9/Hdd] in
    PCSX2.ini. A flat [DEV9] section is an older layout that current builds
    ignore silently, which is a long afternoon if you do not know it.

    Then boot kloader.elf with:

        KernelFileName=host:vmlinux_net.gz
        KernelParameter=root=/dev/hda rw romcons console=romcons console=tty0
        AutoBootTime=1

    Writing to it needs tools/pcsx2/ata-pio-write.patch: PCSX2 implements no ATA
    PIO write command at all, so without it the mount fails with
    "no DRQ after issuing WRITE".
NEXT
