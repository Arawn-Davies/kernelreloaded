/* Copyright (c) 2007-2010 Mega Man */
#ifndef _LOADERMENU_H_
#define _LOADERMENU_H_

#ifdef __cplusplus
#include "menu.h"

void initMenu(Menu *menu);

/** @returns the Advanced Menu, or NULL before initMenu() has run. */
Menu *getAdvancedMenu(void);
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
