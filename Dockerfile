# Dockerized PlayStation 2 build environment for kernelloader / kernelreloaded.
#
# The upstream ps2dev image ships the full PS2 cross-toolchain
# (mips64r5900el-ps2-elf-gcc, ps2sdk, gsKit) with PS2DEV/PS2SDK/PATH already
# set -- but it is a minimal Alpine image with no `make`, and kernelloader
# needs rather more than that. Everything below exists because the build
# fails without it; see docs/ for the full porting notes.
#
# Build with ./build.sh (see that script), which mounts the repo at /work.
FROM ghcr.io/ps2dev/ps2dev:latest

# make/bash: absent from the base image (same as ps2oom).
# gcc/musl-dev: kernelloader builds HOST tools (ppm2rgb, png2rgb) as well as
#   EE code, so it needs a native compiler too.
# libpng/zlib/tiff -dev: png2rgb links libpng; the loader pulls tiff and zlib.
#
# perl/coreutils: loader/Makefile generates rominitialize.h, which carries the
#   width/height/depth of every embedded .rgb image. That rule guards the image
#   fields with `cut -d '_' -f 1 --complement` and derives the macro prefix with
#   a perl one-liner. Alpine has neither: busybox cut rejects --complement, so
#   the guard fails and the block never runs, and perl is absent entirely.
#   The build still succeeds -- it just silently emits no width/height/depth for
#   any texture, leaving them 0. Every image then draws as a 0x0 sprite: no
#   error, nothing on screen, and no clue why. coreutils supplies GNU cut.
RUN apk add --no-cache make bash gcc musl-dev libpng-dev zlib-dev tiff-dev \
                       perl coreutils

# kernel/Makefile (and others) hardcode the old ee-* tool names. Modern ps2sdk
# renamed everything to the mips64r5900el-ps2-elf-* triple, so alias them
# rather than patching every Makefile.
RUN for f in /usr/local/ps2dev/ee/bin/mips64r5900el-ps2-elf-*; do \
        b=$(basename "$f"); \
        ln -sf "$f" "/usr/local/bin/ee-${b#mips64r5900el-ps2-elf-}"; \
    done

# IOP modules (sharedmem/, modules/) do:
#     include $(PS2SDKSRC)/Defs.make
#     include $(PS2SDKSRC)/iop/Rules.make
#
# The INSTALLED SDK tree has Defs.make but not iop/Rules.make -- the build rules
# ship only in the ps2sdk *source* repo (iop/ there has Rules.make,
# Rules.bin.make, Rules.lib.make, Rules.release; the installed iop/ has just
# include/ irx/ lib/ startup/). So a source checkout is genuinely required.
#
# Not pinned: the installed SDK carries no commit marker to match against, and
# these rule files change rarely. If IOP modules start failing oddly after a
# rebuild, an upstream drift here is the first thing to suspect.
RUN apk add --no-cache git \
 && git clone --depth 1 https://github.com/ps2dev/ps2sdk.git /usr/local/src/ps2sdk
ENV PS2SDKSRC=/usr/local/src/ps2sdk

# The source tree keeps IOP headers per-module (iop/system/threadman/include/
# thbase.h) because ps2sdk expects modules to be built inside it and to declare
# IOP_IMPORT_INCS. Out-of-tree modules like kernelloader's sharedmem/ don't,
# so they miss headers the INSTALLED sdk provides flattened in iop/include/.
#
# iop/Rules.make line 24 reads "IOP_INCS := $(IOP_INCS) ...", i.e. it prepends
# whatever we pass in -- so pointing at the flattened installed headers fixes
# every such module at once, with no change to upstream Makefiles.
ENV IOP_INCS="-I/usr/local/ps2dev/ps2sdk/iop/include"

# iop/Rules.make sets "IOP_WARNFLAGS ?= -Wall -Werror", so the environment wins.
#
# -Werror is dropped rather than suppressed warning-by-warning. gcc has grown a
# lot of diagnostics since 2003, and this code trips a new class every few
# files: -Wpointer-sign, -Wunused-but-set-variable, -Wattributes (packed on a
# char field, which is a genuine no-op), -Wunused-variable. Chasing each with a
# -Wno- flag is whack-a-mole that also risks masking a real one.
#
# -Wall is kept, so every warning still prints on every build and stays
# reviewable — they simply no longer halt it. Note the EE side is treated
# differently: TGE/sbios keeps -Werror, with narrowly targeted suppressions,
# because that is where a genuine out-of-bounds bug surfaced (see
# docs/porting-notes.md on mc.c).
ENV IOP_WARNFLAGS="-Wall"

# tools/bin2s replaces the bin2s that modern ps2sdk dropped (it ships bin2c,
# which has a different interface). It comes from the mounted repo so edits
# take effect without rebuilding this image.
ENV PATH="/work/tools:${PATH}"

WORKDIR /work
