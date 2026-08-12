/*
 * tge_types.h - TGE types header file
 *
 * Copyright (c) 2003 Marcus R. Brown <mrbrown@0xd6.org>
 *
 * See the file LICENSE, located within this directory, for licensing terms.
 */

#ifndef TGE_TYPES_H
#define TGE_TYPES_H

#include "stdint.h"

/* u64/s64 were "unsigned/signed long int", which is only 64-bit when the
 * compiler runs with -mgp64 (implied by the old -mips3 flag). Modern ps2dev gcc
 * targets the n32 ABI where long is 32-bit, silently making u64 half-width --
 * which shrank ee_dmatag_t from 16 to 12 bytes and broke the DMA tag layout.
 * long long is 64-bit under both ABIs. Same defect as kernel/stdint.h. */
typedef	unsigned char 		u8;
typedef unsigned short 		u16;
typedef unsigned int 		u32;
typedef unsigned long long	u64;

typedef signed char 		s8;
typedef signed short 		s16;
typedef	signed int 		s32;
typedef signed long long	s64;

#if defined(R5900) || defined(_R5900)
typedef unsigned int		u128 __attribute__((mode(TI)));
typedef int			s128 __attribute__((mode(TI)));
#endif

#endif /* TGE_TYPES_H */
