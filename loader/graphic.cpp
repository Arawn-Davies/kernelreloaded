/* Copyright (c) 2007 - 2009 Mega Man */
#include "gsKit.h"
#include "dmaKit.h"
#include "malloc.h"
#include "stdio.h"
#include "kernel.h"
#include "sio.h"

#include "config.h"
#include "menu.h"
#include "rom.h"
#include "graphic.h"
#include "loader.h"
#include "configuration.h"
#include "kprint.h"
#include "nvram.h"
#include "bootlog.h"
#include "font.h"
#include "modules.h"
#include "ps2dev9.h"
#include "loadermenu.h"
#include <string.h>
#include <screenshot.h>


/** Maximum buffer size for error_printf(). */
#define MAX_BUFFER 128

/** Maximum buffer size of info message buffer. */
#define MAX_INFO_BUFFER 4096

/** Maximum number of error messages. */
#define MAX_MESSAGES 10

/** Maximum texture size in Bytes. */
#define MAX_TEX_SIZE 0x30000

/** Size of small textures (will be uploaded). */
#define SMALL_TEXT_SIZE 0x4000

/** GS_MODE_VGA_1280_75 is not used, use it as special value for autodetect. */
#define GS_AUTO_DETECT GS_MODE_VGA_1280_75

/** True, if graphic is initialized. */
static bool graphicInitialized = false;

static Menu *menu = NULL;

static Menu *mainMenu = NULL;

static bool enableDisc = 0;

/** gsGlobal is required for all painting functiions of gsKit. */
static GSGLOBAL *gsGlobal = NULL;

/** Colours used for painting. */
static u64 White, Black, Blue, Red;

static void paintBootLog(void);

/** Text colour. */
static u64 TexCol;

/** Red text colour. */
static u64 TexRed;

/** Heading colour inside the System Info panel, matching the title lockup. */
static u64 TexPanelHead;

/** Colour for the status/hint text along the bottom of the screen.
 *
 * This was black, which worked while the background was a flat blue fill. The
 * background is now a full-screen starfield texture, so black text over it is
 * effectively invisible. White reads against the dark sky and the mountains
 * under both of the positions these strings use (left edge and right edge);
 * the bright horizon glow sits between them. Renamed from TexBlack because the
 * old name no longer describes it. */
static u64 TexInfo;

/** Font used for printing text. */
static GSFONTM *gsFont;

GSFONTM *getGsFont(void)
{
	return gsFont;
}

/** File name that is printed on screen. */
static char loadName[26];

static const char *statusMessage = NULL;

/** Percentage for loading file shown as progress bar. */
static int loadPercentage = 0;

/* The window of the overall bar that the current stage occupies.
 *
 * Every loader reports 0..100 for the file it is reading, so the bar used to
 * restart for each one -- modules, then SBIOS, then the kernel, then the initrd,
 * four sweeps that say nothing about how far the boot as a whole has got. The
 * stage maps that per-file percentage into a slice of the bar instead, so it
 * fills once from empty to full across the whole boot. Callers are unchanged;
 * only loader.c needs to say which slice it is in. */
static int stageBase = 0;
static int stageSpan = 100;

/** Scale factor for font.
 *
 * Drives the title, status line and error text. Reduced from 1.0 -- the BIOS
 * ROM font is chunky at full size and the extra headroom keeps long status
 * strings on one line. menuEntry.cpp carries its own scale for menu items and
 * was reduced by the same proportion. */
static float scale = 0.70f;

/** Ring buffer with error messages. */
static const char *errorMessage[MAX_MESSAGES] = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

/** Read pointer into ring buffer of error messages. */
static int readMsgPos = 0;

/** Write pointer into ring buffer of error messages. */
static int writeMsgPos = 0;

static GSTEXTURE *texFolder = NULL;

static GSTEXTURE *texUp = NULL;

static GSTEXTURE *texBack = NULL;

static GSTEXTURE *texSelected = NULL;

static GSTEXTURE *texUnselected = NULL;

static GSTEXTURE *texPenguin = NULL;


static GSTEXTURE *texStarfield = NULL;

/** Title lockup: "KernelReloaded" wordmark plus its subtitle, pre-rendered
 * as artwork rather than composed from the BIOS ROM font. */
static GSTEXTURE *texTitle = NULL;

/** Bottom button bar: the X/triangle/circle glyphs and their labels. */
static GSTEXTURE *texBottomBar = NULL;

/** Background panel behind the System Info block. */
static GSTEXTURE *texPanel = NULL;

/** Rounded highlight behind the selected menu entry. */
static GSTEXTURE *texHighlight = NULL;

/** Loading bar: empty track, and the fill drawn over it. */
static GSTEXTURE *texBarTrack = NULL;
static GSTEXTURE *texBarFill = NULL;

/** Pixels reserved at the bottom of the display.
 *
 * Everything anchored to the bottom -- the contextual hint lines, the mode and
 * build-state block, and the maxY passed to printTextBlock() -- is measured up
 * from Height minus this. Raised from 42 to clear the 34px button bar that now
 * occupies the bottom edge, which the hints would otherwise be drawn over. */
static int reservedEndOfDisplayY = 72;

/** Baseline for the status / filename line.
 *
 * The title lockup is drawn at y=24 and is 74px tall, so it occupies down to
 * y=98. The old value of 90 put the status text INSIDE it -- "Copying files and
 * start..." printed across "PlayStation 2 Linux Loader". Sits below the lockup
 * now, where a menu heading would be, so it reads as the current activity. */
static const int STATUS_LINE_Y = 108;

/** Top of the loading bar.
 *
 * Clears the status line above it rather than sitting under its descenders,
 * which is what a bar at 120 did to "host:initrd_clean.gz". */
static const int BAR_Y = 138;

/** Width of the loading bar.
 *
 * Matches the column printTextBlock wraps to, so the bar, the status line and
 * any wrapped text share one left and right edge. The artwork is 260px and is
 * stretched to fit. */
static const int BAR_W = 540;

/* During the load the bar moves to the bottom centre, macOS-style, so it does
 * not sit across the boot log. Narrower than BAR_W because it is centred and
 * has nothing to line up with on the left. */
static const int LOAD_BAR_W = 400;
static const int LOAD_BAR_Y = 380;

static bool usePad = false;

static int emulatedKey = ' ';

int scrollPos = 0;

int inputScrollPos = 0;

u32 modeList[] = {
	GS_AUTO_DETECT,
	GS_MODE_VGA_640_60,
	GS_MODE_VGA_640_72,
	GS_MODE_VGA_640_75,
	GS_MODE_VGA_640_85,
	GS_MODE_DTV_480P,
	GS_MODE_NTSC,
	GS_MODE_PAL,
	GS_MODE_DTV_576P,
	GS_MODE_DTV_720P,
	GS_MODE_DTV_1080I,
};
int currentMode = 0;
int lastMode = 0;

int frequenzy[] = {
	0,
	60,
	72,
	75,
	85,
	60,
	60,
	50,
	50,	/* 576P */
	60,	/* 720P */
	60,	/* 1080I */
};

const char *modeDescription[] = {
	"Auto",
	"640x480 60Hz",
	"640x480 72Hz",
	"640x480 75Hz",
	"640x480 85Hz",
	"480P",
	"NTSC",
	"PAL",
	"576P",
	"720P",
	"1080I",
};

extern "C" {
	int xoffset = 0;
	int yoffset = 0;
}

/** Apply the selected entry of modeList[] to a GSGLOBAL.
 *
 * Setting gsGlobal->Mode alone is not enough. gsKit_init_global() fills in
 * Width, Height, Interlace and Field for the mode it detected -- PAL or NTSC --
 * and gsKit_init_screen() then builds the display from those, not from Mode.
 * Overwriting Mode by itself therefore left every mode running at the detected
 * geometry: kernelloader reported "Set screen mode to 640x512" whichever entry
 * was chosen, and the mismatch between the requested mode and the PAL-sized
 * framebuffer showed as a black screen on several of them.
 *
 * The four fields have to move together, which is what ps2oom's
 * doomgeneric_ps2_gs.c does when it selects a mode explicitly.
 *
 * GS_AUTO_DETECT resolves through gsKit_detect_signal(), which only ever
 * answers NTSC or PAL.
 */
static void applyVideoMode(GSGLOBAL *g, int modeIndex)
{
	u32 mode;

	if (modeList[modeIndex] == GS_AUTO_DETECT) {
		mode = gsKit_detect_signal();
	} else {
		mode = modeList[modeIndex];
	}

	g->Mode = mode;

	switch (mode) {
	case GS_MODE_PAL:
		g->Interlace = GS_INTERLACED;
		g->Field = GS_FIELD;
		g->Width = 640;
		g->Height = 512;
		break;
	case GS_MODE_NTSC:
		g->Interlace = GS_INTERLACED;
		g->Field = GS_FIELD;
		g->Width = 640;
		g->Height = 448;
		break;
	case GS_MODE_DTV_576P:
		g->Interlace = GS_NONINTERLACED;
		g->Field = GS_FRAME;
		g->Width = 640;
		g->Height = 512;
		break;
	case GS_MODE_DTV_720P:
		/* 1280x720 will not fit VRAM at 32bpp: two buffers alone would be
		 * 7.4 MB of the 4 MB available. CT16 halves that to 3.7 MB, which
		 * fits but leaves very little for textures. */
		g->Interlace = GS_NONINTERLACED;
		g->Field = GS_FRAME;
		g->Width = 1280;
		g->Height = 720;
		g->PSM = GS_PSM_CT16;
		break;
	case GS_MODE_DTV_1080I:
		/* Even at CT16 a 1280x1080 pair does not fit, so this mode runs
		 * single-buffered. Expect tearing; it is the only way it fits. */
		g->Interlace = GS_INTERLACED;
		g->Field = GS_FIELD;
		g->Width = 1280;
		g->Height = 1080;
		g->PSM = GS_PSM_CT16;
		g->DoubleBuffering = GS_SETTING_OFF;
		break;
	default:
		/* The VGA modes and DTV 480P are all progressive 640x480. */
		g->Interlace = GS_NONINTERLACED;
		g->Field = GS_FRAME;
		g->Width = 640;
		g->Height = 480;
		break;
	}

	/* PSM and DoubleBuffering are only overridden above, never restored:
	 * changeMode() builds a fresh GSGLOBAL with gsKit_init_global() every time,
	 * so each call starts from gsKit's defaults and the HD settings cannot leak
	 * into a subsequent SD mode. */
}

void check_screen_offsets(void)
{
	kprintf("Set screen mode to %ux%u\n", gsGlobal->Width, gsGlobal->Height);
	if (gsGlobal->Width > 640) {
		xoffset = (gsGlobal->Width - 640) / 2;
	} else {
		xoffset = 0;
	}
}

/** Draw a texture.
 *
 * Rewritten for modern gsKit, which gained a texture manager
 * (gsKit_TexManager_*) after this code was written. The original did its own
 * VRAM management: gsKit_vram_alloc() a permanent slot for small textures and
 * upload once, or share one globalVram scratch buffer for large ones and
 * re-upload them inline in height-sliced chunks on every frame.
 *
 * That workflow no longer draws anything. gsKit now expects a texture to be
 * bound through gsKit_TexManager_bind() before use; the manager owns VRAM,
 * decides what to upload and evicts under pressure. A caller-populated
 * Texture->Vram is not what it consults, so the textures were silently never
 * present when the sprite was drawn -- no error, no VRAM failure, and
 * independent of pixel format or texture size, which is exactly what we saw.
 *
 * The reference for this is ps2oom's doomgeneric_ps2_gs.c, which drives the
 * same gsKit on the same toolchain: init the manager once, then bind before
 * each draw and call gsKit_TexManager_nextFrame() after the flip.
 *
 * The slicing machinery is gone with it. The manager handles textures too
 * large to keep resident, so the 731x512 starfield no longer needs manual
 * chunking -- which also retires the buggy gsKit_texture_upload_inline()
 * helper whose lastMem/lastVram cache was never actually assigned.
 */
/* Same draw with the texture modulated by a brightness level.
 *
 * gsKit multiplies the texture by the vertex colour, where 0x80 is 1.0 -- so
 * the scale runs past unmodified: 0x40 is half, 0xC0 is one and a half. Both
 * ends are used by the button bar, which reads washed out at 1.0 against the
 * starfield and should be plainly dead during a load. */
#define SHADE_DIM    0x40
#define SHADE_BRIGHT 0xC0

static void paintTextureShaded(GSTEXTURE *tex, int x, int y, int z, u8 level)
{
	if (tex == NULL) {
		return;
	}

	gsKit_TexManager_bind(gsGlobal, tex);

	gsKit_prim_sprite_texture(gsGlobal, tex,
		x, y, 0, 0, x + tex->Width, y + tex->Height,
		tex->Width, tex->Height, z,
		GS_SETREG_RGBAQ(level, level, level, 0x80, 0x00));
}

void paintTexture(GSTEXTURE *tex, int x, int y, int z)
{
	if (tex == NULL) {
		return;
	}

	gsKit_TexManager_bind(gsGlobal, tex);

	gsKit_prim_sprite_texture(gsGlobal, tex,
		x, y, 0, 0, x + tex->Width, y + tex->Height,
		tex->Width, tex->Height, z,
		GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00));
}

/** Draw the left `width` pixels of a texture.
 *
 * gsKit_prim_sprite_texture takes UV coordinates, so clipping the source to a
 * sub-rectangle is free -- no second texture and no stretching. Used for the
 * loading bar fill, which is one texture revealed progressively. */
static void paintTexturePartial(GSTEXTURE *tex, int x, int y, int z, int width)
{
	if ((tex == NULL) || (width <= 0)) {
		return;
	}
	if (width > (int) tex->Width) {
		width = tex->Width;
	}

	gsKit_TexManager_bind(gsGlobal, tex);
	gsKit_prim_sprite_texture(gsGlobal, tex,
		x, y, 0, 0, x + width, y + tex->Height,
		width, tex->Height, z,
		GS_SETREG_RGBAQ(0x80,0x80,0x80,0x80,0x00));
}

/** Draw a texture stretched to a given width.
 *
 * The bar artwork is 260px wide and was drawn at native size, which is under
 * half the usable width of the screen. Stretching is free -- the GS scales
 * while sampling, and gsKit_prim_sprite_texture already takes independent
 * source and destination rectangles. */
static void paintTextureStretched(GSTEXTURE *tex, int x, int y, int z, int width)
{
	if (tex == NULL) {
		return;
	}
	gsKit_TexManager_bind(gsGlobal, tex);
	gsKit_prim_sprite_texture(gsGlobal, tex,
		x, y, 0, 0,
		x + width, y + tex->Height,
		tex->Width, tex->Height, z,
		GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

/** As above, revealed to a percentage.
 *
 * Source and destination are clipped by the SAME fraction, so the fill's
 * gradient stays in step with the track instead of being squashed into
 * whatever is revealed. */
static void paintTextureStretchedPartial(GSTEXTURE *tex, int x, int y, int z, int width, int percent)
{
	int dw;
	int sw;

	if ((tex == NULL) || (percent <= 0)) {
		return;
	}
	if (percent > 100) {
		percent = 100;
	}
	dw = (width * percent) / 100;
	sw = ((int) tex->Width * percent) / 100;
	if (sw <= 0) {
		return;
	}
	gsKit_TexManager_bind(gsGlobal, tex);
	gsKit_prim_sprite_texture(gsGlobal, tex,
		x, y, 0, 0,
		x + dw, y + tex->Height,
		sw, tex->Height, z,
		GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00));
}

static char infoBuffer[MAX_INFO_BUFFER];
static int infoBufferPos = 0;

static char *inputBuffer = NULL;
static int writeable = 0;
static int cursor_counter = 0;
static int cursorpos = 0;

int printTextBlock(int x, int y, int z, int maxWidth, int maxY, const char *msg, int scrollPos, int cursorpos, int cursor)
{
	/* Wraps on MEASURED width rather than a character count.
	 *
	 * The old signature took maxCharsPerLine and every caller passed 26, which
	 * is the only honest answer when the font is fixed-pitch and unmeasurable:
	 * 26 of the widest character had to fit. With a proportional face that
	 * wastes most of a line on ordinary text and still overruns on a line full
	 * of capitals. Now a line is filled until the next glyph would cross
	 * maxWidth, so it uses the room it actually has. */
	char lineBuffer[256];
	int pos;
	int lineNo;
	const int lineStep = fontLineHeight(FONT_BODY) + 6;

	pos = 0;
	lineNo = 0;
	do {
		int i = 0;
		int w = 0;
		int lastSpace = -1;
		int lastSpacePos = 0;
		int insertCursorPos = -1;

		if (pos == cursorpos) {
			insertCursorPos = 0;
		}

		while (i < (int) sizeof(lineBuffer) - 2) {
			char c = msg[pos];
			int adv;

			if (c == 0) {
				lastSpace = i;
				lastSpacePos = pos;
				break;
			}
			if (c == '\r') {
				pos++;
				continue;
			}
			if (c == '\n') {
				lastSpace = i;
				lastSpacePos = pos + 1;
				pos++;
				break;
			}

			/* One character at a time: measuring the remaining string per
			 * character would be quadratic on a long kernel command line. */
			adv = fontCharWidth(c, FONT_BODY);
			if ((w + adv) > maxWidth) {
				break;
			}

			if (c == ' ') {
				lastSpace = i;
				lastSpacePos = pos + 1;
			}
			lineBuffer[i] = c;
			i++;
			w += adv;
			pos++;
			if (pos == cursorpos) {
				insertCursorPos = i;
			}
		}

		if (lastSpace >= 0) {
			/* Break at the last space so words stay whole. */
			pos = lastSpacePos;
		} else {
			/* Nothing to break on -- a single unbroken run longer than the
			 * line, such as a device path. Cut it. */
			lastSpace = i;
		}
		lineBuffer[lastSpace] = 0;

		if ((insertCursorPos >= 0) && (lastSpace >= insertCursorPos)) {
			char *c;

			for (c = &lineBuffer[lastSpace]; c >= &lineBuffer[insertCursorPos]; c--) {
				c[1] = c[0];
			}
			if (cursor) {
				lineBuffer[insertCursorPos] = '_';
			} else {
				lineBuffer[insertCursorPos] = emulatedKey;
			}
		}

		if (lineNo >= scrollPos) {
			fontPrint(xoffset + x, yoffset + y, z, TexCol, lineBuffer, FONT_BODY);
			y += lineStep;
			if (y > (maxY - lineStep)) {
				break;
			}
		}
		lineNo++;
	} while (msg[pos] != 0);

	if (lineNo < scrollPos) {
		return lineNo;
	} else {
		return scrollPos;
	}
}

void graphic_common(void)
{
	gsKit_clear(gsGlobal, Blue);

	/* Paint background. */
	paintTexture(texStarfield, xoffset + 0, yoffset + 0, 0);

	paintTexture(texPenguin, xoffset + 5, yoffset + 10, 1);

	/* The build-option suffix letters that used to follow the version here --
	 * R (RESET_IOP), S (SCREENSHOT), M/O (NEW/OLD_ROM_MODULES), S
	 * (SHARED_MEM_DEBUG), which rendered as "3.0RSOS" -- have been dropped so
	 * the title reads as a plain version. Nothing is lost: Advanced Menu ->
	 * Versions -> "Kernelloader Version" spells every one of them out in full
	 * ("RESET_IOP SCREENSHOT OLD_ROM_MODULES ..."), which is far more use than
	 * a run of single letters. */

	/* Title lockup and bottom button bar are artwork, not text: the BIOS ROM
	 * font is one blocky typeface and cannot express the two-tone wordmark or
	 * the PlayStation glyphs. Both are ordinary textures drawn through the same
	 * path as the starfield, so neither needed new rendering code -- only the
	 * texture pipeline working, which it now does.
	 *
	 * Sits to the right of Tux (5..133) and is 284px wide, so it ends well
	 * inside the 640px safe area. */
	paintTexture(texTitle, xoffset + 140, yoffset + 24, 2);

	/* Full-width, anchored to the bottom edge. Dimmed during the load, when
	 * none of the buttons it advertises are live. */
	if (texBottomBar != NULL) {
		const int by = gsGlobal->Height - texBottomBar->Height;

		paintTextureShaded(texBottomBar, xoffset, by, 2,
			bootlogActive() ? SHADE_DIM : SHADE_BRIGHT);
	}

	/* System Info panel.
	 *
	 * Everything shown here was already gathered at startup but only reachable
	 * by walking into Advanced Menu -> Versions. The mockup puts it on the main
	 * screen, which is where it is useful.
	 *
	 * Sits to the right of the menu: entries are drawn from x=50 and their
	 * highlight ends at x+330, so x=400 clears them, and printTextBlock's
	 * 26-column text also stops short of it.
	 *
	 * Labels and values are placed at fixed columns rather than measured --
	 * gsKit_fontm offers no way to measure a rendered string, so right-aligning
	 * the values (as the mockup does) is not possible until there is a glyph
	 * atlas with an advance-width table. */
	if (bootlogActive()) {
		/* The boot has left the menu, so the System Info panel is gone and the
		 * log takes the screen. What is on it now is what the loader is doing;
		 * what the console is are answers the user already had time to read. */
		paintBootLog();
	} else if (texPanel != NULL) {
		int px = xoffset + 400;
		int py = yoffset + 200;
		int row = py + 8 + fontLineHeight(FONT_PANEL) + 8;

		paintTexture(texPanel, px, py, 2);

		/* Heading centred on the panel rather than pinned to a guessed
		 * column, which needs a string width -- the thing gsKit_fontm could
		 * not give us. */
		fontPrintCentred(px + (texPanel->Width / 2), py + 8, 3,
			TexPanelHead, "System Info", FONT_PANEL);

		/* Labels left, values RIGHT-ALIGNED to the panel's inner edge, as the
		 * mockup has them. The old code put values at a fixed x=88 and hoped:
		 * that is why "DVD-Video" had to become "DVD-V", because at the longer
		 * label the value collided with it and rendered as "DVD-Videono". */
/* The value is clipped to whatever the label leaves, so a long one -- a full
 * ROM version is 14 characters -- truncates with an ellipsis instead of
 * running backwards into the label. */
#define PANEL_ROW(label, value) \
		fontPrint(px + 12, row, 3, TexInfo, (label), FONT_PANEL); \
		{ \
			int lw = fontMeasure((label), FONT_PANEL); \
			int avail = texPanel->Width - 24 - lw - 8; \
			int vw = fontMeasure((value), FONT_PANEL); \
			if (vw > avail) { \
				fontPrintClipped(px + 12 + lw + 8, row, 3, TexInfo, (value), avail, FONT_PANEL); \
			} else { \
				fontPrintRight(px + texPanel->Width - 12, row, 3, TexInfo, (value), FONT_PANEL); \
			} \
		} \
		row += fontLineHeight(FONT_PANEL) + 2

		{
			/* Name the region rather than showing raw NVRAM bytes.
			 *
			 * ps2_region_type is "S%02x T%02x F%02x R%02x (%d NVM errors)",
			 * which is diagnostic detail for the Versions menu, not something
			 * to read at a glance -- and it overran the panel.
			 *
			 * ROMVER carries the region as a letter at index 4 ("0160EC..."
			 * -> 'E'), which is the same source region detection falls back to
			 * in modules.c and is more dependable than NVRAM. The raw bytes are
			 * still available under Advanced Menu -> Versions. */
			const char *region;

			switch ((strlen(ps2_rom_version) > 4) ? ps2_rom_version[4] : 0) {
			case 'J': region = "Japan";     break;
			case 'A': region = "USA";       break;
			case 'E': region = "Europe";    break;
			case 'C': region = "China";     break;
			case 'H': region = "Asia";      break;
			case 'K': region = "Korea";     break;
			case 'R': region = "Russia";    break;
			default:  region = "unknown";   break;
			}

			PANEL_ROW("Model", ps2_console_type);
			PANEL_ROW("ROM", ps2_rom_version);
			PANEL_ROW("Region", region);
			/* The address Linux will actually use, parsed out of the kernel
			 * command line's "ip=<client>:<server>:..." -- not getMyIP(), which
			 * is kernelloader's own ps2link setting and defaults to
			 * 192.168.0.10 regardless of the network in use. Showing that was
			 * worse than showing nothing: it looked authoritative and was not.
			 *
			 * Falls back to getMyIP() when the command line carries no ip=,
			 * which is the case for a ramdisk boot that never touches the
			 * network. */
			static char ipbuf[24];
			const char *ip = NULL;
			const char *cmdline = getKernelParameter();
			const char *f = cmdline;

			while ((f != NULL) && (*f != 0)) {
				/* Match "ip=" only at the start of a token. */
				if ((strncmp(f, "ip=", 3) == 0)
					&& ((f == cmdline) || (f[-1] == ' '))) {
					unsigned int n = 0;

					f += 3;
					while ((f[n] != 0) && (f[n] != ':') && (f[n] != ' ')
						&& (n < (sizeof(ipbuf) - 1))) {
						ipbuf[n] = f[n];
						n++;
					}
					ipbuf[n] = 0;
					if (n > 0) {
						ip = ipbuf;
					}
					break;
				}
				f = strchr(f, ' ');
				if (f != NULL) {
					f++;
				}
			}

			PANEL_ROW("IP", (ip != NULL) ? ip : getMyIP());
			/* Both of these decide whether an NFS root can work at all, and
			 * were previously only inferable from a boot log. "DVD-V" rather
			 * than "DVD-Video": the label column is 74px, and the longer text
			 * ran straight into its own value ("DVD-Videono"). */
			PANEL_ROW("Network", hasNetworkSupport() ? "yes" : "no");
			PANEL_ROW("DVD-Video", isDVDVSupported() ? "yes" : "no");
			PANEL_ROW("Loader", LOADER_VERSION);
		}
#undef PANEL_ROW
	}
	/* Video mode and build state, top right.
	 *
	 * These used to sit bottom right, above the button bar. That was fine while
	 * the System Info panel held seven rows, but it now holds ten and reaches
	 * far enough down that the two blocks overlapped -- "Auto" printed across
	 * the DVD-Video value and "UNSTABLE" across the loader version, which is
	 * how the collision was spotted.
	 *
	 * Top right is the only place with room that does not have to be re-tuned
	 * whenever a panel row is added: the title lockup occupies x=140..424 at
	 * y=24..98, the panel does not start until y=200, and nothing else is drawn
	 * to the right of the lockup. The bottom edge was never a good home for it
	 * either -- that strip is shared with the contextual hints on the left and
	 * the button bar beneath.
	 *
	 * Right-aligned to the same x=620 safe-area edge the panel values use, so
	 * the two blocks share a margin.
	 *
	 * (The byline that once lived at the bottom moved to Advanced Menu ->
	 * Versions -> Credits, where a changing name needs no re-tuning.) */
	fontPrintRight(xoffset + 620, yoffset + 40, 3, TexInfo,
		modeDescription[currentMode], FONT_STATUS);
	fontPrintRight(xoffset + 620, yoffset + 40 + fontLineHeight(FONT_STATUS) + 2, 3, TexInfo,
		"UNSTABLE"
#ifdef RTE
		" RTE"
#endif
		, FONT_STATUS);
}

/** Paint screen when Auto Boot is in process. */
void graphic_auto_boot_paint(int time)
{
	static char msg[80];

	if (!graphicInitialized) {
		return;
	}
	graphic_common();

	snprintf(msg, sizeof(msg), "Auto Boot in %d seconds.", time);
	fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
		msg, FONT_HINT);

	gsKit_queue_exec(gsGlobal);
	gsKit_finish(); /* Ensure that DMA has been finished before switching screen buffer. */
	gsKit_sync_flip(gsGlobal);
	/* Tells the manager a frame boundary passed, so it can retire textures that
	 * went unused this frame. Required by the bind()-per-draw workflow. */
	gsKit_TexManager_nextFrame(gsGlobal);
}

/** Paint current state on screen. */
/* The boot log window.
 *
 * Modelled on the SGI ARCS console: a bordered box over the background with
 * the log inside, top-aligned, oldest line first. Deliberately not a full
 * screen console -- the loader's own title and progress bar still say what
 * stage it is at, and the log says what that stage is actually doing.
 *
 * Drawn from primitives rather than a texture: it has to work when the failure
 * being diagnosed might be a texture upload. */
static void paintBootLog(void)
{
	const int x = xoffset + 40;
	const int y = yoffset + 120;
	const int w = 560;
	const int h = 16 + (BOOTLOG_LINES * (fontLineHeight(FONT_PANEL) + 1));
	int i;
	int row;

	/* Border, then the darker field inside it. Two sprites, so the frame is
	 * whatever is left showing round the edge. */
	gsKit_prim_sprite(gsGlobal, x, y, x + w, y + h, 2, TexPanelHead);
	gsKit_prim_sprite(gsGlobal, x + 2, y + 2, x + w - 2, y + h - 2, 3, Black);

	row = y + 8;
	for (i = 0; i < bootlogCount(); i++) {
		const char *line = bootlogLine(i);

		if (line == NULL) {
			break;
		}
		/* Clipped, not wrapped: a wrapped line would push an older one off the
		 * top, and the end of a long path matters less than keeping the
		 * sequence of stages intact. */
		fontPrintClipped(x + 8, row, 4, TexInfo, line, w - 16, FONT_PANEL);
		row += fontLineHeight(FONT_PANEL) + 1;
	}
}

void graphic_paint(void)
{
	const char *msg;

	if (!graphicInitialized) {
		return;
	}
	graphic_common();

	/* The disc sits at 100,300 and the boot log covers that, so it would draw
	 * straight through the text. It says nothing the log does not. */
	/* One progress bar, bottom centre, with what is being read in small text
	 * above it -- the shape of a macOS startup.
	 *
	 * Used for every load, not only a boot. It is the same information whether
	 * the loader is reading a kernel or a file the browser asked for, and a bar
	 * that moves position depending on context is harder to read than one that
	 * does not. At FONT_TITLE across the top it also collided with the boot log
	 * and drew over its first two lines. */
	if ((statusMessage != NULL) || (loadName[0] != 0)) {
		const char *what = (statusMessage != NULL) ? statusMessage : loadName;
		const int bx = xoffset + ((640 - LOAD_BAR_W) / 2);

		fontPrintCentred(xoffset + 320,
			yoffset + LOAD_BAR_Y - fontLineHeight(FONT_HINT) - 6,
			5, TexCol, what, FONT_HINT);

		if (texBarTrack != NULL) {
			paintTextureStretched(texBarTrack, bx, yoffset + LOAD_BAR_Y, 4, LOAD_BAR_W);
			paintTextureStretchedPartial(texBarFill, bx, yoffset + LOAD_BAR_Y, 5,
				LOAD_BAR_W, loadPercentage);
		} else {
			gsKit_prim_sprite(gsGlobal, bx, yoffset + LOAD_BAR_Y,
				bx + LOAD_BAR_W, yoffset + LOAD_BAR_Y + 20, 4, White);
			if (loadPercentage > 0) {
				gsKit_prim_sprite(gsGlobal, bx, yoffset + LOAD_BAR_Y,
					bx + (LOAD_BAR_W * loadPercentage) / 100,
					yoffset + LOAD_BAR_Y + 20, 5, Blue);
			}
		}
	}
	msg = getErrorMessage();
	if (msg != NULL) {
		fontPrint(xoffset + 50, yoffset + 170, 3, TexRed,
			"Error Message:", FONT_TITLE);
		printTextBlock(50, 230, 3, 540, gsGlobal->Height - reservedEndOfDisplayY, msg, 0, -1, 0);
	} else {
		if (!isInfoBufferEmpty()) {
			scrollPos = printTextBlock(xoffset + 50, yoffset + 170, 3, 540, gsGlobal->Height - reservedEndOfDisplayY, infoBuffer, scrollPos, -1, 0);
		} else {
			if (inputBuffer != NULL) {
				inputScrollPos = printTextBlock(xoffset + 50, yoffset + 170, 3, 540, gsGlobal->Height - reservedEndOfDisplayY, inputBuffer, inputScrollPos, writeable ? cursorpos : -1, writeable && (cursor_counter < (getModeFrequenzy()/2)));
			} else if (menu != NULL) {
				menu->paint();
			}
		}
	}
	if (enableDisc) {
		fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
			"Loading, please wait...", FONT_HINT);
	} else {
		if (msg != NULL) {
			if (usePad) {
				fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
					"Press CROSS to continue.", FONT_HINT);
			}
		} else {
			if (!isInfoBufferEmpty()) {
				if (usePad) {
					fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
						"Press CROSS to continue.", FONT_HINT);
					fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY + fontLineHeight(FONT_HINT) + 2, 3, TexInfo,
						"Use UP and DOWN to scroll.", FONT_HINT);
				}
			} else {
				if (inputBuffer != NULL) {
					if (writeable) {
						fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
							"Please use USB keyboard.", FONT_HINT);
					}
					fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY + fontLineHeight(FONT_HINT) + 2, 3, TexInfo,
						"Press CROSS to quit.", FONT_HINT);
				} else if (menu != NULL) {
					if (usePad) {
						fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
							"Press CROSS to select menu.", FONT_HINT);
						fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY + fontLineHeight(FONT_HINT) + 2, 3, TexInfo,
							"Use UP and DOWN to scroll.", FONT_HINT);
					}
				}
			}
		}
		if (!usePad) {
			fontPrint(xoffset + 50, gsGlobal->Height - reservedEndOfDisplayY, 3, TexInfo,
				"Please wait...", FONT_HINT);
		}
	}
	gsKit_queue_exec(gsGlobal);
	gsKit_finish(); /* Ensure that DMA has been finished before switching screen buffer. */
	gsKit_sync_flip(gsGlobal);
	/* Tells the manager a frame boundary passed, so it can retire textures that
	 * went unused this frame. Required by the bind()-per-draw workflow. */
	gsKit_TexManager_nextFrame(gsGlobal);

	cursor_counter++;
	if (cursor_counter >= getModeFrequenzy()) {
		cursor_counter = 0;
	}
}

extern "C" {

	/**
	 * Set load percentage of file.
	 * @param percentage Percentage to set (0 - 100).
	 * @param name File name printed on screen.
	 */
	void graphic_setLoadStage(int base, int span) {
		stageBase = base;
		stageSpan = span;
	}

	void graphic_setPercentage(int percentage, const char *name) {
		if (percentage > 100) {
			percentage = 100;
		}

		/* The loaders call this once per read chunk -- hundreds of times
		 * for a multi-megabyte kernel or initrd -- and every call used to
		 * repaint the whole screen. Most of those repaints drew a frame
		 * identical to the last one. Repaint only when something visible
		 * actually changed; it takes about twenty seconds off loading a
		 * 4MB initrd under an interpreter, and helps real hardware too.
		 *
		 * Compare the caller's pointer rather than the stored string:
		 * loadName is only 26 bytes and longer names are stored ellipsed,
		 * so comparing against it would never match for exactly the long
		 * paths that need this most. The loaders pass the same pointer on
		 * every iteration, which is what this is guarding. */
		static const char *lastName = (const char *) -1;

		/* Into the current stage's slice of the bar. With the default 0/100
		 * window this is the identity, so a caller outside a boot -- the file
		 * browser, say -- behaves exactly as before. */
		percentage = stageBase + ((percentage * stageSpan) / 100);

		if (percentage == loadPercentage && name == lastName) {
			return;
		}
		lastName = name;

		loadPercentage = percentage;

		if (name != NULL) {
			unsigned int len;

			len = strlen(name);
			if (len < sizeof(loadName)) {
				strcpy(loadName, name);
			} else {
				int r;
				int n;
				const char ellipse[] = "...";

				/* Name too long, show only start and end of string. */
				r = sizeof(loadName) - 1;
				n = (r - (sizeof(ellipse) - 1)) / 2;
				memcpy(loadName, name, n);
				r -= n;
				strcpy(&loadName[n], ellipse);
				r -= sizeof(ellipse) - 1;
				memcpy(&loadName[n + sizeof(ellipse) - 1], &name[len - r], r);
				loadName[sizeof(loadName) - 1] = 0;
			}
		} else {
			loadName[0] = 0;
		}
		graphic_paint();
	}

	/**
	 * Set status message.
	 * @param text Text displayed on screen.
	 */
	void graphic_setStatusMessage(const char *text) {
		if (text != NULL) {
			sio_printf("Status: %s\n", text);
		}
		statusMessage = text;
		graphic_paint();
	}
}

GSGLOBAL *getGsGlobal(void)
{
	return gsGlobal;
}

GSTEXTURE *getTexture(const char *filename)
{
	GSTEXTURE *tex = NULL;
	const rom_entry_t *romfile;
	romfile = rom_getFile(filename);
	if (romfile != NULL) {
		/* calloc, NOT malloc: gsKit's GSTEXTURE has grown fields since this
		 * code was written -- ClutPSM, TBW, Clut, VramClut, ClutStorageMode
		 * and Delayed are all left uninitialised by the assignments below.
		 * "Delayed" is the fatal one: it means "delay texture upload to VRAM",
		 * so a garbage non-zero value makes gsKit silently never upload the
		 * texture. It then draws nothing at all, with no error reported and
		 * regardless of pixel format or which upload path is taken. Zeroing
		 * the whole struct keeps every field gsKit gained at its default. */
		tex = (GSTEXTURE *) calloc(1, sizeof(GSTEXTURE));
		if (tex != NULL) {
			tex->Width = romfile->width;
			tex->Height = romfile->height;
			if (romfile->depth == 4) {
				tex->PSM = GS_PSM_CT32;
			} else {
				tex->PSM = GS_PSM_CT24;
			}
			tex->Mem = (u32 *) romfile->start;
			tex->Filter = GS_FILTER_LINEAR;

			/* VRAM is the texture manager's to hand out, not ours. Vram == 0
			 * means "not resident yet"; gsKit_TexManager_bind() in
			 * paintTexture() allocates and uploads on first use and re-uploads
			 * after an eviction. The previous code called gsKit_vram_alloc()
			 * here and assigned the result itself, which modern gsKit ignores.
			 * Delayed/VramClut are already 0 from the calloc above. */
			tex->Vram = 0;

			kprintf("texture \"%s\" %ux%u psm %u (%u bytes)\n", filename,
				tex->Width, tex->Height, tex->PSM,
				gsKit_texture_size_ee(tex->Width, tex->Height, tex->PSM));
		} else {
			error_printf("Out of memory while loading texture (%s).", filename);
		}
	} else {
		error_printf("Failed to open texture \"%s\".", filename);
	}
	return tex;
}

/** Re-establish a texture after the display mode changed.
 *
 * A mode switch tears down and rebuilds the GS display buffers, so everything
 * the texture manager had resident is gone. Marking the texture invalid is all
 * that is needed: the next gsKit_TexManager_bind() in paintTexture() sees no
 * residency and uploads it again.
 *
 * This used to re-run the manual gsKit_vram_alloc()/gsKit_texture_upload()
 * pair, which modern gsKit ignores -- see paintTexture(). */
void reallocTexture(GSTEXTURE *tex)
{
	if (tex == NULL) {
		return;
	}

	/* Clear residency by hand rather than calling
	 * gsKit_TexManager_invalidate(). Invalidate is for a texture the CURRENT
	 * manager knows about, but changeMode() replaces gsGlobal wholesale via
	 * gsKit_init_global(), so by the time this runs the new manager has never
	 * seen these textures -- and asking it to invalidate one sends it through
	 * bookkeeping that does not describe it. That was the burst of
	 * "TLB Miss ... [store]" at 0x300000xx inside memcpy(), followed by a Trap
	 * exception, on every video mode change.
	 *
	 * Vram == 0 is exactly the state getTexture() leaves a texture in before
	 * its first use, and gsKit_TexManager_bind() treats it as "not resident"
	 * and uploads into the rebuilt VRAM layout. */
	tex->Vram = 0;
}

bool isNTSCMode(void)
{
	return (gsGlobal->Mode != GS_MODE_PAL);
}

int getCurrentMode(void)
{
#if 0
	return gsGlobal->Mode;
#else
	return modeList[currentMode];
#endif
}

/**
 * Initialize graphic screen.
 */
Menu *graphic_main(void)
{
	int i;
	int numberOfMenuItems;

	addConfigVideoItem("videomode", &currentMode);

	gsGlobal = gsKit_init_global();

	if (isNTSCMode()) {
		frequenzy[0] = 60;
	} else {
		frequenzy[0] = 50;
	}

	kprintf("Switching to %s\n", modeDescription[currentMode]);

	lastMode = currentMode;

	applyVideoMode(gsGlobal, currentMode);

	if (isNTSCMode()) {
		numberOfMenuItems = 7;
	} else {
		numberOfMenuItems = 8;
	}

	dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
		D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

	// Initialize the DMAC
	dmaKit_chan_init(DMA_CHANNEL_GIF);
	dmaKit_chan_init(DMA_CHANNEL_FROMSPR);
	dmaKit_chan_init(DMA_CHANNEL_TOSPR);

	Black = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x00, 0x00);
	White = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x00, 0x00);
	Blue = GS_SETREG_RGBAQ(0x10, 0x10, 0xF0, 0x00, 0x00);
	Red = GS_SETREG_RGBAQ(0xF0, 0x10, 0x10, 0x00, 0x00);

	TexCol = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
	TexRed = GS_SETREG_RGBAQ(0xF0, 0x10, 0x10, 0x80, 0x00);
	TexInfo = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
	TexPanelHead = GS_SETREG_RGBAQ(0x4F, 0xB4, 0xF0, 0x80, 0x00);

	gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
	gsGlobal->ZBuffering = GS_SETTING_OFF;

	gsKit_init_screen(gsGlobal);

	/* The texture manager owns VRAM and must be (re)initialised whenever the
	 * display buffers are set up; without this, gsKit_TexManager_bind() has no
	 * state to allocate from and every textured draw silently renders nothing. */
	gsKit_TexManager_init(gsGlobal);

	check_screen_offsets();

	gsFont = gsKit_init_fontm();
	if (gsKit_fontm_upload(gsGlobal, gsFont) != 0) {
		kprintf("Can't find any font to use\n");
		SleepThread();
	}

	gsFont->Spacing = 0.8f;

	/* The atlas font. gsKit_fontm stays for now so anything not yet ported keeps
	 * drawing, but everything positioned relative to something else -- centred,
	 * right-aligned, or clipped to a box -- goes through font.c, because those
	 * need a string width and gsKit_fontm cannot supply one. */
	fontInit();
	texFolder = getTexture("folder.rgb");
	texUp = getTexture("up.rgb");
	texBack = getTexture("back.rgb");
	texSelected = getTexture("selected.rgb");
	texUnselected = getTexture("unselected.rgb");
	texPenguin = getTexture("penguin.rgb");
#ifdef NO_BACKDROP
	/* VRAM bisect aid (make NO_BACKDROP=1). The 731x512 backdrop is 1.5MB of
	 * the 4MB of VRAM once gsKit rounds its buffer width up to 768, which is
	 * what pushes a PAL 640x512 double-buffered display over the limit. Leave
	 * it unloaded to test that; everything else draws as usual. */
	texStarfield = NULL;
	kprintf("graphic: NO_BACKDROP build, starfield not uploaded\n");
#else
	texStarfield = getTexture("starfield.rgb");
#endif
	texTitle = getTexture("title.rgb");
	texBottomBar = getTexture("bottombar.rgb");
	texPanel = getTexture("panel.rgb");
	texHighlight = getTexture("highlight.rgb");
	texBarTrack = getTexture("bartrack.rgb");
	texBarFill = getTexture("barfill.rgb");

	mainMenu = menu = new Menu(gsGlobal, gsFont, numberOfMenuItems);
	menu->setPosition(50, 120);

	gsKit_mode_switch(gsGlobal, GS_ONESHOT);

	/* Activate graphic routines. */
	graphicInitialized = true;

	for (i = 0; i < 2; i++) {
		graphic_paint();
	}
	return menu;
}

void incrementMode(void)
{
	currentMode++;
	if (currentMode >= (int) (sizeof(modeList)/sizeof(modeList[0]))) {
		currentMode = 0;
	}
}

void decrementMode(void)
{
	currentMode--;
	if (currentMode < 0) {
		currentMode = (sizeof(modeList)/sizeof(modeList[0])) - 1;
	}
}

void setMode(int mode)
{
	if (mode < (int) (sizeof(modeList)/sizeof(modeList[0]))) {
		currentMode = mode;
	}
}

int getModeFrequenzy(void)
{
	return frequenzy[currentMode];
}

int setCurrentMenu(void *arg)
{
	Menu *newMenu = (Menu *) arg;

	menu = newMenu;

	return 0;
}

Menu *getCurrentMenu(void)
{
	return menu;
}

GSTEXTURE *getTexFolder(void)
{
	return texFolder;
}

GSTEXTURE *getTexUp(void)
{
	return texUp;
}

GSTEXTURE *getTexBack(void)
{
	return texBack;
}

GSTEXTURE *getTexHighlight(void)
{
	return texHighlight;
}

GSTEXTURE *getTexSelected(void)
{
	return texSelected;
}

GSTEXTURE *getTexUnselected(void)
{
	return texUnselected;
}

extern "C" {
	void setErrorMessage(const char *text) {
		if (errorMessage[writeMsgPos] == NULL) {
			errorMessage[writeMsgPos] = text;
			writeMsgPos = (writeMsgPos + 1) % MAX_MESSAGES;
		} else {
			kprintf("Error message queue is full at error:\n");
			kprintf("%s\n", text);
		}
	}

	void goToNextErrorMessage(void)
	{
		if (errorMessage[readMsgPos] != NULL) {
			errorMessage[readMsgPos] = NULL;
			readMsgPos = (readMsgPos + 1) % MAX_MESSAGES;
		}
	}

	const char *getErrorMessage(void) {
		return errorMessage[readMsgPos];
	}

	int error_printf(const char *format, ...)
	{
		static char buffer[MAX_MESSAGES][MAX_BUFFER];
		int ret;
		va_list varg;
		va_start(varg, format);

		if (errorMessage[writeMsgPos] == NULL) {
			ret = vsnprintf(buffer[writeMsgPos], MAX_BUFFER, format, varg);

			/* Mirror every on-screen error to the debug channel.
			 *
			 * This used to be a bare sio_putsn(), which appends no newline, so
			 * consecutive errors ran together into one unreadable line and had
			 * no marker to search for. Emitting a fixed "[kloader ERROR]"
			 * prefix and a guaranteed newline means the full error history can
			 * be read from the SIO log (PCSX2's console, or a serial capture on
			 * real hardware) without having to photograph the screen -- and
			 * without having to dismiss each prompt to see the next one. */
			kprintf("[kloader ERROR] %s\n", buffer[writeMsgPos]);

			setErrorMessage(buffer[writeMsgPos]);

			if (graphicInitialized) {
				if (readMsgPos == writeMsgPos) {
					/* Show it before doing anything else. */
					graphic_paint();
				}
			}
		} else {
			/* Queue full, so this message will never reach the screen -- which
			 * makes it all the more important that it reaches the log. The
			 * original logged "format", the raw template with its conversions
			 * unexpanded, hiding the actual values precisely when they were
			 * most needed. Expand it properly instead. */
			static char dropped[MAX_BUFFER];

			vsnprintf(dropped, MAX_BUFFER, format, varg);
			kprintf("[kloader ERROR dropped] %s\n", dropped);
			ret = -1;
		}

		va_end(varg);
		return ret;
	}

	void info_prints(const char *text)
	{
		int len = strlen(text) + 1;
		int remaining;

		if (len > MAX_INFO_BUFFER) {
			kprintf("info_prints(): text too long.\n");
			return;
		}

		remaining = MAX_INFO_BUFFER - infoBufferPos;
		if (len > remaining) {
			int required;
			int i;

			/* required space in buffer. */
			required = len - remaining;

			/* Find next new line. */
			for (i = required; i < MAX_INFO_BUFFER; i++) {
				if (infoBuffer[i] == '\n') {
					i++;
					break;
				}
			}

			if (i >= MAX_INFO_BUFFER) {
				/* Delete complete buffer, buffer doesn't have any carriage returns. */
				infoBufferPos = 0;
			} else {
				/* Scroll buffer and delete old stuff. */
				for (i = 0; i < (infoBufferPos - required); i++) {
					infoBuffer[i] = infoBuffer[required + i];
				}
				infoBufferPos = infoBufferPos - required;
			}
			infoBufferPos -= required;
		}
		strcpy(&infoBuffer[infoBufferPos], text);
		infoBufferPos += len - 1;
	}

	int info_printf(const char *format, ...)
	{
		int ret;
		static char buffer[MAX_BUFFER];
		va_list varg;
		va_start(varg, format);

		ret = vsnprintf(buffer, MAX_BUFFER, format, varg);
		info_prints(buffer);

		va_end(varg);
		return ret;
	}

	void setEnableDisc(int v)
	{
		enableDisc = v;

		/* Show it before doing anything else. */
		graphic_paint();
	}

	void scrollUpFast(void)
	{
		int i;

		for (i = 0; i < 8; i++) {
			scrollUp();
		}
	}

	void scrollUp(void)
	{
		if (inputBuffer != NULL) {
			inputScrollPos--;
			if (inputScrollPos < 0) {
				inputScrollPos = 0;
			}
		} else {
			scrollPos--;
			if (scrollPos < 0) {
				scrollPos = 0;
			}
		}
	}

	void scrollDownFast(void)
	{
		int i;

		for (i = 0; i < 8; i++) {
			scrollDown();
		}
	}

	void scrollDown(void)
	{
		if (inputBuffer != NULL) {
			inputScrollPos++;
		} else {
			scrollPos++;
		}
	}

	int getScrollPos(void)
	{
		return scrollPos;
	}

	int isInfoBufferEmpty(void)
	{
		return !(loaderConfig.enableEEDebug && (infoBufferPos > 0));
	}

	void clearInfoBuffer(void)
	{
		infoBuffer[0] = 0;
		scrollPos = 0;
		infoBufferPos = 0;

		if (graphicInitialized) {
			/* Show it before doing anything else. */
			graphic_paint();
		}
	}
	void enablePad(int val)
	{
		usePad = val;
	}

	void setInputBuffer(char *buffer, int w)
	{
		scrollPos = 0;
		inputScrollPos = 0;
		inputBuffer = buffer;
		writeable = w;
		if (writeable) {
			cursorpos = strlen(inputBuffer);
		} else {
			cursorpos = 0;
		}
	}

	char *getInputBuffer(void)
	{
		return inputBuffer;
	}

	int isWriteable(void)
	{
		return writeable;
	}

	void graphic_screenshot(void)
	{
		static int screenshotCounter = 0;
		char text[256];

#ifdef RESET_IOP
		snprintf(text, 256, "mass0:kloader%d.tga", screenshotCounter);
#else
		snprintf(text, 256, "host:kloader%d.tga", screenshotCounter);
#endif
		ps2_screenshot_file(text, gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1],
			gsGlobal->Width, gsGlobal->Height / 2, gsGlobal->PSM);
		screenshotCounter++;

		/* Fix deadlock in gsKit. */
		gsGlobal->FirstFrame = GS_SETTING_ON;
	}

	void moveScreen(int dx, int dy)
	{
		static int x = 0;
		static int y = 0;

		x += dx;
		y += dy;

		gsKit_set_display_offset(gsGlobal, x, y);
	}

	void changeMode(void)
	{
		int i;
		int numberOfMenuItems;

		/* Range check by decrement and increment. */
		decrementMode();
		incrementMode();

		kprintf("Switching to %s\n", modeDescription[currentMode]);
		if (lastMode == currentMode) {
			/* Nothing to do. */
			return;
		}

		lastMode = currentMode;

		/* Release the font through gsKit's own call, while the GSGLOBAL it was
		 * uploaded against is still valid.
		 *
		 * This used to hand-free the font's internals -- offset_table, TexBase
		 * and each page's Clut -- under the comment "gsKit has no function to
		 * free allocated memory for fonts". That is no longer true: modern
		 * gsKit provides gsKit_free_fontm(). Worse, the hand-freed GSFONTM was
		 * then handed straight back to gsKit_fontm_upload() below, which wrote
		 * through structures that had been partially torn down. The result was
		 * heap corruption on every video mode change, surfacing as a burst of
		 * "TLB Miss ... [store]" inside memcpy() and a Trap exception, at a
		 * scatter of PCs rather than one consistent site.
		 *
		 * The font is rebuilt from scratch after the new screen is up, exactly
		 * as graphic_main() does it. */
		gsKit_free_fontm(gsGlobal, gsFont);
		gsFont = NULL;

		gsKit_deinit_global(gsGlobal);

		gsGlobal = gsKit_init_global();


		applyVideoMode(gsGlobal, currentMode);

		if (isNTSCMode()) {
			numberOfMenuItems = 7;
		} else {
			numberOfMenuItems = 8;
		}

		dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC,
			D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

		// Initialize the DMAC
		dmaKit_chan_init(DMA_CHANNEL_GIF);
		dmaKit_chan_init(DMA_CHANNEL_FROMSPR);
		dmaKit_chan_init(DMA_CHANNEL_TOSPR);

		gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
		gsGlobal->ZBuffering = GS_SETTING_OFF;

		gsKit_init_screen(gsGlobal);

		/* changeMode() throws the old GSGLOBAL away and builds a new one with
		 * gsKit_init_global(), so this really is a fresh manager that has to be
		 * initialised -- it is not a redundant second call. */
		gsKit_TexManager_init(gsGlobal);

		check_screen_offsets();

		if (gsGlobal->Width > 640) {
			xoffset = (gsGlobal->Width - 640) / 2;
		} else {
			xoffset = 0;
		}

		/* Fresh font object for the new GSGLOBAL, mirroring graphic_main(). */
		fontReset();
		gsFont = gsKit_init_fontm();
		if (gsKit_fontm_upload(gsGlobal, gsFont) != 0) {
			kprintf("Can't find any font to use\n");
			SleepThread();
		}

		gsFont->Spacing = 0.8f;
		reallocTexture(texFolder);
		reallocTexture(texUp);
		reallocTexture(texBack);
		reallocTexture(texSelected);
		reallocTexture(texUnselected);
		reallocTexture(texPenguin);
		reallocTexture(texStarfield);
		reallocTexture(texTitle);
		reallocTexture(texBottomBar);
		reallocTexture(texPanel);
		reallocTexture(texHighlight);
		reallocTexture(texBarTrack);
		reallocTexture(texBarFill);

		if (mainMenu != NULL) {
			mainMenu->reset(gsGlobal, gsFont, numberOfMenuItems);
			mainMenu->setPosition(50, 120);
		}

		gsKit_mode_switch(gsGlobal, GS_ONESHOT);

		/* Activate graphic routines. */
		graphicInitialized = true;

		for (i = 0; i < 2; i++) {
			graphic_paint();
		}
	}


}

int getCursorPos(void)
{
	return cursorpos;
}

void incCursorPos(void)
{
	if (cursorpos < (int) strlen(inputBuffer)) {
		cursorpos++;
	}
}

void decCursorPos(void)
{
	if (cursorpos > 0) {
		cursorpos--;
	}
}

void homeCursorPos(void)
{
	cursorpos = 0;
}

void endCursorPos(void)
{
	cursorpos = strlen(inputBuffer);
}

void setEmulatedKey(int key)
{
	emulatedKey = key;
}
