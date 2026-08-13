include config.mk

all:
	make -C tools/ppm2rgb
	make -C tools/png2rgb
	make -C tools/hello
	make -C kernel
	# smaprpc and dev9init restored from 4ba4d6e^. On a slim PSTwo, Linux cannot
	# drive the ethernet from the EE side -- loader.c's own comment on
	# intrelay-dev9-rpc says "Network not working from EE side (use smaprpc.irx)".
	# Removing them was safe for a fat console and broke NFS root on a slim.
	make -C iop
	make -C TGE
	if [ -e $(PS2LINUXDVD)/pbpx_955.09 ]; then \
		make -C RTE; \
	fi
	make -C tools/crc32gen
	make -C loader

test:
	make -C loader test

reset:
	make -C loader reset

clean:
	make -C kernel clean
	make -C iop clean
	make -C TGE clean
	make -C RTE clean
	make -C loader clean
	make -C tools/crc32gen clean
	make -C tools/ppm2rgb clean
	make -C tools/png2rgb clean
	make -C tools/hello clean
