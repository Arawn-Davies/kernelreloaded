# Restored from 4ba4d6e^ and rebuilt against modern ps2sdk. The original
# included $(PS2SDK)/samples/Makefile.pref and Makefile.iopglobal, neither of
# which exists any more; this follows sharedmem/'s pattern instead.

IOP_OBJS_DIR = ./
IOP_BIN_DIR  = ./
IOP_SRC_DIR  = ./

IOP_BIN = dev9init.irx
IOP_OBJS = dev9init.o hdd.o imports.o
IOP_LDFLAGS = -Wl,-s
IOP_CFLAGS = -D_PS2_ -I../../include

install: all
	install -m 644 $(IOP_BIN) ../../loader/

all: $(IOP_BIN)

$(IOP_BIN): $(IOP_OBJS)

clean:
	rm -f *.elf *.o *.a *.irx imports.c

include $(PS2SDKSRC)/Defs.make
include $(PS2SDKSRC)/iop/Rules.make
include $(PS2SDKSRC)/iop/Rules.release
