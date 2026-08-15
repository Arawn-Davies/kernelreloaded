PROGRAM = loader
PACKEDFILE = kloader.elf
include ../config.mk

EE_BIN = $(PROGRAM).elf
EE_OBJS = $(PROGRAM).o main.o jump.o entry.o exception.o interrupts.o graphic.o cache.o
EE_OBJS += menu.o menuEntry.o pad.o modules.o rom.o loadermenu.o configuration.o eedebug.o
EE_OBJS += SMS_SIF.o SMS_CDVD.o SMS_CDDA.o getrte.o getsbios.o getelf.o ps2dev9.o hdd.o
EE_OBJS += nvram.o kprint.o crc32check.o font.o
EE_LIBS = -lpatches -L$(PS2DEV)/gsKit/lib -lgskit -ldmakit -L$(PS2SDK)/ports/lib 
EE_LIBS += -lmc -lfileXio -lkbd -lcdvd
ifneq ($(LIBTIFF),)
EE_LIBS += -L$(LIBTIFF) -ltiff
endif
EE_LIBS += -lm -ldebug
EE_FLAGS = -g -I$(PS2DEV)/gsKit/include -I../include
EE_FLAGS += -Wall -W
ifeq ($(SHARED_MEM_DEBUG),yes)
EE_FLAGS += -DSHARED_MEM_DEBUG -DUSER_SPACE_SUPPORT=1
EE_OBJS += iopmem.o
endif
ifeq ($(RESET_IOP),yes)
EE_FLAGS += -DRESET_IOP
endif
ifeq ($(MX4SIO),yes)
EE_FLAGS += -DMX4SIO_SUPPORT
MODULES += mx4sio_bd.irx
endif
ifeq ($(SCREENSHOT),yes)
EE_FLAGS += -DSCREENSHOT
endif
ifeq ($(NEW_ROM_MODULES),yes)
EE_FLAGS += -DNEW_ROM_MODULES
EE_LIBS += -lpadx
else
EE_FLAGS += -DOLD_ROM_MODULES
EE_LIBS += -lpad
endif
ifeq ($(FILEIO_DEBUG),yes)
EE_CFLAGS += -DFILEIO_DEBUG
endif
ifeq ($(CALLBACK_DEBUG),yes)
EE_CFLAGS += -DCALLBACK_DEBUG
endif
ifeq ($(SBIOS_DEBUG),yes)
EE_CFLAGS += -DSBIOS_DEBUG
endif
ifeq ($(PAD_MOVE_SCREEN),yes)
EE_CFLAGS += -DPAD_MOVE_SCREEN
endif
EE_OBJS += $(addsuffix .o, $(subst .,_,$(ROM_FILES)))
EE_LIBS += -lz

# Autodetect if RTE is available.
EE_FLAGS += $(shell if [ -e "$(PS2LINUXDVD)/pbpx_955.09" ]; then echo -DRTE; fi)

EE_FLAGS += -I$(PS2SDK)/ports/include
EE_FLAGS += -I../tools/crc32gen/
# -Werror dropped for the modern-toolchain port. Combined with -W (-Wextra) it
# fails inside ps2sdk's OWN headers: rom0_info.h and osd_config.h trip
# -Wunused-parameter about 20 times before any kernelloader source is reached.
# Warnings still print. -Werror-implicit-function-declaration is kept below
# because that one catches real missing-header bugs rather than style.
# EE_FLAGS += -Werror
# Modern ps2sdk guards fileXio.h / fileXio_rpc.h / iox_stat.h with
#   #ifndef NEWLIB_PORT_AWARE
#   #error "Using fio/fileXio functions directly in the newlib port ..."
# It is an opt-in acknowledgement, not a deprecation: the concern is that
# newlib's POSIX layer and the fileXio RPC layer each keep their own idea of
# open files, so mixing them on the SAME file can desynchronise. kernelloader
# does not do that -- it uses fileXio only for fileXioInit/fileXioMkdir/umount
# style operations and plain stdio (fopen/fread) for file contents. Six files
# need it: configuration.cpp, loadermenu.cpp, modules.c, getelf.c, getrte.c,
# getsbios.c.
#
# Defining it is the intended way to say "yes, deliberately". Porting those
# calls to POSIX equivalents would be the larger, more correct change, and is
# worth revisiting once the loader runs -- see docs/porting-notes.md.
EE_FLAGS += -DNEWLIB_PORT_AWARE

# Draw all text through the BIOS ROM font instead of the glyph atlas
# (make FONT_ROM=1). A bisect aid for hardware faults -- see font.cpp.
ifneq ($(FONT_ROM),)
EE_FLAGS += -DFONT_ROM
endif

# Leave the starfield backdrop unloaded (make NO_BACKDROP=1). A VRAM bisect
# aid -- see the note at texStarfield in graphic.cpp.
ifneq ($(NO_BACKDROP),)
EE_FLAGS += -DNO_BACKDROP
endif

ifneq ($(FAKE_EXTRA_RAM),)
EE_FLAGS += -DFAKE_MAXMEM_MB=$(FAKE_EXTRA_RAM)
endif

EE_CFLAGS += $(EE_FLAGS) -Wimplicit-int -Werror-implicit-function-declaration

# The -D_STLP_* defines, -Dwint_t=int and the stlport include path are all
# vestigial: modern ps2sdk ships no STLport at all (that directory does not
# exist, and citronalco's fix already dropped -lstlport in favour of the real
# std:: headers). The _STLP_* ones are harmless no-ops without the library, but
# -Dwint_t=int is actively fatal -- gcc 15's stddef.h does
#     typedef __WINT_TYPE__ wint_t;
# which the command-line define rewrites to "typedef unsigned int int;",
# giving "multiple types in one declaration" before any source is read.
# Dropped along with the dead -I; the _STLP_* defines are left alone to keep
# this diff to what actually breaks.
EE_CXXFLAGS = $(EE_FLAGS) -D_STLP_NO_NAMESPACES -D_NOTHREADS -D_STLP_NO_EXCEPTIONS -D_STLP_USE_NEWALLOC -D_STLP_HAS_WCHAR_T -D_STLP_NO_IOSTREAMS

# ps2link.irx and ps2smap.irx are absent from this list because modern ps2sdk
# ships neither. ps2link moved out of ps2sdk into its own project
# (ps2dev/ps2link); ps2smap.irx was renamed and restructured -- the SMAP driver
# is now smap.irx / smap-ps2ip.irx / smap-modular-netman.irx, which take
# different module arguments than the "ip=... netmask=... gateway=..." string
# getPS2MAPParameter() builds, so substituting one blind would load a driver
# that silently ignores its configuration.
#
# The matching moduleList[] entries in modules.c are removed to match. Both were
# .network = -1, so they only ever loaded on consoles where hasNetworkSupport()
# is true, and upstream's readme.txt warns that these IOP network modules are
# mutually exclusive with Linux's own SMAP driver -- the system hangs if the IOP
# touches the network. Booting Linux from mc/USB does not need either.
#
# Restoring network + ps2link support means vendoring a ps2link.irx and porting
# getPS2MAPParameter() to the modern smap argument format.
MODULES = \
	ps2dev9.irx \
	fileXio.irx \
	iomanX.irx \
	ioptrap.irx \
	poweroff.irx \
	ps2ip.irx \
	ps2http.irx \
	sharedmem.irx \
	smaprpc.irx \
	dev9init.irx \
	usbd.irx \
	bdm.irx \
	bdmfs_fatfs.irx \
	usbmass_bd.irx \
	ps2kbd.irx

ROM_FILES += $(MODULES) \
	defaultconfig.txt \
	SMSUTILS.irx \
	SMSCDVD.irx \
	eromdrvloader.irx \
	TGE/sbios_old.elf \
	TGE/sbios_new.elf \
	TGE/intrelay-direct.irx \
	TGE/intrelay-direct-rpc.irx \
	TGE/intrelay-dev9.irx \
	TGE/intrelay-dev9-rpc.irx \
	kernel.elf \
	mcicons/icon.sys \
	mcicons/kloader.icn \
	$(RGB_FILES)

RGB_FILES = \
	folder.rgb \
	penguin.rgb \
	up.rgb \
	back.rgb \
	selected.rgb \
	unselected.rgb \
	disc.rgb \
	starfield.rgb \
	title.rgb \
	bottombar.rgb \
	panel.rgb \
	highlight.rgb \
	bartrack.rgb \
	barfill.rgb \
	glyphs.rgb

RGB_HEADERS = \
	$(subst .rgb,.h, $(RGB_FILES))

RTE_FILES += \
	RTE/cdvdfsv.irx \
	RTE/cdvdman.irx \
	RTE/dmarelay.irx \
	RTE/iopintr.irx \
	RTE/libsd.irx \
	RTE/mcman.irx \
	RTE/mcserv.irx \
	RTE/padman.irx \
	RTE/sdrdrv.irx \
	RTE/sio2man.irx \
	RTE/sbios.elf

ROM_FILES += $(shell if [ -e "$(PS2LINUXDVD)/pbpx_955.09" ]; then echo $(RTE_FILES); fi)

all: $(PACKEDFILE)

$(PACKEDFILE): $(EE_BIN)
	../tools/crc32gen/crc32gen $^
	ps2-packer $^ $@

$(MODULES):
	for file in $@; do install -m 644 $(PS2SDK)/iop/irx/$$file $$file; done

rom.o: rom.c romfilelist.h romdefinitions.h rominitialize.h

test: all
ifeq ($(LOAD_PS2LINK),yes)
	ps2client -h $(TARGET_IP) execee host:$(PACKEDFILE) -d
else
	ps2client -h $(TARGET_IP) execee host:$(PACKEDFILE) --fix-no-disc
endif

reset: $(PROGRAM).elf
	ps2client -h $(TARGET_IP) reset

clean:
	rm -f $(EE_BIN) *.o
	rm -f kernel.elf $(MODULES)
	rm -f romfilelist.h romdefinitions.h rominitialize.h
	rm -f $(RGB_HEADERS) $(RGB_FILES)
	rm -f $(PACKEDFILE)
	rm -f $(ROM_FILES)
	rm -f $(addsuffix .s,$(subst .,_,$(ROM_FILES)))
	rm -f $(addsuffix .o,$(subst .,_,$(ROM_FILES)))
	rm -rf .depend
	rm -rf TGE RTE
	rm -rf mcicons

romfilelist.h: $(RGB_HEADERS) $(RGB_FILES)
	rm -f romfilelist.h
	for file in $(ROM_FILES); do \
		echo "{ \"$$file\", NULL, 0, 0, 0, 0}," >> romfilelist.h; \
	done

romdefinitions.h:
	rm -f romdefinitions.h
	for file in $(subst -,_,$(subst .,_,$(subst /,_,$(ROM_FILES)))); do \
		echo "extern unsigned char $$file[];" >> romdefinitions.h; \
		echo "extern int size_$$file;" >> romdefinitions.h; \
	done

rominitialize.h: $(RGB_FILES)
	rm -f rominitialize.h
	for file in $(subst -,_,$(subst .,_,$(subst /,_,$(ROM_FILES)))); do \
		if [ "`echo $$file | cut -d '_' -f 1 --complement`" = "rgb" ]; then \
			echo "#include \"$${file}\"" | sed -e "s/_rgb\"/\.h\"/g" >> rominitialize.h; \
		fi; \
	done
	for file in $(subst -,_,$(subst .,_,$(subst /,_,$(ROM_FILES)))); do \
		echo "rom_files[i].start = $$file;" >> rominitialize.h; \
		echo "rom_files[i].size = size_$$file;" >> rominitialize.h; \
		if [ "`echo $$file | cut -d '_' -f 1 --complement`" = "rgb" ]; then \
			ufile="`echo $$file | perl -n -e \"s/_rgb\$$//g; print uc($$@);\"`"; \
			echo "rom_files[i].width = $${ufile}_WIDTH;" >> rominitialize.h; \
			echo "rom_files[i].height = $${ufile}_HEIGHT;" >> rominitialize.h; \
			echo "rom_files[i].depth = $${ufile}_DEPTH;" >> rominitialize.h; \
		fi; \
		echo "i++;" >> rominitialize.h; \
	done

%_irx.s: %.irx
	rm -f $@.tmp
	rm -f $@
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_irx
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@
	rm -f $@.tmp

%_elf.s: %.elf
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_elf
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@

%_rgb.s: %.rgb
	rm -f $@.tmp
	rm -f $@
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_rgb
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@
	rm -f $@.tmp

%.rgb %.h: %.ppm ../tools/ppm2rgb/ppm2rgb
	../tools/ppm2rgb/ppm2rgb $*.ppm $*.rgb $*.h $*

%.rgb %.h: %.png ../tools/png2rgb/png2rgb
	../tools/png2rgb/png2rgb $*.png $*.rgb $*.h $*

%_bmp.s: %.bmp
	rm -f $@.tmp
	rm -f $@
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_bmp
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@
	rm -f $@.tmp

# Embed the built-in fallback configuration. Same shape as the rules above;
# .txt had no rule because nothing text-based was embedded before.
%_txt.s: %.txt
	rm -f $@.tmp
	rm -f $@
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_txt
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@
	rm -f $@.tmp

%_sys.s: %.sys
	rm -f $@.tmp
	rm -f $@
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_sys
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@
	rm -f $@.tmp

%_icn.s: %.icn
	rm -f $@.tmp
	rm -f $@
	bin2s $^ $@.tmp $(subst -,_,$(subst /,_,$*))_icn
	sed -e "s/\.data/.section .rom/g" <$@.tmp >$@
	rm -f $@.tmp

mcicons/%.icn: ../assets/mcicons/%.icn
	mkdir -p mcicons
	cp $^ $@

mcicons/%.sys: ../assets/mcicons/%.sys
	mkdir -p mcicons
	cp $^ $@

mcicons/%.bmp: ../assets/mcicons/%.bmp
	mkdir -p mcicons
	cp $^ $@

.depend/%.d: %.c
	mkdir -p .depend
	$(EE_CC) -E -M -MG $(EE_CFLAGS) $(EE_INCS) $< >$@

.depend/%.d: %.cpp
	mkdir -p .depend
	$(EE_CXX) -E -M -MG $(EE_CXXFLAGS) $(EE_INCS) $< >$@

include Makefile.pref
include Makefile.eeglobal
ifneq ($(MAKECMDGOALS),clean)
-include $(addprefix .depend/,$(subst .o,.d, $(EE_OBJS)))
endif
