/* Copyright (c) 2007 Mega Man */
#include "menuEntry.h"
#include "graphic.h"
#include "font.h"

#define CHECK_ITEM_SIZE 22
void paint_quad(GSGLOBAL *gsGlobal, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int z, u64 color)
{
	gsKit_prim_line(gsGlobal, x1, y1, x2, y2, z, color);
	gsKit_prim_line(gsGlobal, x1, y1, x3, y3, z, color);
	//gsKit_prim_line(gsGlobal, x2, y2, x3, y3, z, color);
	gsKit_prim_line(gsGlobal, x3, y3, x4, y4, z, color);
	gsKit_prim_line(gsGlobal, x2, y2, x4, y4, z, color);
}

void paint_mark(GSGLOBAL *gsGlobal, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int z, u64 color)
{
	gsKit_prim_line(gsGlobal, x2, y2, x3, y3, z, color);
	gsKit_prim_line(gsGlobal, x1, y1, x4, y4, z, color);
}

void MenuEntry::paint(int x, int y, int z)
{
	/** Text colour. */
	u64 TexCol;
	u64 CheckCol;

	if (selected) {
		/* White on the dark rounded highlight. It used to be black, which was
		 * right when the highlight was a flat white rectangle. */
		CheckCol = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x00, 0x00);
		TexCol = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
	} else {
		TexCol = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x80, 0x00);
		CheckCol = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x00, 0x00);
	}

	if (selected) {
		GSTEXTURE *hl = getTexHighlight();

		if (hl != NULL) {
			/* Rounded, translucent, blue-bordered -- the mockup's treatment,
			 * replacing a flat white rectangle that fought the starfield. */
			paintTexture(hl, x - 8, y - 3, z);
		} else {
			u64 White = GS_SETREG_RGBAQ(0xFF, 0xFF, 0xFF, 0x00, 0x00);

			gsKit_prim_sprite(gsGlobal, x - 8, y - 3, x + 330, y + 21, z, White);
		}
	}

	if (multiItem) {
		/* Update name, because somebody could changed the value outside. */
		name = valueList[*multiValue];
	}

	if (checkItem) {
		//gsKit_prim_quad(gsGlobal, x, y, x, y + CHECK_ITEM_SIZE, x + CHECK_ITEM_SIZE, y, x + CHECK_ITEM_SIZE, y + CHECK_ITEM_SIZE, 3, CheckCol);
#if 0
		x += 2;
		y += 2;
		paint_quad(gsGlobal, x, y, x, y + CHECK_ITEM_SIZE, x + CHECK_ITEM_SIZE, y, x + CHECK_ITEM_SIZE, y + CHECK_ITEM_SIZE, z + 1, CheckCol);
		if (*checkValue) {
			paint_mark(gsGlobal, x, y, x, y + CHECK_ITEM_SIZE, x + CHECK_ITEM_SIZE, y, x + CHECK_ITEM_SIZE, y + CHECK_ITEM_SIZE, z + 1, CheckCol);
		}
		x -= 2;
		y -= 2;
		x += 30;
#else
	GSTEXTURE *tex = NULL;

	if (*checkValue) {
		tex = getTexSelected();
	} else {
		tex = getTexUnselected();
	}
	if (tex != NULL) {
		/* Modern gsKit will not sample a texture the manager has not made
		 * resident: unbound, these icons draw as a black square (whatever
		 * occupies that VRAM). paintTexture() binds for the same reason. */
		gsKit_TexManager_bind(gsGlobal, tex);
		gsKit_prim_sprite_texture(gsGlobal, tex, x, y - 3, 0, 0, x + tex->Width, y - 3 + tex->Height, tex->Width, tex->Height, z + 1, 0x80808080 /* color */);
		x += tex->Width;
	}
#endif
	}

	if (tex != NULL) {
		gsKit_TexManager_bind(gsGlobal, tex);
		gsKit_prim_sprite_texture(gsGlobal, tex, x, y - 3, 0, 0, x + tex->Width, y - 3 + tex->Height, tex->Width, tex->Height, z + 1, 0x80808080 /* color */);
		x += tex->Width;
	}
	/* Vertically centred in the 26px highlight rather than sitting on a
	 * baseline chosen by eye, which the atlas makes arithmetic. */
	fontPrint(x, y + ((26 - fontLineHeight(FONT_MENU)) / 2) - 2, z + 1, TexCol, name, FONT_MENU);
}

int MenuEntry::execute(void)
{
	if (executeFn != NULL) {
		return executeFn(executeArg);
	} else {
		error_printf("Missing function call for menu \"%s\".", name);
	}
	return 0;
}

void MenuEntry::reset(GSGLOBAL *gsGlobal, GSFONTM *gsFont)
{
	this->gsGlobal = gsGlobal;
	this->gsFont = gsFont;
}
