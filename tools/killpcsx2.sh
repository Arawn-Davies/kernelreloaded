#!/usr/bin/env bash
# Kill every PCSX2 process, whatever form it is wearing.
#
# This needs three patterns, not one, which is why ad-hoc pkill kept failing:
#
#   pcsx2-qt-suspect.AppImage        the wrapper we launch
#   AppRun                           the AppImage shim it execs
#   /tmp/.mount_pcsx2-XXXXXX/...     the real binary, after the AppImage
#                                    mounts its squashfs and re-execs
#
# The last one is the one that survives: its argv does not contain the
# AppImage's filename, and its comm is truncated to 15 chars
# ("pcsx2-qt-suspec"), so neither `pkill -f <appimage>` nor `pkill -x pcsx2-qt`
# matches it. It is also reparented away from our `timeout`, so the timeout
# does not reap it either.
#
# Matching is done on /proc/<pid>/exe and the full argv, and self-matching is
# avoided by comparing PIDs rather than by grep tricks.

set -u

me=$$
found_any=0

for round in 1 2 3; do
    pids=""
    for d in /proc/[0-9]*; do
        pid=${d#/proc/}
        [ "$pid" = "$me" ] && continue
        exe=$(readlink "$d/exe" 2>/dev/null || true)
        args=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null || true)
        case "$exe$args" in
            *pcsx2*|*PCSX2*|*AppRun*)
                # Skip our own helper and any editor/grep that merely mentions it.
                case "$args" in
                    *killpcsx2*) continue ;;
                esac
                pids="$pids $pid"
                ;;
        esac
    done

    pids=$(echo "$pids" | tr -s ' ')
    [ -z "${pids// /}" ] && break

    found_any=1
    echo "  round $round: killing$pids"
    # shellcheck disable=SC2086
    kill -9 $pids 2>/dev/null || true
    sleep 1
done

left=0
for d in /proc/[0-9]*; do
    pid=${d#/proc/}
    [ "$pid" = "$me" ] && continue
    args=$(tr '\0' ' ' < "$d/cmdline" 2>/dev/null || true)
    case "$args" in
        *killpcsx2*) continue ;;
        *pcsx2*|*PCSX2*) left=$((left + 1)) ;;
    esac
done

if [ "$found_any" = "0" ]; then
    echo "  nothing was running"
fi
echo "  remaining: $left"
[ "$left" -eq 0 ]
