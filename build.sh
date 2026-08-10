#!/usr/bin/env bash
#
# Build kernelloader inside the ps2dev Docker toolchain (see Dockerfile).
#
# OUTPUT — everything lands in ./bin/ (repo-local, gitignored), so it is the
# same place whether you build on WSL2, pure Linux, or Windows+Cygwin:
#   bin/kloader.elf     the loader, ready for mc0:/mass0:
#
# Usage:
#   ./build.sh                 # build kernelloader -> bin/kloader.elf
#   ./build.sh clean           # make clean in the submodule
#   ./build.sh shell           # interactive shell, cwd = /work/kernelloader
#   ./build.sh <target> [...]  # any other make target, passed through
#
# Nothing host-specific is committed. If you want personal paths (e.g. copying
# the ELF onto a mounted memory card), make a gitignored wrapper that exports
# what it needs and execs this script.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${PS2BOOTOPIA_IMAGE:-ps2bootopia:local}"
SRC="kernelloader"
BIN="${HERE}/bin"

cd "$HERE"

if [ ! -f "${SRC}/Makefile" ]; then
    echo "[X] ${SRC}/ is empty — fetch the submodule first:" >&2
    echo "      git submodule update --init --recursive" >&2
    exit 1
fi

# kernelloader is upstream's code, pinned as a submodule. Our modern-toolchain
# fixes live in patches/ here rather than in that checkout, so the submodule
# stays clean and can be rebased on upstream. Apply them if they aren't already.
apply_patches() {
    local p
    for p in "${HERE}"/patches/*.patch; do
        [ -e "$p" ] || continue
        if git -C "$SRC" apply --check --reverse "$p" >/dev/null 2>&1; then
            echo "    [=] $(basename "$p") already applied"
        elif git -C "$SRC" apply --check "$p" >/dev/null 2>&1; then
            git -C "$SRC" apply "$p"
            echo "    [+] $(basename "$p") applied"
        else
            echo "[X] $(basename "$p") does not apply cleanly to ${SRC}/." >&2
            echo "    The submodule is probably at a different commit than the" >&2
            echo "    patch was made against. Expected base:" >&2
            echo "      $(head -20 "$p" | grep -m1 'base commit' || echo '      see patches/README.md')" >&2
            exit 1
        fi
    done
}

echo "==> Patches"
apply_patches

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
    -w "/work/${SRC}"
)

case "${1:-}" in
    shell)
        exec docker run -it "${common[@]}" "$IMAGE" /bin/bash
        ;;
    clean)
        docker run "${common[@]}" "$IMAGE" make clean || true
        rm -f "${BIN}"/*.elf 2>/dev/null || true
        echo "[✓] cleaned"
        exit 0
        ;;
esac

echo "==> Building kernelloader"
docker run "${common[@]}" -e ARGS="$*" "$IMAGE" bash -c '
    set -e
    make $ARGS
    # The loader ELF lands in loader/; copy whatever was produced into /work/bin.
    mkdir -p /work/bin
    for f in loader/kloader.elf loader/loader.elf kernel/kernel.elf; do
        [ -f "$f" ] && cp -f "$f" /work/bin/
    done
    true
'

echo
if ls "${BIN}"/*.elf >/dev/null 2>&1; then
    echo "[✓] output in bin/"
    ls -l --block-size=K "${BIN}"/*.elf | sed 's/^/    /'
else
    echo "[!] build finished but no ELF landed in bin/ — check the log above"
fi
