#!/bin/sh
#
# Red/green test for a PCSX2 build, using our own kernel and the minish initrd.
#
# The point is to be able to say which patches are load-bearing rather than
# assuming. Boot one AppImage, watch the log for a fixed ladder of milestones,
# and report the furthest one reached. An unpatched PCSX2 stops at the third
# rung; a fully patched one reaches the last.
#
#   ./redgreen.sh /path/to/pcsx2-qt.AppImage [label]
#
# Exits 0 on GREEN (a shell came up), 1 otherwise, so it can drive a bisect.
#
# Requires KLTEST to hold kloader.elf, vmlinux_rx.gz and initrd_clean.gz, and
# writes its own config.txt there -- so it will overwrite whatever config is
# in place. That is deliberate: the test has to control the boot.

set -eu

APPIMAGE="${1:?usage: redgreen.sh <appimage> [label]}"
LABEL="${2:-$(basename "$APPIMAGE")}"
KLTEST="${KLTEST:-/mnt/c/Users/azama/PCSX2/kltest}"
TIMEOUT="${TIMEOUT:-180}"

LOG="$KLTEST/redgreen.txt"

[ -x "$APPIMAGE" ] || { echo "[X] not executable: $APPIMAGE" >&2; exit 2; }
for f in kloader.elf vmlinux_rx.gz initrd_clean.gz; do
    [ -f "$KLTEST/$f" ] || { echo "[X] missing $KLTEST/$f" >&2; exit 2; }
done

pkill -f 'pcsx2-qt.*AppImage' 2>/dev/null || true
sleep 1

cat > "$KLTEST/config.txt" <<'EOF'
KernelFileName=host:vmlinux_rx.gz
InitrdFileName=host:initrd_clean.gz
KernelParameter=root=/dev/ram0 rw ramdisk_size=16384 romcons console=tty0 console=romcons init=/minish
AutoBootTime=1
EOF

rm -f "$LOG"
cd "$KLTEST"
PCSX2_STRICT_USEG=1 nohup "$APPIMAGE" -logfile "$LOG" -fastboot \
    -- "$KLTEST/kloader.elf" >/dev/null 2>&1 &
EMU=$!

# The ladder. Each rung is a string that must appear in the log, in order, and
# names the stage that fails if it is the last one seen.
#
#   handover   loader started the kernel at all
#   kernel     the kernel got far enough to print its banner
#   userspace  init ran -- this is the rung an unpatched PCSX2 never reaches,
#              because demand paging never completes and it sits at 88k freed
#   shell      a shell is alive and reading its tty
ladder_probe='Linux version'
ladder_free='Freeing unused kernel memory'
ladder_root='VFS: Mounted root'
ladder_shell='minish: no libc'

elapsed=0
while [ "$elapsed" -lt "$TIMEOUT" ]; do
    if [ -f "$LOG" ] && grep -qa "$ladder_shell" "$LOG" 2>/dev/null; then
        break
    fi
    if [ -f "$LOG" ] && grep -qaE 'Kernel panic|Attempted to kill' "$LOG" 2>/dev/null; then
        break
    fi
    sleep 3
    elapsed=$((elapsed + 3))
done

reached="nothing"
[ -f "$LOG" ] || LOG=/dev/null
grep -qa "$ladder_probe" "$LOG" 2>/dev/null && reached="kernel booted"
grep -qa "$ladder_free" "$LOG" 2>/dev/null && reached="init memory freed"
grep -qa "$ladder_root" "$LOG" 2>/dev/null && reached="root mounted"
grep -qa "$ladder_shell" "$LOG" 2>/dev/null && reached="shell running"

panic=no
grep -qaE 'Kernel panic|Attempted to kill' "$LOG" 2>/dev/null && panic=yes

pkill -f 'pcsx2-qt.*AppImage' 2>/dev/null || true
wait "$EMU" 2>/dev/null || true

if [ "$reached" = "shell running" ]; then
    printf 'GREEN  %-32s reached: %s\n' "$LABEL" "$reached"
    exit 0
fi

printf 'RED    %-32s reached: %s%s\n' "$LABEL" "$reached" \
    "$([ "$panic" = yes ] && echo ' (panic)')"
exit 1
