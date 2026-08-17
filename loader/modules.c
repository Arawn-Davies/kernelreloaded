/* Copyright (c) 2007 Mega Man */
#include <stdio.h>
#include <string.h>
/* Modern ps2sdk/newlib headers no longer pull these in transitively:
 *   stdlib.h  strtoul()
 *   fcntl.h   open(), O_RDONLY
 *   unistd.h  read(), close()
 *   fileio.h  fioExit() -- still present in ps2sdk, just never included here;
 *             needs NEWLIB_PORT_AWARE, set in the Makefile. */
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <fileio.h>
#include <kernel.h>
#include <iopheap.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <iopcontrol.h>

#include "modules.h"
#include "graphic.h"
#include "bootlog.h"
#include "loader.h"
#include "rom.h"
#include "eedebug.h"
#include "configuration.h"
#include "fileXio_rpc.h"
#include "SMS_CDVD.h"
#include "SMS_CDDA.h"
#include "graphic.h"
#include "loadermenu.h"
#include "nvram.h"
#include "kprint.h"

#ifdef NEW_ROM_MODULES
#define MODPREFIX "X"
#endif

#ifdef OLD_ROM_MODULES
#define MODPREFIX ""
#endif

#define IRX_MAGIC_EXPORT 0x41c00000

/** Structure describing module that should be loaded. */
typedef struct
{
	/** Path to module file. */
	const char *path;
	/** Parameter length. */
	unsigned int argLen;
	/** Module parameters. */
	const char *args;
	/** True, if ps2smap. */
	int ps2smap;
	/** True, if configuration should be loaded. */
	int loadCfg;
	/** True, if module can be loaded from "mc0:/kloader/". */
	int checkMc;
	/** True, if module is responsible eromdrv. */
	int eromdrv;
	/** True, if it is SMS module which controls DVDV. */
	int sms_mod;
	/** True, if module handles DNS. */
	int dns;
	/** True, if module needs network. */
	int network;
	/** 1, if debug mode. 0, load always. -1, no debug mode */
	int debug_mode;
	/** True, if the module is allowed to be missing from this console's ROM.
	 *
	 * Its absence is logged and the boot continues, rather than queueing an
	 * error. A queued error makes the "Buffer check" stage in loader.c call
	 * waitForUser(), which blocks on a pad press -- so a module that is simply
	 * not in this ROM could otherwise halt the boot outright. This is the same
	 * exemption the eromdrv case below already had, generalised. */
	int optional;
} moduleLoaderEntry_t;


static char eromdrvpath[MAX_INPUT_LEN] = "rom1:EROMDRVE";

static moduleLoaderEntry_t moduleList[] = {
#if defined(IOP_RESET)
	{
		.path = "eedebug.irx",
		.argLen = 0,
		.args = NULL,
		.debug_mode = -1,
	},
#endif
	{
		/* Stop sound. */
		.path = "rom0:CLEARSPU",
		.argLen = 0,
		.args = NULL
	},
	{
		/* Module is required to access rom1:
		 *
		 * Absent from the SCPH-10000/15000 "ProtoKernel" ROMs, which predate
		 * it -- rom0 has no ADDDRV at all there and both load and start return
		 * -203. rom1: access is then unavailable (so is DVD-Video, which the
		 * eromdrv entry below already degrades over), but Linux boots fine
		 * without it. Optional so it cannot strand the boot on a pad prompt. */
		.path = "rom0:ADDDRV",
		.argLen = 0,
		.args = NULL,
		.optional = 1,
	},
	{
		/* Module is required to access video DVDs */
		.path = "eromdrvloader.irx",
		.argLen = 0,
		.args = NULL,
		.eromdrv = -1
	},
#if defined(RESET_IOP)
	{
		.path = "rom0:" MODPREFIX "SIO2MAN",
		.argLen = 0,
		.args = NULL,
		.debug_mode = -1,
	},
	{
		.path = "rom0:" MODPREFIX "MCMAN",
		.argLen = 0,
		.args = NULL,
		.debug_mode = -1,
	},
	{
		.path = "rom0:" MODPREFIX "MCSERV",
		.argLen = 0,
		.args = NULL,
		.debug_mode = -1,
	},
#endif
	{
		.path = "rom0:" MODPREFIX "PADMAN",
		.argLen = 0,
		.args = NULL,
		.loadCfg = -1 /* MC modules are loaded before this entry. */
	},
	{
		.path = "SMSUTILS.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.sms_mod = -1
	},
	{
		.path = "SMSCDVD.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.sms_mod = -1
	},
#if defined(RESET_IOP)
	{
		.path = "ioptrap.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.debug_mode = -1,
	},
	{
		.path = "iomanX.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.debug_mode = -1,
	},
	{
		.path = "poweroff.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.debug_mode = -1,
	},
	{
		.path = "ps2dev9.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.debug_mode = -1,
	},
	{
		.path = "ps2ip.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.debug_mode = -1,
	},
	/* ps2smap.irx and ps2link.irx entries removed: modern ps2sdk ships neither,
	 * so loader/Makefile can no longer embed them as ROM blobs and these
	 * entries would fail rom_getFile() at runtime. See the comment above
	 * MODULES in loader/Makefile for what restoring them would take.
	 *
	 * Both were .network = -1, so they only loaded when hasNetworkSupport()
	 * was true. Upstream readme.txt: these IOP network modules are mutually
	 * exclusive with Linux's own SMAP driver and hang the system if the IOP
	 * uses the network -- so a memory-card/USB Linux boot is unaffected. */
#endif
	{
		.path = "ps2http.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
		.network = -1,
	},
	/* USB mass storage, via the Block Device Manager stack.
	 *
	 * This replaces usbhdfsd.irx, the FAT16/FAT32-only driver that predates
	 * BDM. The new stack is what OPL, Neutrino and NHDDL use, and it brings
	 * exFAT (FatFs), GPT as well as MBR partition tables, and the option of
	 * reaching the internal ATA disk through the same filesystem layer.
	 *
	 * Load order follows the import lists and must not be reshuffled:
	 *   bdm          - the manager itself; depends on nothing but the kernel
	 *   bdmfs_fatfs  - filesystem driver; imports bdm and ioman (iomanX above)
	 *   usbd         - USB host stack
	 *   usbmass_bd   - USB block device; imports bdm and usbd
	 *
	 * bdmfs_fatfs registers the device as "mass", and its own comment notes it
	 * "uses global connection order for full backward compatibility" -- so
	 * existing mass0: paths in config.txt keep working. */
	{
		.path = "bdm.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1
	},
	{
		.path = "bdmfs_fatfs.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1
	},
	{
		.path = "usbd.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1
	},
	{
		.path = "usbmass_bd.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1
	},
	/* MX4SIO: SD card in the memory card slot, as another BDM transport.
	 * Imports bdm exactly as usbmass_bd does, so it slots into the same stack
	 * and its cards appear as mass devices through bdmfs_fatfs.
	 *
	 * Note it drives SIO2 directly (dmacman/intrman, no sio2man import) and
	 * physically occupies a memory card slot -- so on a console with the
	 * adapter fitted, that slot is not available as mc0:/mc1:. Relevant to the
	 * mc0/mc1 config search: the adapter and a config on that card are
	 * mutually exclusive. */
#ifdef MX4SIO_SUPPORT
	{
		.path = "mx4sio_bd.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1
	},
#endif
	{
		.path = "fileXio.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1,
	},
	{
		.path = "ps2kbd.irx",
		.argLen = 0,
		.args = NULL,
		.checkMc = -1
	},
};

static int moduleLoaderNumberOfModules = sizeof(moduleList) / sizeof(moduleLoaderEntry_t);

/** Parameter for IOP reset. */
#ifdef NEW_ROM_MODULES
/* XXX: This will load the newer CDVDMAN module which doesn't support reading of NVRAM. */
static char s_pUDNL   [] __attribute__(   (  section( ".data" ), aligned( 1 )  )   ) = "rom0:UDNL rom0:EELOADCNF";
#endif
#ifdef OLD_ROM_MODULES
/* This will load the old CDVDMAN module which supports reading of NVRAM. */
static char s_pUDNL   [] __attribute__(   (  section( ".data" ), aligned( 1 )  )   ) = "rom0:UDNL";
#endif

char ps2_rom_version[256] = "unknown";
static char version[256];

static int romver = 0;

static int eromdrvSupport;

static int libsd_version = 0x7FFFFFFF;

static int network_support = -1;

char hardware_information[128] = "unknown";

int isSlimPSTwo(void)
{
	if (romver > 0x0190) {
		return -1;
	} else {
		return 0;
	}
}

int hasNetworkSupport(void)
{
	return network_support;
}

int isDVDVSupported(void)
{
	return eromdrvSupport;
}

void checkROMVersion(void)
{
	int fd;
	int ret;

	fd = open("rom0:ROMVER", O_RDONLY);
	if (fd >= 0) {
		ret = read(fd, version, sizeof(version));
		close(fd);
		if (ret > 0) {
			int len = ret;

			/* rom0:ROMVER is 16 bytes and newline-terminated:
			 * "0160EC20011004\n\0". The old ps2_rom_version[ret - 1] = 0
			 * zeroed index 15, which was ALREADY the terminator, so the
			 * newline at index 14 survived into the string.
			 *
			 * Invisible in a log -- it just breaks the line early -- but not
			 * on screen: the UI font maps an unprintable character to a space,
			 * so the version measured about four pixels wider than it drew and
			 * every right-aligned copy of it sat that far left of the column.
			 *
			 * Strip every trailing control character rather than assuming one:
			 * some ROMs pad with \r\n. */
			if (len > (int) sizeof(ps2_rom_version) - 1) {
				len = sizeof(ps2_rom_version) - 1;
			}
			memcpy(ps2_rom_version, version, len);
			ps2_rom_version[len] = 0;
			while ((len > 0) && ((unsigned char) ps2_rom_version[len - 1] <= ' ')) {
				len--;
				ps2_rom_version[len] = 0;
			}
		}
		version[4] = 0;
		romver = strtoul(version, NULL, 16);
	}
}

int getBiosVersion(void)
{
	return romver;
}

void checkLibsdExport(FILE *fin)
{
	int dummy;
	char modulename[9];

	// unused:
	if (fread(&dummy, sizeof(int), 1, fin) != 1)
		return;

	// Read version number
	if (fread(&libsd_version, sizeof(int), 1, fin) != 1)
		return;

	// Read version number
	if (fread(modulename, 8, 1, fin) != 1)
		return;

	modulename[8] = 0;

	if (strcmp(modulename, "libsd") != 0) {
		/* Found module description. */
		libsd_version = 0x7FFFFFFF;
	}
}

int get_libsd_version(void)
{
	return libsd_version;
}

void checkForMusicSupport(void)
{
	FILE *fin;

	fin = fopen("rom1:LIBSD", "rb");
	if (fin != NULL) {
		int magic;

		while(fread(&magic, sizeof(int), 1, fin) == 1) {
			if (magic == IRX_MAGIC_EXPORT)
				checkLibsdExport(fin);
		}
		fclose(fin);
	} else {
		kprintf("Failed to open rom1:LIBSD.\n");
	}
}

/** Region letter used to build the "rom1:EROMDRV<X>" path.
 *
 * The EROM driver filename is region-suffixed. Historically this was taken
 * from NVRAM alone, which fails on any console or emulator whose NVRAM reads
 * back blank or unrecognised -- PCSX2 for instance returns all zeroes, giving
 * "S00 T00 F00 R00" and a nonsense path.
 *
 * ROMVER is a far more dependable source and is already read at startup by
 * checkROMVersion(). Its layout is "VVVVRTYYYYMMDD": four version digits, then
 * the region letter, then the console type, then the build date. For
 * "0160EC20011004" that gives 'E' (Europe).
 *
 * NVRAM is still preferred when it holds a plausible letter, since it reflects
 * the actual installed drive; ROMVER is the fallback.
 *
 * @returns the region letter, or 0 when neither source yields a plausible one.
 */
static char detect_region_letter(void)
{
	const u8 *nvm;
	char region;

	nvm = get_nvram();
	if (nvm_errors == 0) {
		region = (char) nvm[NVM_REAL_REGION];
		if ((region >= 'A') && (region <= 'Z')) {
			return region;
		}
	}

	if (strlen(ps2_rom_version) > 4) {
		region = ps2_rom_version[4];
		if ((region >= 'A') && (region <= 'Z')) {
			kprintf("NVRAM region unusable, using ROMVER region '%c' from \"%s\".\n",
				region, ps2_rom_version);
			return region;
		}
	}

	return 0;
}

int loadLoaderModules(int debug_mode, int disable_cdrom)
{
	static int load_dvd_config = -1;
	static int load_netsurf_config = -1;
	static int load_usb_config = -1;
	int i;
	int rv;
	int lrv = -1;

	if (debug_mode == 1) {
		/* Network is used by ps2link and can't be used by Linux. */
		network_support = 0;
	}

#ifdef RESET_IOP
	if (debug_mode == -1) {
		graphic_setStatusMessage("Flushing cache");
		FlushCache(0);
		graphic_setStatusMessage("Exit IOP Heap");
		SifExitIopHeap();
		graphic_setStatusMessage("Exit LoadFile");
		SifLoadFileExit();
		graphic_setStatusMessage("Exit FIO");
		fioExit();
		graphic_setStatusMessage("Exit RPC");
		SifExitRpc();
		graphic_setStatusMessage("Stop DMA");
		SifStopDma();
		graphic_setStatusMessage("PreReset Init RPC");
		SifInitRpc(0);
		graphic_setStatusMessage("Reseting IOP");
		while(!SifIopReset(s_pUDNL, 0));

		graphic_setStatusMessage("IOP Sync");
		while (!SifIopSync());

		graphic_setStatusMessage("Initialize RPC");
		SifInitRpc(0);
	}
#endif

	graphic_setStatusMessage("Patching enable LMB");
	sbv_patch_enable_lmb();
	graphic_setStatusMessage("Patching disable prefix check");
	sbv_patch_disable_prefix_check();

	/* CDVDMAN is loaded by IopReset and NVRAM can be read. */
	graphic_setStatusMessage("Read NVRAM from CDVD");

	if (!disable_cdrom) {
		nvram_init();
	}

	eromdrvSupport = 0;

	/* FIXME: eedebug handler seems to crash the ee
	graphic_setStatusMessage("Add eedebug handler");
	addEEDebugHandler();
	*/

	graphic_setStatusMessage("Loading modules");
	for (i = 0; i < moduleLoaderNumberOfModules; i++) {
		const rom_entry_t *romfile;

		if (moduleList[i].debug_mode != 0) {
			if (moduleList[i].debug_mode != debug_mode) {
				continue;
			}
		}

		/* Load configuration when necessary modules are loaded. */
		if (moduleList[i].loadCfg) {
			checkForMusicSupport();

			setDefaultConfiguration(NULL);

			/* Search both memory cards, mc0 first.
			 *
			 * This is the change the whole port exists for. kernelloader
			 * already had a startup fallback chain -- mc0, then mass0:PS2NS,
			 * then mass0, then cdfs -- but mc1 was simply absent from it, even
			 * though CONFIG_DIR2 has always been defined. With FreeMcBoot
			 * occupying mc0 and everything Linux on mc1, the config was never
			 * found automatically, and the only way to reach it was Advanced
			 * Menu -> File Menu -> Select Config File on every single boot.
			 *
			 * mc1 is readable at exactly this point: MCMAN and MCSERV are
			 * loaded two entries earlier in moduleList[] and serve both slots,
			 * which is why this needs no extra module loading or reordering.
			 *
			 * mc0 keeps priority, so an existing mc0 setup behaves as before. */
			lrv = loadConfiguration(CONFIG_FILE);
			if (lrv != 0) {
				kprintf("No config at \"%s\", trying \"%s\".\n",
					CONFIG_FILE, CONFIG_FILE2);
				lrv = loadConfiguration(CONFIG_FILE2);
				if (lrv == 0) {
					kprintf("Loaded configuration from \"%s\".\n", CONFIG_FILE2);
				}
			}
			if (lrv != 0) {
				/* Development fallback: host: is served by ps2client and by
				 * PCSX2 (relative to the loaded ELF), so a config can be
				 * tested with no memory card or USB stick present. Opening it
				 * simply fails on a real console. */
				lrv = loadConfiguration(HOST_CONFIG_FILE);
				if (lrv == 0) {
					kprintf("Loaded configuration from \"%s\".\n", HOST_CONFIG_FILE);
				}
			}

			changeMode();

			/* Load configuration on startup and not on IOP reset. */
			moduleList[i].loadCfg = 0;

			/* Earliest point loaderConfig.autoBootTime can be known for this
			 * (normal) build -- config.txt is read here, in this loop, and
			 * not before. On real hardware even the host: fallback is only
			 * reached after the mc0:/mc1: attempts above it, which need
			 * MCMAN/MCSERV (loaded two entries up moduleList[]) to be
			 * meaningful at all. INSTANT_BOOT_DEFAULT builds (main.cpp) skip
			 * this dependency entirely by not needing config.txt in the
			 * first place; this is the fallback for a normal kloader.elf
			 * whose config.txt sets AutoBootTime=-1 (kload's -kload-instant
			 * always writes it, belt-and-suspenders, even though the
			 * instant-build variant does not need it). */
			if (loaderConfig.autoBootTime < 0 && !bootlogActive()) {
				bootlogBegin();
				loaderConfig.instantBoot = 1;
			}
		}
		graphic_setStatusMessage(moduleList[i].path);
		kprintf("Loading module (%s)\n", moduleList[i].path);

		if (!network_support) {
			if (moduleList[i].network) {
				continue;
			}
		}

		if (moduleList[i].ps2smap) {
			moduleList[i].args = getPS2MAPParameter(&moduleList[i].argLen);
		}
		if (moduleList[i].dns) {
			moduleList[i].args = getPS2DNS(&moduleList[i].argLen);
		}
		if (moduleList[i].checkMc) {
			static char file[256];

			/* Try to load module from MC if available. */
			snprintf(file, sizeof(file), CONFIG_DIR "/%s", moduleList[i].path);
			rv = SifLoadModule(file, moduleList[i].argLen, moduleList[i].args);
		} else {
			rv = -1;
		}
		if (rv < 0) {
			if ((moduleList[i].sms_mod == 0) || (isDVDVSupported())) {
				if (moduleList[i].eromdrv < 0) {
					/* Try to detect EROM driver only the first time. */
					moduleList[i].eromdrv = 1;
					if (disable_cdrom) {
						continue;
					}

					rv = open("rom1:EROMDRV", O_RDONLY);
					if (rv >=0 ) {
						eromdrvpath[12] = 0;

						/* This is an old fat PS2 (working with SCPH-50004 and SCPH-39004). */
						close(rv);
					} else {
						char region;

						/* Prefer NVRAM, fall back to ROMVER -- see
						 * detect_region_letter(). */
						region = detect_region_letter();
						if (region == 0) {
							const u8 *nvm = get_nvram();

							/* Neither source knew. Note the original format
							 * string here had six conversions but only five
							 * arguments, so the trailing "(%s)" printed
							 * garbage; ps2_region_type is what it wanted. */
							kprintf("Cannot determine region: NVRAM S%02x T%02x F%02x R%02x, "
								"ROMVER \"%s\" (%s), %d NVM errors.\n",
								nvm[0x180],
								nvm[0x181],
								nvm[NVM_FAKE_REGION],
								nvm[NVM_REAL_REGION],
								ps2_rom_version,
								ps2_region_type,
								nvm_errors);
							continue;
						}

						eromdrvpath[12] = region;
						rv = open(eromdrvpath, O_RDONLY);
						if (rv >= 0) {
							/* Region code seems to be correct. */
							close(rv);
						} else {
							/* No EROM driver for this region. That is normal
							 * on consoles without DVD-Video support, and on
							 * emulators that provide no rom1 at all -- it only
							 * means no DVD-Video playback, so it must not
							 * block startup with an error screen the user
							 * cannot act on. Booting Linux does not need it. */
							kprintf("No EROM driver at \"%s\" (region '%c', ROMVER \"%s\"). "
								"DVD-Video support unavailable.\n",
								eromdrvpath, region, ps2_rom_version);
							continue;
						}
					}
				}
				if (moduleList[i].eromdrv != 0) {
					moduleList[i].args = get_eromdrvpath();
					moduleList[i].argLen = strlen(moduleList[i].args) + 1;
				}
				romfile = rom_getFile(moduleList[i].path);
				if (romfile != NULL) {
					int ret;

					ret = SifExecModuleBuffer((void *) romfile->start, romfile->size, moduleList[i].argLen, moduleList[i].args, &rv);
					if (ret < 0) {
						rv = ret;
					}
				} else {
					rv = SifLoadModule(moduleList[i].path, moduleList[i].argLen, moduleList[i].args);
				}
				if (rv < 0) {
					if (moduleList[i].eromdrv != 0) {
						kprintf("Failed to load module \"%s\".\n", get_eromdrvpath());
					} else {
						kprintf("Failed to load module \"%s\".\n", moduleList[i].path);
					}
					if (moduleList[i].ps2smap && !isSlimPSTwo()) {
						network_support = 0;
					} else {
						if (moduleList[i].eromdrv != 0) {
							/* The EROM driver only provides DVD-Video playback.
							 * Failing to load it must not raise a blocking
							 * error screen: it is not needed to boot Linux, and
							 * there is nothing the user can do about it.
							 *
							 * It fails readily in practice. When rom1 is present
							 * the code above concludes "old fat PS2" and strips
							 * the region letter to plain "rom1:EROMDRV", which
							 * opens but does not load on every BIOS. When rom1
							 * is absent the region path is used instead. Either
							 * way the outcome is the same: no DVD-Video.
							 *
							 * kprintf above already recorded it. */
							kprintf("DVD-Video support unavailable.\n");
						} else if (moduleList[i].optional) {
							/* Not in this ROM and allowed not to be. Logged by
							 * the kprintf above; must not queue an error, or
							 * the Buffer check stage waits on a pad press. */
							kprintf("Optional module \"%s\" unavailable, continuing.\n",
								moduleList[i].path);
						} else {
							error_printf("Failed to load module \"%s\".", moduleList[i].path);
						}
					}
				} else {
					if (moduleList[i].eromdrv != 0) {
						eromdrvSupport = -1;
					}
				}
			}
		}
	}
	graphic_setStatusMessage(NULL);
	printAllModules();

	fileXioInit();

	if (load_netsurf_config) {
		load_netsurf_config = 0;

		if (lrv != 0) {
			graphic_setStatusMessage("Check for NetSurf config");

			lrv = loadConfiguration(PS2NS_CONFIG_FILE);

			graphic_setStatusMessage(NULL);
		}
	}

	if (load_usb_config) {
		load_usb_config = 0;

		if (lrv != 0) {
			graphic_setStatusMessage("Check for USB config");

			lrv = loadConfiguration(USB_CONFIG_FILE);

			graphic_setStatusMessage(NULL);
		}
	}

	if (load_dvd_config && isDVDVSupported()) {
		load_dvd_config = 0;

		graphic_setStatusMessage("Init DVD driver");

		CDDA_Init();
		CDVD_Init();

		if (lrv != 0) {
			DiskType type;

			graphic_setStatusMessage("Load config from DVD");

			type = CDDA_DiskType();

			if (type == DiskType_DVDV) {
				CDVD_SetDVDV(1);
			} else {
				CDVD_SetDVDV(0);
			}

			kprintf("kloader disc type %u\n", type);
			switch (type) {
			case DiskType_CD:
			case DiskType_DVD:
			case DiskType_DVDV:
				/* Load configuration from disc. */
				lrv = loadConfiguration(DVD_CONFIG_FILE);

				changeMode();
#if 0
				if (lrv != 0) {
					error_printf("Failed to load config from \"%s\", using default configuration.", DVD_CONFIG_FILE);
				}
#endif
				break;
			default:
				kprintf("kloader unsupported disc type %u\n", type);
				break;
			}

			/* Stop CD when finished. */
			CDVD_Stop();
			CDVD_FlushCache();
		}
		graphic_setStatusMessage(NULL);
	}

	/* Last resort: the configuration embedded in this ELF as a ROM file.
	 *
	 * Every real source above -- mc0:, mc1:, host:, mass0: and cdfs: -- has
	 * failed by this point, so a card, stick or disc config always takes
	 * precedence and this changes nothing for a normal setup. It matters for a
	 * console with no config at all, and for emulators, where there may be no
	 * writable device the guest can see. See loader/defaultconfig.txt. */
	if (lrv != 0) {
		lrv = loadConfigurationFromRom(ROM_CONFIG_FILE);
		if (lrv == 0) {
			kprintf("Loaded built-in configuration \"%s\".\n", ROM_CONFIG_FILE);
			changeMode();
		}
	}

	snprintf(hardware_information, sizeof(hardware_information),
		"%s with DVD-R %s, %s sound support and %s network adapter",
		isSlimPSTwo() ? "slim PSTwo" : "fat PS2",
		disable_cdrom ? "disabled" : (isDVDVSupported() ? "support" : "problem"),
		(libsd_version <= 0x104) ? "direct" : "indirect",
		network_support ? "with" : "without");

	return 0;
}

const char *get_eromdrvpath(void)
{
	return eromdrvpath;
}

