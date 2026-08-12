#!/usr/bin/env bash
#
# Put the FULL BlackRhino distribution (PS2 Live Linux DVD v3) on an NFS export.
#
# Run this ON the NFS server, as a sudo-capable user:
#     scp tools/install-blackrhino-full.sh arawn@10.0.0.120:~/
#     ssh -t arawn@10.0.0.120 'bash ~/install-blackrhino-full.sh'
#
# (ssh -t is required — sudo needs a TTY for the password prompt.)
#
# WHY ON THE SERVER
# The archive is 2.6 GB and expands to a ~4.4 GB ISO before you get at the
# rootfs. Doing this anywhere else means moving several GB over the network
# afterwards, to land in exactly the directory this script writes to directly.
#
# WHAT IT DOES BEYOND EXTRACTING
#   - makes the root filesystem genuinely writable over NFS
#   - autostarts X11 into Window Maker
# Both are needed because the DVD image is built to run read-only from a disc,
# not as an NFS root.
#
# Idempotent: re-running re-verifies without re-downloading or re-extracting,
# unless you pass -reextract.

set -euo pipefail

# ---------------------------------------------------------------------------
# Configuration — override on the command line, e.g.
#     NFSROOT=/srv/ps2root PS2_IP=10.0.0.121 bash install-blackrhino-full.sh
# ---------------------------------------------------------------------------
NFSROOT="${NFSROOT:-/srv/ps2root}"       # where the rootfs is extracted
PS2_IP="${PS2_IP:-10.0.0.121}"           # the console. MUST match the kernel ip=
SERVER_IP="${SERVER_IP:-10.0.0.120}"     # this machine, as the PS2 addresses it
GATEWAY_IP="${GATEWAY_IP:-10.0.0.1}"     # your router
NETMASK="${NETMASK:-255.255.0.0}"        # 255.255.255.0 for a /24
CACHE="${CACHE:-$HOME/blackrhino-dl}"    # keeps the 2.6 GB archive between runs
WM="${WM:-wmaker}"                       # window manager to start

# PAL, full ("large") image, for a console WITHOUT a modchip. If you have a
# modchip or an NTSC console, swap the name and the md5 from the release's
# readme_ps2livedvd_v3.txt.
IMAGE="ps2linux_live_v3_pal_large_no_modchip"
IMAGE_MD5="adc6a1f51c545e5c0c278f89495ee2be"
SF_DIR="BlackRhino%20Linux%20Distribution/Live%20Linux%20DVD/PS2%20Live%20Linux%20DVD%20v3"

EXPORTS_LINE="${NFSROOT} ${PS2_IP}(rw,no_root_squash,async,no_subtree_check)"

say()  { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '    [!] %s\n' "$*"; }
fail() { printf '\n[X] %s\n' "$*" >&2; exit 1; }

[ "$(id -u)" -eq 0 ] && fail "Run as a sudo-capable user, not root — it calls sudo itself."

say "Checking prerequisites"
need=""
command -v 7z      >/dev/null || need="$need p7zip-full"
command -v curl    >/dev/null || need="$need curl"
command -v rsync   >/dev/null || need="$need rsync"
dpkg -s nfs-kernel-server >/dev/null 2>&1 || need="$need nfs-kernel-server"
if [ -n "$need" ]; then
    echo "    installing:$need"
    sudo apt-get update -qq
    sudo apt-get install -y $need
else
    echo "    all present"
fi

# ---------------------------------------------------------------------------
say "Fetching ${IMAGE}.7z (2.6 GB)"
# ---------------------------------------------------------------------------
mkdir -p "$CACHE"
ARCHIVE="${CACHE}/${IMAGE}.7z"

if [ -f "$ARCHIVE" ] && [ "$(md5sum "$ARCHIVE" | cut -d' ' -f1)" = "$IMAGE_MD5" ]; then
    echo "    cached copy verified, skipping download"
else
    # SourceForge serves an interstitial HTML page at the /download URL; the
    # real mirror link has to be pulled out of it.
    page="https://sourceforge.net/projects/kernelloader/files/${SF_DIR}/${IMAGE}.7z/download"
    html="$(mktemp)"; trap 'rm -f "$html"' EXIT
    curl -fsSL -A 'Mozilla/5.0' -o "$html" "$page" || fail "could not reach SourceForge"
    url="$(grep -oE 'https://downloads\.sourceforge\.net/project/kernelloader[^"'"'"' ]*' "$html" \
           | head -1 | sed 's/&amp;/\&/g')"
    [ -n "$url" ] || fail "could not find the mirror URL in SourceForge's download page"

    echo "    downloading (resumable; re-run if interrupted)"
    curl -fL -A 'Mozilla/5.0' -C - --progress-bar -o "$ARCHIVE" "$url" \
        || fail "download failed"

    echo "    verifying md5"
    got="$(md5sum "$ARCHIVE" | cut -d' ' -f1)"
    [ "$got" = "$IMAGE_MD5" ] || fail "md5 mismatch: got $got, expected $IMAGE_MD5"
    echo "    ok"
fi

# ---------------------------------------------------------------------------
say "Extracting the rootfs into ${NFSROOT}"
# ---------------------------------------------------------------------------
if [ -d "${NFSROOT}/etc" ] && [ "${1:-}" != "-reextract" ]; then
    echo "    already extracted (pass -reextract to redo)"
else
    WORK="$(mktemp -d)"
    # The ISO is ~4.4 GB; clean it up even if we abort.
    trap 'sudo umount "${WORK}/mnt" 2>/dev/null || true; sudo rm -rf "$WORK"' EXIT

    echo "    unpacking the ISO out of the archive"
    7z x -o"$WORK" "$ARCHIVE" >/dev/null || fail "7z extraction failed"
    ISO="$(find "$WORK" -maxdepth 2 -iname '*.iso' | head -1)"
    [ -n "$ISO" ] || fail "no .iso inside the archive — layout unexpected"
    echo "    found $(basename "$ISO") ($(du -h "$ISO" | cut -f1))"

    mkdir -p "${WORK}/mnt"
    sudo mount -o loop,ro "$ISO" "${WORK}/mnt" || fail "could not mount the ISO"

    # The live DVD carries its root filesystem one of two ways depending on the
    # build: as a plain directory tree, or as a compressed image alongside the
    # kernel. Detect rather than assume, and say what was found either way.
    SRC=""
    if [ -d "${WORK}/mnt/etc" ] && [ -d "${WORK}/mnt/bin" ]; then
        SRC="${WORK}/mnt"
        echo "    rootfs is a plain directory tree on the ISO"
    else
        inner="$(sudo find "${WORK}/mnt" -maxdepth 3 -type d -name etc \
                 -exec test -d '{}/../bin' \; -print 2>/dev/null | head -1)"
        if [ -n "$inner" ]; then
            SRC="$(dirname "$inner")"
            echo "    rootfs found at ${SRC#${WORK}/mnt}"
        fi
    fi

    if [ -z "$SRC" ]; then
        echo
        echo "    Could not identify the root filesystem automatically."
        echo "    Top level of the ISO:"
        ls -la "${WORK}/mnt" | sed 's/^/      /'
        fail "inspect the listing above and set SRC by hand, or report it back"
    fi

    sudo mkdir -p "$NFSROOT"
    echo "    copying (preserving ownership, permissions and symlinks)"
    sudo rsync -aHAX --numeric-ids --info=progress2 "${SRC}/" "${NFSROOT}/"

    sudo umount "${WORK}/mnt"
    sudo rm -rf "$WORK"
    trap - EXIT
    [ -d "${NFSROOT}/etc" ] || fail "no /etc in ${NFSROOT} — copy did not land"
fi

# ---------------------------------------------------------------------------
say "Making the root filesystem writable"
# ---------------------------------------------------------------------------
# The DVD image expects to run read-only from a disc, so its fstab and its
# scratch directories are set up for that. Over NFS the export is rw with
# no_root_squash, so the filesystem itself can be written -- what has to change
# is what the system believes.
#
# Note there is no tmpfs option here: the BlackRhino 2.4.17 kernel is built
# with CONFIG_TMPFS unset, so /tmp and /var CANNOT be memory filesystems. They
# have to be real, writable directories on the export.
sudo tee "${NFSROOT}/etc/fstab" >/dev/null <<'EOF'
# NFS root. The kernel mounts / itself from the nfsroot= parameter before this
# file is read; the entry is here so remounts and `mount -a` agree with it.
#
# nolock  -- lockd/statd round-trips are a known hang point on a 2.4.17 client
# hard    -- retry indefinitely rather than returning errors to userspace
# intr    -- but still allow signals to break out of a wedged mount
/dev/nfs        /               nfs     rw,nolock,hard,intr     0 0

proc            /proc           proc    defaults                0 0
devpts          /dev/pts        devpts  gid=5,mode=620          0 0

# No tmpfs entries for /tmp or /var: this kernel has CONFIG_TMPFS unset, so
# both live on the NFS export and are made writable below.
EOF
echo "    wrote /etc/fstab"

# World-writable with the sticky bit, as /tmp must be.
for d in tmp var/tmp; do
    sudo mkdir -p "${NFSROOT}/${d}"
    sudo chmod 1777 "${NFSROOT}/${d}"
done
# Runtime state that must exist and be writable at boot.
for d in var/run var/lock var/log var/spool; do
    sudo mkdir -p "${NFSROOT}/${d}"
    sudo chmod 0755 "${NFSROOT}/${d}"
done
# Stale pids and locks from the image build will confuse init on first boot.
sudo rm -f "${NFSROOT}/var/run/"*.pid "${NFSROOT}/var/lock/"* 2>/dev/null || true
echo "    /tmp and /var/tmp are 1777; /var/{run,lock,log,spool} exist and are writable"

# A read-only live system often ships these as symlinks into a ramdisk that
# will not exist here. Replace any that dangle with real directories.
for d in tmp var/run var/lock; do
    p="${NFSROOT}/${d}"
    if [ -L "$p" ]; then
        warn "${d} was a symlink ($(readlink "$p")) — replacing with a real directory"
        sudo rm -f "$p"
        sudo mkdir -p "$p"
        [ "$d" = "tmp" ] && sudo chmod 1777 "$p"
    fi
done

# ---------------------------------------------------------------------------
say "Autostarting X11 into ${WM}"
# ---------------------------------------------------------------------------
if [ ! -x "${NFSROOT}/usr/bin/${WM}" ] && [ ! -x "${NFSROOT}/usr/X11R6/bin/${WM}" ]; then
    warn "${WM} not found in the image — skipping autostart setup"
    warn "look for what is available: ls ${NFSROOT}/usr/X11R6/bin"
else
    # startx reads ~/.xinitrc. exec so the WM becomes the session leader and
    # X exits when it does, rather than leaving an orphaned server.
    sudo tee "${NFSROOT}/root/.xinitrc" >/dev/null <<EOF
#!/bin/sh
# Written by install-blackrhino-full.sh
exec ${WM}
EOF
    sudo chmod 0755 "${NFSROOT}/root/.xinitrc"

    # Start X once at boot from inittab rather than switching to a full
    # graphical runlevel: this image is sysvinit-era and has no display
    # manager configured, and "once" avoids a respawn loop if X fails.
    INITTAB="${NFSROOT}/etc/inittab"
    if [ -f "$INITTAB" ]; then
        if grep -q '^x1:' "$INITTAB"; then
            echo "    inittab already has an x1: entry, leaving it alone"
        else
            echo 'x1:2345:once:/bin/su - root -c "/usr/X11R6/bin/startx -- -br" >/dev/null 2>&1' \
                | sudo tee -a "$INITTAB" >/dev/null
            echo "    added an x1: entry to /etc/inittab"
        fi
    else
        warn "no /etc/inittab — cannot set up autostart; run startx by hand"
    fi
    echo "    /root/.xinitrc execs ${WM}"
fi

# ---------------------------------------------------------------------------
say "Configuring the export"
# ---------------------------------------------------------------------------
# Scoped to the console's address alone. no_root_squash means root on the PS2 is
# root on this directory — REQUIRED for a root filesystem, and exactly why the
# export must never be widened to a subnet.
if grep -qF "$NFSROOT" /etc/exports 2>/dev/null; then
    echo "    entry already present:"
    grep -F "$NFSROOT" /etc/exports | sed 's/^/      /'
else
    echo "$EXPORTS_LINE" | sudo tee -a /etc/exports >/dev/null
    echo "    added: $EXPORTS_LINE"
fi

# The 2.4.17 client speaks NFSv3 only; modern servers default to v4-only.
if [ -f /etc/nfs.conf ] && grep -qE '^\s*vers3\s*=\s*n' /etc/nfs.conf; then
    sudo sed -i 's/^\s*vers3\s*=\s*n/vers3=y/' /etc/nfs.conf
    echo "    flipped vers3=n -> vers3=y in /etc/nfs.conf"
fi
sudo systemctl enable --now rpcbind 2>/dev/null || true
sudo exportfs -ra
sudo systemctl enable --now nfs-kernel-server

if command -v ufw >/dev/null && sudo ufw status 2>/dev/null | grep -q '^Status: active'; then
    for p in 111 2049; do
        sudo ufw allow from "$PS2_IP" to any port "$p" proto tcp comment 'PS2 NFS root' >/dev/null
    done
    echo "    opened 111/tcp + 2049/tcp from ${PS2_IP}"
fi

say "Verifying"
sudo exportfs -v | sed 's/^/    /'
du -sh "$NFSROOT" 2>/dev/null | sed 's/^/    rootfs size: /'

say "Done"
cat <<EOF

  NFS root : ${NFSROOT}
  Exported : ${PS2_IP} only, rw + no_root_squash
  X11      : startx -> ${WM} at boot, via /etc/inittab and /root/.xinitrc

  Build the matching USB stick on your workstation:

      PS2_IP=${PS2_IP} NFS_IP=${SERVER_IP} GATEWAY=${GATEWAY_IP} \\
      NETMASK=${NETMASK} NFS_EXPORT=${NFSROOT} ./tools/mkusb.sh --nfs

  Then copy the contents of dist/usb/ to the stick root and launch
  kloader.elf from wLaunchELF.

  Watch mounts arrive while the PS2 boots:
      sudo journalctl -u nfs-kernel-server -f

  If X does not come up, log in on the console and run startx by hand -- the
  error will be on tty1 or in ~/.xsession-errors. The PS2 GS driver may need
  a specific resolution set in /etc/X11/XF86Config-4.
EOF
