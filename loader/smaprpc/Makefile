#  _____     ___ ____
#   ____|   |    ____|      PSX2 OpenSource Project
#  |     ___|   |____       (C)2002, David Ryan ( Oobles@hotmail.com )
#                           (C)2003, adresd     ( adresd_ps2dev@yahoo.com )
#  ------------------------------------------------------------------------
#
# Restored from 4ba4d6e^ and rebuilt against modern ps2sdk. The original
# included $(PS2SDK)/Defs.make plus a vendored Rules.make copy; the build rules
# now ship only in the ps2sdk SOURCE tree, so this follows the same pattern as
# sharedmem/ -- which is the one IOP module in this tree that already builds.
# The vendored Rules.make is left in place but unused.

IOP_OBJS_DIR = ./
IOP_BIN_DIR  = ./
IOP_SRC_DIR  = ./

IOP_BIN = smaprpc.irx
IOP_OBJS = main.o smap.o smap_rpc.o imports.o

ifeq ($(DEBUG),1)
DEBUG_FLAGS = -DDEBUG
endif

IOP_CFLAGS = -D_PS2_ -I../include -fno-builtin $(DEBUG_FLAGS)
IOP_LDFLAGS = -Wl,-s

install: all
	cp $(IOP_BIN) ../loader

all: $(IOP_BIN)

$(IOP_BIN): $(IOP_OBJS)

clean:
	rm -f *.elf *.o *.a *.irx imports.c

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.make
include $(PS2SDKSRC)/iop/Rules.release
