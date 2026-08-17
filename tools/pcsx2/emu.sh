#!/bin/sh
# Find, stop and launch emulator instances for kernelreloaded testing.
#
# Doing this by hand with pkill goes wrong in three specific ways, all of which
# have cost real debugging time on this project:
#
#   pkill -x pcsx2-qt        matches nothing. Linux truncates comm to 15
#                            characters, so the process is "pcsx2-qt-fixeso"
#                            and an exact-name match silently succeeds while
#                            killing nothing.
#
#   pkill -f AppImage        kills the shell that ran it. The pattern appears
#                            in the caller's own command line, so it matches
#                            itself. Even "pcsx2" in a script's *filename* is
#                            enough to do this.
#
#   one AppImage = two PIDs  the bundle re-execs through AppRun, so the wrapper
#                            and the emulator share a command line. Killing one
#                            leaves the other, which then looks like a fresh
#                            instance next time you look.
#
# So: match on the full command line read from /proc, never on comm; exclude
# this script and every one of its ancestors by PID rather than by pattern; and
# treat "how many are left" as the result rather than pkill's exit status.
#
# Supersedes tools/killpcsx2.sh, which solved the matching problem the same way
# and is folded in here; this adds the launch guard, which is what actually
# stops the duplicate-instance mistake rather than cleaning up after it.
#
# Usage:
#   emu.sh status                 list running instances
#   emu.sh kill                   stop them all, TERM then KILL, and verify
#   emu.sh run <appimage> [args]  refuse if one is already up, else launch
#
# The pattern defaults to the emulator names this project builds. Override with
# EMU_PATTERN for a different fork or a stock build.

set -eu

PATTERN="${EMU_PATTERN:-whiterhino|pcsx2}"

# This script lives in tools/pcsx2/, so its own command line matches the
# default pattern. Excluding ancestors is not enough: every command
# substitution below forks a *child* carrying the same command line, so
# emu_pids would report its own subshells. Exclude anything named like us.
SELF_TAG=$(basename "$0")

# Every PID from here up to init.
self_ancestry() {
	pid=$$
	while [ "$pid" -gt 1 ]; do
		echo "$pid"
		pid=$(awk '{print $4}' "/proc/$pid/stat" 2>/dev/null) || break
		[ -n "$pid" ] || break
	done
}

# Read a process's argv as a plain string. Empty if it is a kernel thread or
# exited while we were looking -- /proc races constantly and that is normal.
cmdline_of() {
	# Braces, not a plain redirect: 2>/dev/null covers the command's stderr,
	# not the shell's own complaint when /proc/<pid> disappears mid-scan.
	{ tr '\0' ' ' < "/proc/$1/cmdline"; } 2>/dev/null || true
}

# PIDs whose full command line matches, minus ourselves.
emu_pids() {
	mine=$(self_ancestry | tr '\n' ' ')
	for d in /proc/[0-9]*; do
		pid=${d#/proc/}
		case " $mine " in *" $pid "*) continue ;; esac
		# comm is truncated at 15 chars and the AppImage re-exec hides the
		# real name, so read argv instead.
		cmd=$(cmdline_of "$pid")
		case "$cmd" in *"$SELF_TAG"*) continue ;; esac

		# Match argv OR the binary path behind it.
		#
		# /proc/<pid>/exe is a symlink to the binary a process is running --
		# nothing to do with Windows, despite the name. It matters because the
		# AppImage mounts its squashfs under /tmp/.mount_* and re-execs, after
		# which the surviving process's argv no longer contains the AppImage's
		# filename, but its binary path still does. That is the process
		# `pkill -f <appimage>` fails to find.
		binpath=$(readlink "$d/exe" 2>/dev/null || true)
		[ -n "$cmd$binpath" ] || continue
		printf '%s %s' "$cmd" "$binpath" | grep -Eq "$PATTERN" && echo "$pid"
	done
	return 0
}

show() {
	found=0
	for pid in $(emu_pids); do
		found=$((found + 1))
		printf '%7s  %s\n' "$pid" "$(cmdline_of "$pid" | cut -c1-100)"
	done
	[ "$found" -eq 0 ] && echo "no emulator running"
	return 0
}

stop() {
	pids=$(emu_pids)
	if [ -z "$pids" ]; then
		echo "no emulator running"
		return 0
	fi

	# shellcheck disable=SC2086
	kill $pids 2>/dev/null || true

	# Give it a moment to exit cleanly; the AppImage unmounts /tmp/.mount_* on
	# the way out, and SIGKILL leaves that behind.
	n=0
	while [ "$n" -lt 10 ]; do
		[ -z "$(emu_pids)" ] && break
		sleep 1
		n=$((n + 1))
	done

	pids=$(emu_pids)
	if [ -n "$pids" ]; then
		echo "still up after SIGTERM, forcing: $pids"
		# shellcheck disable=SC2086
		kill -9 $pids 2>/dev/null || true
		sleep 1
	fi

	left=$(emu_pids | wc -l)
	if [ "$left" -eq 0 ]; then
		echo "stopped"
	else
		echo "FAILED, $left still running" >&2
		return 1
	fi
}

launch() {
	[ $# -ge 1 ] || { echo "usage: emu.sh run <appimage> [args...]" >&2; exit 2; }

	# The whole point. Two instances fighting over one config directory and one
	# log file produce interleaved output that reads like a single confused
	# boot, and that has wasted a sweep more than once.
	if [ -n "$(emu_pids)" ]; then
		echo "an emulator is already running -- stop it first:" >&2
		show >&2
		exit 1
	fi

	app=$1
	shift
	[ -x "$app" ] || { echo "not executable: $app" >&2; exit 2; }

	# Detached, output discarded: the useful log is the emulator's own
	# emulog.txt, which it writes regardless. stdout carries only Mesa noise.
	nohup "$app" "$@" >/dev/null 2>&1 &
	sleep 3
	echo "launched:"
	show
}

case "${1:-status}" in
	status) show ;;
	kill)   stop ;;
	run)    shift; launch "$@" ;;
	*)      echo "usage: emu.sh {status|kill|run <appimage> [args...]}" >&2; exit 2 ;;
esac
