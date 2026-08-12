# intrelay restored. rickgaiser removed it in 4ba4d6e ("Remove irx modules no
# longer needed", intrelay -> obsolete) alongside dmarelay, eedebug and smaprpc.
# The last two moved to the linux-firmware-ps2 repo, but intrelay is still
# REQUIRED by the 2.4.17-era kernels this loader targets -- upstream's own
# readme says so: "Redirects interrupts from IOP to EE. Required: Yes." Without
# it the loader gets as far as loading the kernel and then fails on
# host:TGE/intrelay-direct.irx, which loader.c still references.
#
# Four variants, selected by the two switches, all installed into loader/TGE/:
#   intrelay-direct.irx        no DEV9, no RPC  -- the normal one
#   intrelay-direct-rpc.irx    slim PSTwo
#   intrelay-dev9.irx          when ps2dev9/ps2link is used
#   intrelay-dev9-rpc.irx      both
#
# dmarelay is deliberately NOT restored: upstream's readme states "dmarelay.irx
# from TGE is not working".
install:
	make -C iop/intrelay DEV9_SUPPORT=no RPC_IRQ_SUPPORT=no install
	make -C iop/intrelay DEV9_SUPPORT=no RPC_IRQ_SUPPORT=yes install
	make -C iop/intrelay DEV9_SUPPORT=yes RPC_IRQ_SUPPORT=no install
	make -C iop/intrelay DEV9_SUPPORT=yes RPC_IRQ_SUPPORT=yes install
	make ROM_MODULE_VERSION=old -C sbios install
	make ROM_MODULE_VERSION=new -C sbios install

clean:
	make ROM_MODULE_VERSION=old -C sbios clean
	make ROM_MODULE_VERSION=new -C sbios clean
	make -C iop/intrelay DEV9_SUPPORT=no RPC_IRQ_SUPPORT=no clean
	make -C iop/intrelay DEV9_SUPPORT=no RPC_IRQ_SUPPORT=yes clean
	make -C iop/intrelay DEV9_SUPPORT=yes RPC_IRQ_SUPPORT=no clean
	make -C iop/intrelay DEV9_SUPPORT=yes RPC_IRQ_SUPPORT=yes clean
