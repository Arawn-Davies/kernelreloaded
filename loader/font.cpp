/* Proportional text from a glyph atlas. See font.h for why this exists. */

#include <string.h>
#include <gsKit.h>

#include "font.h"
#include "glyphsdata.h"
#include "graphic.h"
#include "kprint.h"

#ifdef FONT_ROM
/* Build-time fallback to the BIOS ROM font (make FONT_ROM=1).
 *
 * A bisect aid, not a supported mode. The atlas adds two things the loader
 * never used to do -- a 512x256 texture upload and a sprite per glyph -- and
 * this reverts both while leaving every caller and the rest of the UI alone,
 * so a hardware failure can be attributed to the text path or ruled out.
 *
 * gsKit_fontm cannot measure a string, which is the whole reason the atlas
 * exists, so the metrics below are approximations from the ROM font's fixed
 * 16x16 cell. Alignment will be visibly off. That is expected: this build is
 * for answering "does it draw at all", not for looking right.
 */

#define FONTM_CELL 16.0f

static int fontmReady;

int fontInit(void)
{
	fontmReady = (getGsFont() != NULL);
	if (!fontmReady) {
		kprintf("font: FONT_ROM build, but gsKit_init_fontm() gave nothing\n");
		return -1;
	}
	kprintf("font: FONT_ROM build, drawing through the BIOS ROM font\n");
	return 0;
}

void fontReset(void)
{
	/* The ROM font's VRAM is owned by graphic.cpp, which re-uploads it on a
	 * mode change. Nothing to drop here. */
}

int fontLineHeight(float scale)
{
	return (int) (FONTM_CELL * scale);
}

int fontCharWidth(char c, float scale)
{
	(void) c;
	return (int) ((FONTM_CELL * scale) + 0.5f);
}

int fontMeasure(const char *text, float scale)
{
	if (text == NULL) {
		return 0;
	}
	return (int) ((strlen(text) * FONTM_CELL * scale) + 0.5f);
}

void fontPrint(int x, int y, int z, u64 colour, const char *text, float scale)
{
	GSGLOBAL *gs = getGsGlobal();
	GSFONTM *fm = getGsFont();

	if (!fontmReady || (text == NULL) || (gs == NULL) || (fm == NULL)) {
		return;
	}
	gsKit_fontm_print_scaled(gs, fm, (float) x, (float) y, z, scale, colour, text);
}

void fontPrintCentred(int cx, int y, int z, u64 colour, const char *text, float scale)
{
	fontPrint(cx - (fontMeasure(text, scale) / 2), y, z, colour, text, scale);
}

void fontPrintRight(int rx, int y, int z, u64 colour, const char *text, float scale)
{
	fontPrint(rx - fontMeasure(text, scale), y, z, colour, text, scale);
}

int fontPrintClipped(int x, int y, int z, u64 colour, const char *text, int maxWidth, float scale)
{
	int w;

	if (text == NULL) {
		return 0;
	}
	/* No ellipsis: a fixed-width estimate cannot cut accurately, and a wrong
	 * cut would be one more thing to explain in a test build. */
	w = fontMeasure(text, scale);
	if (w > maxWidth) {
		w = maxWidth;
	}
	fontPrint(x, y, z, colour, text, scale);
	return w;
}

#else /* !FONT_ROM -- the glyph atlas, which is the real implementation. */

static GSTEXTURE *atlas = NULL;

/* Anything outside the generated range draws as a space rather than as a
 * missing-glyph box: the loader shows device paths and kernel command lines,
 * and a stray high byte should not turn into visual noise. */
static const glyph_t *glyphFor(unsigned char c)
{
	if (c < GLYPHS_FIRST || c > GLYPHS_LAST) {
		c = ' ';
	}
	return &glyphsGlyphs[c - GLYPHS_FIRST];
}

int fontInit(void)
{
	if (atlas != NULL) {
		return 0;
	}
	/* Rides the same png2rgb -> bin2s -> rom_getFile() path as every other
	 * texture, so the atlas is just another entry in RGB_FILES. */
	atlas = getTexture("glyphs.rgb");
	if (atlas == NULL) {
		kprintf("font: atlas missing, text will not draw\n");
		return -1;
	}
	return 0;
}

void fontReset(void)
{
	/* A mode change tears down VRAM; clearing residency is all that is needed,
	 * exactly as reallocTexture() does for the other textures. */
	if (atlas != NULL) {
		atlas->Vram = 0;
	}
}

int fontLineHeight(float scale)
{
	return (int) (GLYPHS_LINEHEIGHT * scale);
}

int fontCharWidth(char c, float scale)
{
	return (int) ((glyphFor((unsigned char) c)->advance * scale) + 0.5f);
}

int fontMeasure(const char *text, float scale)
{
	int w = 0;

	if (text == NULL) {
		return 0;
	}
	/* Accumulate in atlas pixels and scale once at the end. Scaling each
	 * advance and rounding as we go would drift by a pixel every few
	 * characters, and a measurement that disagrees with what is drawn is worse
	 * than no measurement at all. */
	while (*text != '\0') {
		w += glyphFor((unsigned char) *text)->advance;
		text++;
	}
	return (int) ((w * scale) + 0.5f);
}

void fontPrint(int x, int y, int z, u64 colour, const char *text, float scale)
{
	GSGLOBAL *gs = getGsGlobal();
	const int h = (int) (GLYPHS_LINEHEIGHT * scale);
	int cum;

	if ((atlas == NULL) || (text == NULL) || (gs == NULL)) {
		return;
	}

	/* Bind once for the whole string, not per glyph. Modern gsKit will not
	 * sample a texture the manager has not made resident, and binding per
	 * character would ask it to re-check residency dozens of times a line. */
	gsKit_TexManager_bind(gs, atlas);

	/* Positions come from the CUMULATIVE unscaled advance, scaled once per
	 * edge -- not from a running scaled pen. Both give the same spacing, but
	 * only this way does the last glyph's right edge land exactly on
	 * x + fontMeasure(), because both are (int)(total * scale) of the same
	 * total. With a running pen the two disagree by a pixel or so depending on
	 * where rounding fell, and right-aligned columns then fail to line up with
	 * each other -- which is exactly what a right-aligned column is for. */
	cum = 0;
	while (*text != '\0') {
		const glyph_t *g = glyphFor((unsigned char) *text);
		const int x0 = x + (int) ((cum * scale) + 0.5f);
		const int x1 = x + (int) (((cum + g->advance) * scale) + 0.5f);

		/* Skip blanks: a space is pure advance, and drawing an empty sprite
		 * still costs a GS primitive. */
		if (*text != ' ') {
			/* The cell is padded either side, so it is drawn slightly wider
			 * than the advance and offset back by the same amount. Without
			 * this the antialiased edge that spills past the advance box is
			 * clipped -- by a different amount per glyph, which is what made a
			 * right-aligned column's edge wobble. Spacing is unaffected:
			 * x0/x1 still come from the advance alone. */
			const int pad = (int) (GLYPHS_PAD * scale) + 1;

			gsKit_prim_sprite_texture(gs, atlas,
				x0 - pad, y,                           /* screen top-left */
				g->x, g->y,                            /* atlas top-left */
				x1 + pad, y + h,                       /* screen bottom-right */
				g->x + g->advance + (2 * GLYPHS_PAD), g->y + GLYPHS_LINEHEIGHT,
				z, colour);
		}
		cum += g->advance;
		text++;
	}
}

void fontPrintCentred(int cx, int y, int z, u64 colour, const char *text, float scale)
{
	fontPrint(cx - (fontMeasure(text, scale) / 2), y, z, colour, text, scale);
}

void fontPrintRight(int rx, int y, int z, u64 colour, const char *text, float scale)
{
	int rsb = 0;
	size_t len;

	/* Align the INK, not the advance box.
	 *
	 * Right-aligning on advances is arithmetically correct and still looks
	 * ragged, because the eye reads where the ink stops and every glyph leaves
	 * a different gap after it: measured against DejaVu, '0' and '2' leave 2px
	 * while '4', 'e', 'o' and 's' leave 1. A column of values ending in
	 * different characters therefore wobbles by a pixel. Pull the string right
	 * by the last glyph's bearing so the ink lands on rx.
	 *
	 * A trailing space has no ink to align, so it is left alone. */
	if (text != NULL) {
		len = strlen(text);
		if ((len > 0) && (text[len - 1] != ' ')) {
			rsb = glyphFor((unsigned char) text[len - 1])->rsb;
		}
	}

	/* Rounded, not truncated. At FONT_PANEL (0.40) a bearing of 1 scales to
	 * 0.4 and a bearing of 2 to 0.8; truncating makes both zero and the whole
	 * correction vanishes -- which is why the column still looked ragged after
	 * ink alignment was added. Rounded, they become 0 and 1, which is the
	 * distinction actually visible on screen. */
	fontPrint(rx - fontMeasure(text, scale) + (int) ((rsb * scale) + 0.5f), y, z, colour, text, scale);
}

int fontPrintClipped(int x, int y, int z, u64 colour, const char *text, int maxWidth, float scale)
{
	static char buf[256];
	const char ellipsis[] = "...";
	int w;
	int i;
	int limit;

	if (text == NULL) {
		return 0;
	}

	w = fontMeasure(text, scale);
	if (w <= maxWidth) {
		fontPrint(x, y, z, colour, text, scale);
		return w;
	}

	/* Cut to fit with room for the ellipsis. Measured rather than guessed at a
	 * character count, which is what the old fixed-column code had to do. */
	limit = maxWidth - fontMeasure(ellipsis, scale);
	if (limit <= 0) {
		return 0;
	}

	w = 0;
	for (i = 0; (text[i] != '\0') && (i < (int) sizeof(buf) - (int) sizeof(ellipsis)); i++) {
		int adv = (int) (glyphFor((unsigned char) text[i])->advance * scale);

		if ((w + adv) > limit) {
			break;
		}
		buf[i] = text[i];
		w += adv;
	}
	strcpy(&buf[i], ellipsis);

	fontPrint(x, y, z, colour, buf, scale);
	return w + fontMeasure(ellipsis, scale);
}

#endif /* FONT_ROM */
