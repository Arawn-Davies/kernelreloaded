#!/usr/bin/env bash
#
# Build kernelloader inside the ps2dev Docker toolchain (see Dockerfile).
#
# OUTPUT — everything lands in ./bin/ (repo-local, gitignored), so it is the
# same place whether you build on WSL2, pure Linux, or Windows+Cygwin:
#
#   bin/kloader.elf         >>> THE DELIVERABLE <<< copy this to mc0:/mass0:
#   bin/debug/loader.elf    the same program, uncompressed, with symbols
#   bin/debug/kernel.elf    EE kernel stub, already embedded in the above
#
# kloader.elf and loader.elf are not two programs. loader/Makefile links
# loader.elf (PROGRAM=loader), crc32gen patches the .text/.rodata checksums
# into its .crc32 section, and ps2-packer compresses the result into a
# self-extracting kloader.elf (PACKEDFILE) -- 6.5 MB becomes ~985 KB. Only
# kloader.elf boots on a console; its entry point is the packer's decompression
# stub. Keep loader.elf for nm/addr2line when something faults, since the
# packed image has no symbols.
#
# Usage:
#   ./build.sh                 # build kernelloader -> bin/kloader.elf
#   ./build.sh clean           # make clean in the source tree
#   ./build.sh shell           # interactive shell, cwd = /work
#   ./build.sh <target> [...]  # any other make target, passed through
#
# Nothing host-specific is committed. If you want personal paths (e.g. copying
# the ELF onto a mounted memory card), make a gitignored wrapper that exports
# what it needs and execs this script.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${KERNELRELOADED_IMAGE:-kernelreloaded:local}"
# The kernelloader source IS this repo -- Makefile, kernel/, TGE/, loader/ and
# the rest sit at the top level, so `make` here just works.
#
# It used to be a git submodule with our changes carried as patches/*.patch,
# applied into the checkout at build time, and then briefly a nested loader/
# wrapper. Both are gone: the fixes are far too extensive to keep rebasing, and
# nothing here can be pushed upstream anyway (kernelloader has been dead since
# 2017), so the pointer only ever referenced commits that existed on one
# machine. patches/ now holds only upstream's Linux kernel patch set.
BIN="${HERE}/bin"

cd "$HERE"

if [ ! -f "Makefile" ]; then
    echo "[X] Makefile is missing — the source tree is incomplete." >&2
    echo "    Expected the kernelloader sources at ${HERE}/." >&2
    exit 1
fi

# Custom UI artwork lives in assets/ and is copied into the source tree at
# build time rather than committed there twice. png2rgb converts these during
# the build; the generated .rgb and .h files are gitignored.
#
# Only the top level of assets/ is copied, and every file there must be a
# texture listed in loader/Makefile's RGB_FILES. Source artwork that is not
# shipped -- the design mockup, full-resolution originals -- belongs in
# assets/src/, which this glob deliberately does not descend into. Before that
# split, mockup.png alone was copying 1.1 MB into the source tree on every
# build for nothing.
copy_assets() {
    local a
    for a in "${HERE}"/assets/*.png; do
        [ -e "$a" ] || continue
        if ! cmp -s "$a" "loader/$(basename "$a")"; then
            cp -f "$a" "loader/"
            echo "    [+] $(basename "$a")"
        else
            echo "    [=] $(basename "$a") unchanged"
        fi
    done
}

echo "==> Assets"
copy_assets

# Build the toolchain image once. Docker caches it unless the Dockerfile
# changes, so this is a no-op on subsequent runs.
if ! docker image inspect "$IMAGE" >/dev/null 2>&1 || [ "${REBUILD_IMAGE:-0}" = "1" ]; then
    echo "==> Building toolchain image ${IMAGE}"
    docker build -t "$IMAGE" "$HERE"
fi

mkdir -p "$BIN"

common=(
    --rm
    -v "${HERE}:/work"
    -w "/work"
)

case "${1:-}" in
    shell)
        exec docker run -it "${common[@]}" "$IMAGE" /bin/bash
        ;;
    clean)
        docker run "${common[@]}" "$IMAGE" make clean || true
        rm -f "${BIN}"/*.elf "${BIN}"/debug/*.elf 2>/dev/null || true
        rmdir "${BIN}/debug" 2>/dev/null || true
        echo "[✓] cleaned"
        exit 0
        ;;
esac

echo "==> Building kernelloader"
docker run "${common[@]}" -e ARGS="$*" "$IMAGE" bash -c '
    set -e
    make $ARGS
    # Only the packed image is a deliverable, so it goes in bin/ alone; the
    # uncompressed build and the embedded kernel stub go to bin/debug/ so there
    # is never a question about which file to copy onto a card.
    mkdir -p /work/bin/debug
    [ -f loader/kloader.elf ] && cp -f loader/kloader.elf /work/bin/
    [ -f loader/loader.elf  ] && cp -f loader/loader.elf  /work/bin/debug/
    [ -f kernel/kernel.elf  ] && cp -f kernel/kernel.elf  /work/bin/debug/
    true
'

echo
if [ -f "${BIN}/kloader.elf" ]; then
    echo "[✓] bin/kloader.elf — copy this one to mc0:/mc1:/mass0:"
    ls -l --block-size=K "${BIN}/kloader.elf" | sed 's/^/    /'
    if ls "${BIN}"/debug/*.elf >/dev/null 2>&1; then
        echo
        echo "    bin/debug/ — build intermediates, not for deployment:"
        echo "      loader.elf   the same program before ps2-packer; has symbols,"
        echo "                   use it for nm/addr2line when something faults"
        echo "      kernel.elf   EE kernel stub, already embedded in loader.elf"
    fi
else
    echo "[!] build finished but bin/kloader.elf is missing — check the log above"
fi
