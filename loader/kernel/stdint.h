/* Copyright (c) 2007 Mega Man */
#ifndef _STDINT_H_
#define _STDINT_H_

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
#ifdef PS2_EE
/* Was "unsigned long", which is only 64-bit when the compiler is invoked with
 * -mgp64 (implied by the old -mips3 flag). Under the n32 ABI that modern
 * ps2dev gcc targets, long is 32-bit, so 64-bit bitfields in gs.h failed with
 * "width of 'padXX' exceeds its type". long long is 64-bit under both. */
typedef unsigned long long uint64_t;
typedef unsigned int uint128_t __attribute__((mode(TI), aligned(16)));
#else
typedef unsigned long long uint64_t;
#endif
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
#ifdef PS2_EE
typedef signed long long int64_t;
typedef __signed__ int int128_t __attribute__((mode(TI), aligned(16)));
#else
typedef signed long long int64_t;
#endif

#endif
