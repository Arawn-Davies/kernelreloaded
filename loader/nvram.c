#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libcdvd.h>
#include <fileio.h>

#include "nvram.h"
#include "modules.h"
#include "memory.h"
#include "kprint.h"
#include "loader.h"

typedef struct {
	int version;
	u32 fake_region;
	u32 real_region;
	u32 console_type;
} nvm_offsets_t;

/* NVM offsets. */
nvm_offsets_t nvmOff[] = {
	{
		.version = 0x000,
		.fake_region = 0x185,
		.real_region = 0x186,
		.console_type = 0x1a0,
	},
	{
		.version = 0x170,
		.fake_region = 0x185,
		.real_region = 0x186,
		.console_type = 0x1b0,
	},
};

/** Store copy of DVD internal NVRAM. */
static u8 nvm[0x400];

char ps2_console_type[32] = "CDVD error";

/** Console generation in thousands: 10, 15, 18, 30, 35, 37, 39, 50 (fat), then
 * 70, 75, 77, 79, 90 (slim). Zero is never returned.
 *
 * Taken from the NVRAM model string where there is one, because that names the
 * actual console: an SCPH-77004 is a 77K whatever its ROM says. Every generation
 * is simply the model number's thousands, so no table is needed and models this
 * code has never heard of still come out right.
 *
 * Under PCSX2, and on any console whose NVRAM is blank, that string is not a
 * model number at all, so fall back to the ROM version. That is coarser, and
 * one step of it is a genuine guess: ROM 2.20 shipped on the 770XX, the 790XX
 * AND early 900XX units, so 2.20 is reported as 77K and may be any of the
 * three. Only 2.30 (late 900XX) and 2.00 (700XX) are unambiguous. NVRAM wins
 * whenever it has an answer, and on real hardware it always does. */
static unsigned int modelGeneration(void)
{
	const char *p = strstr(ps2_console_type, "SCPH-");
	unsigned int n = 0;
	unsigned int ver;

	if (p != NULL) {
		p += 5;
		while ((*p >= '0') && (*p <= '9') && (n < 100000)) {
			n = (n * 10) + (unsigned int) (*p - '0');
			p++;
		}
	}
	if (n >= 10000) {
		return n / 1000;
	}

	/* getBiosVersion(), NOT strtoul(ps2_rom_version, ...).
	 *
	 * ROMVER is "0120EC20010111" -- a four-digit version followed by a region
	 * letter, a type letter and a date. Handing the whole string to strtoul
	 * base 16 does not stop at the version, because E and C are perfectly good
	 * hex digits: it consumes all fourteen characters, overflows, and saturates
	 * at ULONG_MAX -- which sails past every threshold below and reports a
	 * v1.20 SCPH-30003 as a slim 90K.
	 *
	 * It only looked correct on the 10K, whose ROMVER is "0100JC2000..." -- 'J'
	 * is not a hex digit, so parsing stopped at 0100 by luck rather than logic.
	 *
	 * checkROMVersion() already does this properly, terminating the string at
	 * four characters before converting, so ask it instead of re-deriving. */
	ver = (unsigned int) getBiosVersion();

	/* Mapped from the ROM each generation actually shipped with:
	 *
	 *   1.00  V1   10000 / 15000     2.00  V12  700XX
	 *   1.01  V2   18000             2.20  V14  750XX
	 *   1.20  V3   30003             2.20  V15  770XX
	 *   1.50  V4   30000             2.20  ...  790XX, early 900XX
	 *   1.60  V6   30004R, 3500X     2.30  V17+ late 900XX
	 *   1.60  V7   390XX
	 *   1.70  Vx   50000
	 *   1.90  V9   50004
	 *
	 * Two steps are unavoidably lossy, because the ROM genuinely does not
	 * distinguish them: 1.60 covers both the 30004R and the 390XX, and 2.20
	 * covers the 750XX, 770XX, 790XX and early 900XX alike. Each reports the
	 * earliest member of its group. NVRAM settles it exactly on real hardware;
	 * this path only runs when there is no model string to read. */
	if (ver >= 0x0230) return 90;
	if (ver >= 0x0220) return 75;
	if (ver >= 0x0200) return 70;
	if (ver >= 0x0170) return 50;
	if (ver >= 0x0120) return 30;
	if (ver >= 0x0101) return 18;
	return 10;
}

const char *getModelFamily(void)
{
	static char family[8];

	snprintf(family, sizeof(family), "%uK", modelGeneration());
	return family;
}

int isSlimModel(void)
{
	/* Chassis is not an independent fact -- it follows from the generation.
	 * The slim arrived with the 700XX and everything after it is slim, so
	 * anything from 70 up is slim and 500XX and below is fat. Deriving it here
	 * rather than reading it separately is what makes "fat 90K" unrepresentable
	 * instead of merely unlikely.
	 *
	 * Kept apart from isSlimPSTwo(), which answers the ROM-version question the
	 * IOP module selection needs. This one is for what the console IS. */
	return modelGeneration() >= 70;
}
char ps2_region_type[32] = "CDVD error";
int nvm_errors = -1;

void nvram_init(void)
{
	int rv;
	u32 addr;
	u16 data;
	u8 stat;
	int version;
	unsigned int type;
	nvm_offsets_t *off = &nvmOff[0];

	if (nvm_errors >= 0) {
		/* NVRAM was already loaded. */
		return;
	}

	version = getBiosVersion();
	for (type = 0; type < (sizeof(nvmOff)/sizeof(nvmOff[0])); type++) {
		if (nvmOff[type].version <= version) {
			off = &nvmOff[type];
		}
	}

	rv = sceCdInit(SCECdINoD);
	if (rv != 1) {
		kprintf("Error: sceCdInit(SCECdINoD) failed\n");
		return;
	}

	nvm_errors = 0;
	memset(nvm, 0, sizeof(nvm));
	for (addr = 0; addr < sizeof(nvm)/2; addr++) {
		rv = sceCdReadNVM(addr, &data, &stat);
		if (rv != 1) {
			kprintf("sceCdReadNVM Error: rv = %d, addr = 0x%04x data = 0x%04x, stat = 0x%02x\n", rv, 2 * addr, data, stat);
			nvm_errors++;
		} else {
			nvm[addr * 2] = data & 0xFF;
			nvm[addr * 2 + 1] = (data >> 8) & 0xFF;
		}
	}
	rv = sceCdInit(SCECdEXIT);
	if (rv != 1) {
		kprintf("Error: sceCdInit(SCECdEXIT) failed\n");
	}

	memcpy(ps2_console_type, &nvm[off->console_type], sizeof(ps2_console_type));
	ps2_console_type[sizeof(ps2_console_type) - 1] = 0;
	kprintf("PS2 Console type: %s\n", ps2_console_type);

	/* PCSX2 leaves this region of NVRAM blank, so the field is simply empty
	 * under emulation -- and an empty Model line looks like a bug rather than
	 * like missing data. Infer a plausible SCPH number from ROMVER instead,
	 * which is present either way:
	 *
	 *   version  <  0x0150   the earliest fat consoles
	 *            <  0x0190   fat, the SCPH-3xxxx / 5xxxx era
	 *            >= 0x0190   slim PSTwo, SCPH-7xxxx and later
	 *
	 * and the region letter at ROMVER[4] is the last digit of the number:
	 * J=0 Japan, A=1 USA, E=3 Europe, C=6 China. So a v1.60 European ROM
	 * infers SCPH-30003, and a slim European one SCPH-70003.
	 *
	 * Inferred, not read -- the console it names may not be the exact one. The
	 * Model config key overrides this outright. */
	if ((unsigned char) ps2_console_type[0] < ' ') {
		/* getBiosVersion(), for the reason given on modelGeneration() above:
		 * strtoul() over the whole ROMVER string swallows the region and date
		 * as hex and saturates, so every all-hex ROMVER inferred "700" here.
		 * Masked under PCSX2, where the result is replaced by "PCSX2" a few
		 * lines below, but wrong on a console whose NVRAM is blank. */
		unsigned int ver = (unsigned int) getBiosVersion();
		int hostfd;
		char region = (strlen(ps2_rom_version) > 4) ? ps2_rom_version[4] : 0;
		int digit;
		const char *family;

		switch (region) {
			case 'J': digit = 0; break;
			case 'A': digit = 1; break;
			case 'E': digit = 3; break;
			case 'H': digit = 4; break;
			case 'K': digit = 5; break;
			case 'C': digit = 6; break;
			default:  digit = 0; break;
		}
		if (ver >= 0x0190) {
			family = "700";
		} else if (ver >= 0x0150) {
			family = "300";
		} else {
			family = "100";
		}
		snprintf(ps2_console_type, sizeof(ps2_console_type),
			"SCPH-%s0%d", family, digit);

		/* Blank NVRAM already suggests emulation, since every real console has
		 * a model string there. Corroborate it with host:, which PCSX2 provides
		 * (mapped to the directory the ELF came from) and a console does not.
		 *
		 * ps2link also offers host:, which would otherwise be a false positive,
		 * but that path sets debug_mode -- so require its absence. Naming the
		 * emulator outright is more use than inferring a console that is not
		 * really there. */
		hostfd = fioDopen("host:");
		if (hostfd >= 0) {
			fioDclose(hostfd);
			if (debug_mode != 1) {
				snprintf(ps2_console_type, sizeof(ps2_console_type), "PCSX2");
			}
		}

		kprintf("PS2 Console type: NVRAM blank, reporting %s (ROMVER %s)\n",
			ps2_console_type, ps2_rom_version);
	}
	kprintf("PS2 Chassis: %s %s\n", isSlimModel() ? "slim" : "fat", getModelFamily());

	snprintf(ps2_region_type, sizeof(ps2_region_type), "S%02x T%02x F%02x R%02x (%d NVM errors)", nvm[0x180], nvm[0x181], nvm[off->fake_region], nvm[off->real_region], nvm_errors);
}

u8 *get_nvram(void)
{
	return nvm;
}
