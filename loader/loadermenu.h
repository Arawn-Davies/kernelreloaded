/* Copyright (c) 2007-2010 Mega Man */
#ifndef _LOADERMENU_H_
#define _LOADERMENU_H_

#ifdef __cplusplus
#include "menu.h"

void initMenu(Menu *menu);

/** @returns the Advanced Menu, or NULL before initMenu() has run. */
Menu *getAdvancedMenu(void);

/** Registers every config.txt item (KernelParameter, AutoBootTime,
 * EnableExtraMem, ...) against configuration.cpp's configurationVector, plus
 * the default-value setup they depend on. The other half of what used to be
 * initMenu() -- split out because it has zero dependency on Menu/MenuEntry,
 * unlike the widget tree initMenu() builds around it, and
 * kloader-instant.elf needs config.txt to be readable without ever
 * constructing that tree. initMenu() still calls this first, so the normal
 * build's behavior is unchanged. */
void registerLoaderConfigItems(void);
#endif

#ifdef __cplusplus
extern "C" {
#endif
/** @returns the console's configured IP address. */
const char *getMyIP(void);
int setDefaultConfiguration(void *arg);
void configureVideoParameter(void);
int defaultSBIOSCalls(void *arg);
#ifdef __cplusplus
}
#endif

#endif
