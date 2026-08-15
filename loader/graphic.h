/* Copyright (c) 2007 Mega Man */
#ifndef _GRAPHIC_H_
#define _GRAPHIC_H_

#define MAX_INPUT_LEN 1024

#ifdef __cplusplus
#include "menu.h"
#include "config.h"

Menu *graphic_main(void);
void incrementMode(void);
void decrementMode(void);
void setMode(int mode);
int getModeFrequenzy(void);
void graphic_paint(void);
void graphic_auto_boot_paint(int time);
int setCurrentMenu(void *arg);
Menu *getCurrentMenu(void);
GSTEXTURE *getTexFolder(void);
GSTEXTURE *getTexUp(void);
GSTEXTURE *getTexBack(void);
GSTEXTURE *getTexSelected(void);
/** @returns the rounded highlight drawn behind the selected menu entry. */
GSTEXTURE *getTexHighlight(void);
/** Draw a texture at (x, y) with depth z. Binds it first. */
void paintTexture(GSTEXTURE *tex, int x, int y, int z);
GSTEXTURE *getTexUnselected(void);
/* graphic.cpp keeps gsGlobal file-static; font.cpp needs it to draw, and an
 * accessor is preferable to widening the variable's scope. */
GSGLOBAL *getGsGlobal(void);

/** The BIOS ROM font. Exposed for the FONT_ROM fallback in font.cpp. */
GSFONTM *getGsFont(void);
/* Defined in graphic.cpp but never declared in a header: every caller was in
 * graphic.cpp itself until now. */
GSTEXTURE *getTexture(const char *filename);
bool isNTSCMode(void);
int getCurrentMode(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif
	/* Which slice of the overall bar the current stage fills, as base and span
	 * in percent. Set once per boot stage; graphic_setPercentage() maps each
	 * loader's own 0..100 into it, so the bar fills once across the whole boot
	 * instead of restarting per file. Default 0/100 is the identity. */
	void graphic_setLoadStage(int base, int span);
	void graphic_setPercentage(int percentage, const char *name);
	void setErrorMessage(const char *text);
	const char *getErrorMessage(void);
	int error_printf(const char *format, ...);
	void info_prints(const char *text);
	int info_printf(const char *format, ...);
	void graphic_setStatusMessage(const char *text);
	void setEnableDisc(int v);
	void goToNextErrorMessage(void);
	void scrollUpFast(void);
	void scrollUp(void);
	void scrollDownFast(void);
	void scrollDown(void);
	int getScrollPos(void);
	int isInfoBufferEmpty(void);
	void clearInfoBuffer(void);
	void enablePad(int val);
	void setInputBuffer(char *buffer, int writeable);
	char *getInputBuffer(void);
	int isWriteable(void);
	void graphic_screenshot(void);
	void moveScreen(int dx, int dy);
	void changeMode(void);
	int getCursorPos(void);
	void incCursorPos(void);
	void decCursorPos(void);
	void homeCursorPos(void);
	void endCursorPos(void);
	void setEmulatedKey(int key);

	extern int xoffset;
	extern int yoffset;
#ifdef __cplusplus
}
#endif

#endif
