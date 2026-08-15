#ifndef _PS2DEV9_H_
#define _PS2DEV9_H_
/* Copyright (c) 2007 Mega Man */

#ifdef __cplusplus
extern "C" {
#endif

/** DEV9 hardware actually fitted: 0x20 CXD9566 PCMCIA, 0x30 CXD9611 expansion
 * bay, 0 none. Safe to call before ps2dev9_init() -- it only reads the revision
 * register and caches the answer. */
int ps2dev9_probe(void);

int ps2dev9_init(void);
int pcic_get_cardtype(void);
void dev9IntrEnable(int mask);

#ifdef __cplusplus
}
#endif

#endif
