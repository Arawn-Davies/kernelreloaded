include ../../config.mk
EE_PREFIX = ee-
EE_CC = $(EE_PREFIX)gcc
EE_OBJCOPY = $(EE_PREFIX)objcopy

# Default value, overwritten by calling makefile.
ROM_MODULE_VERSION=old

EE_INCS = -I. -I../include -I../../include
EE_CFLAGS = -D_EE
EE_CFLAGS += -G0 -Wall -g
ifeq ($(SBIOS_DEBUG),yes)
# Debug code need more memory, so optimize for size.
EE_CFLAGS += -Os
# Optimize for speed.
else
ifeq ($(ROM_MODULE_VERSION),new)
# SBIOS for new module is larger and need to be optimized for size.
EE_CFLAGS += -Os
else
# Was -O2 ("SBIOS for old modules can be optimized for speed"). gcc 15 emits
# noticeably more code than the 2000s toolchain, and correcting u64 in
# tge_types.h from 32-bit to a true 64-bit doubled every u64 in static data --
# together overflowing the fixed 0xEFE0 region (see linkfile; it cannot grow,
# the PS2 model-number string sits at the end). -Os is the same remedy this
# Makefile already applies to the 'new' variant for the same reason.
EE_CFLAGS += -Os
endif
endif
EE_CFLAGS += $(EE_INCS)
EE_CFLAGS += -Wall -W -Werror-implicit-function-declaration -Wimplicit-int -Werror
EE_CFLAGS += -fno-builtin-memcpy -fno-builtin-memset
# This freestanding stdio.h declares snprintf/vsnprintf with an int length where
# gcc's builtins expect size_t. Combined with -Werror above, gcc 15 rejects the
# mismatch. Suppressing the builtin for these two keeps the local prototypes
# authoritative without changing any signature or implementation.
EE_CFLAGS += -fno-builtin-snprintf -fno-builtin-vsnprintf
# tge_types.h gates u128/s128 behind "#if defined(R5900) || defined(_R5900)",
# which the old ee-gcc predefined. Modern mips64r5900el-ps2-elf-gcc defines
# _MIPS_ARCH_R5900 instead, so u128 vanished -- surfacing as "unknown type name
# 'u128'" and, because that malformed iop_dmatag_t, a bogus
# "size of array element is not a multiple of its alignment" alongside it.
EE_CFLAGS += -D_R5900
# 2003-era code assigns freely between u32*/int* and similar. gcc grew
# -Wpointer-sign since, and -Werror above turns every one into a build failure.
# These are benign signedness mismatches, not type confusion; suppressing the
# warning is far safer than casting at dozens of call sites in SIF/DMA code.
EE_CFLAGS += -Wno-pointer-sign
# mc.c:1140 indexes mcRpcCmd[2][17] with MC_FUNC_CHG_PRITY (0x14 = 20) -- the
# wrong enum family. That table is indexed by MC_RPCCMD_* everywhere else, and
# has no CHG_PRITY entry, so this reads out of bounds. It is a PRE-EXISTING bug
# that gcc 15 newly detects, not something this port introduced, and fixing it
# needs the correct MCSERV command byte. Demoted to a warning (not silenced) so
# it stays visible until someone can resolve it.
EE_CFLAGS += -Wno-error=array-bounds
# The SBIOS type-puns through pointer casts all over (hardware registers,
# SIF packets, sound regs), which violates strict aliasing. At -O2 gcc both
# warns and is entitled to optimise on the assumption. Disabling the assumption
# is the correct fix for code like this -- rewriting every pun via memcpy or
# unions would be a large, risky change to code that cannot be tested here. */
EE_CFLAGS += -fno-strict-aliasing
# BSD doesn't enable floating point registers. Only printf() saves them as precaution.
EE_CFLAGS += -msoft-float

EE_LDFLAGS = -nostartfiles -nostdlib -Tlinkfile

EE_FILES = prologue sbios misc sifdma core sifcmd sifrpc iopheap cache \
	strlen strncpy memcpy pad memset strcpy strcmp cdvd ncmd scmd smod iopmem \
	memcmp mc mutex sound dve_reg
EE_OBJS = $(addsuffix _$(ROM_MODULE_VERSION).o, $(EE_FILES))
EE_FILES += printf snprintf
ifeq ($(FILEIO_DEBUG),yes)
EE_CFLAGS += -DFILEIO_DEBUG
EE_FILES += fileio
endif
ifeq ($(SHARED_MEM_DEBUG),yes)
EE_CFLAGS += -DSHARED_MEM_DEBUG
EE_FILES += iopmemdebug
endif
ifeq ($(CALLBACK_DEBUG),yes)
EE_CFLAGS += -DCALLBACK_DEBUG -fno-builtin
endif
ifeq ($(SIO_DEBUG),yes)
EE_CFLAGS += -DSIO_DEBUG
endif
ifeq ($(ROM_MODULE_VERSION),old)
EE_CFLAGS += -DOLD_ROM_MODULE_VERSION
else
EE_CFLAGS += -DNEW_ROM_MODULE_VERSION
endif
ifeq ($(SBIOS_DEBUG),yes)
EE_CFLAGS += -DSBIOS_DEBUG
endif

install: all
	mkdir -p ../../loader/TGE
	install -m 644 -p sbios_$(ROM_MODULE_VERSION).elf ../../loader/TGE/
	ee-strip ../../loader/TGE/sbios_$(ROM_MODULE_VERSION).elf

all: sbios_$(ROM_MODULE_VERSION).bin

sbios_$(ROM_MODULE_VERSION).bin: sbios_$(ROM_MODULE_VERSION).elf
	$(EE_OBJCOPY) -I elf32-littlemips -O binary $< $@

sbios_$(ROM_MODULE_VERSION).elf: $(EE_OBJS)
	$(EE_CC) $(EE_LDFLAGS) -o $@ $(EE_OBJS)

clean:
	rm -f sbios_$(ROM_MODULE_VERSION).bin sbios_$(ROM_MODULE_VERSION).elf $(EE_OBJS)

%_$(ROM_MODULE_VERSION).o: %.c
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

%_$(ROM_MODULE_VERSION).o: %.S
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
