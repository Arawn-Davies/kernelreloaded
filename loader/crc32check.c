#include <stdio.h>
#include <graphic.h>
#include <sio.h>
/* sio_printf() -- removed from ps2sdk's sio.h, reimplemented in kprint.c. */
#include "kprint.h"

#include "stdint.h"
#include "crc32gen.h"
#include "crc32check.h"

#define CRC32MASK 0x04C11DB7 /* CRC-32 Bitmaske */

void _ftext(void);
void _etext(void);
void _frodata(void);
void _erodata(void);

/* MUST be volatile. This table is patched AFTER linking by crc32gen, which
 * writes the real CRC into the .crc32 section of the ELF. The initialiser below
 * leaves .crc implicitly 0, so a plain "const" array lets gcc 15 constant-fold
 * every read of crc[i].crc to literal 0 and never emit a load at all -- the
 * check then always fails with "expected 0x00000000" no matter what crc32gen
 * wrote. volatile forces the runtime load that makes post-link patching work.
 * Older toolchains happened to keep the load; this is not something to rely on. */
volatile const crc32_section_t crc[] __attribute__ ((__section__ (".crc32"))) = {
	{ .section = ".text", .start = (uint32_t) &_ftext, .end = (uint32_t) &_etext },
	{ .section = ".rodata", .start = (uint32_t) &_frodata, .end = (uint32_t) &_erodata },
};

/* This is an odd CRC: the register shifts left, MSB first, but the data bits
 * are fed in starting from the LSB. That is the same as an ordinary MSB-first
 * CRC over bit-reversed bytes, which is what makes a table possible at all.
 *
 * Doing it a bit at a time cost about 2.3 seconds per pass, and the loader
 * makes three passes, so a quarter of the entire boot was spent here. Both
 * tables are built once from the original bit loop rather than being written
 * out as constants, so they cannot drift from it, and the result is identical
 * to what crc32gen wrote into the .crc32 section.
 */
static uint32_t crc_table[256];
static uint8_t crc_reverse[256];
static int crc_table_ready = 0;

static void build_crc_table(void)
{
	int i;
	int n;

	for (i = 0; i < 256; i++) {
		uint32_t crc32 = ((uint32_t) i) << 24;
		uint8_t r = 0;

		for (n = 0; n < 8; n++) {
			if (crc32 & 0x80000000U) {
				crc32 = (crc32 << 1) ^ CRC32MASK;
			} else {
				crc32 <<= 1;
			}
			if (i & (1 << n)) {
				r |= 1 << (7 - n);
			}
		}
		crc_table[i] = crc32;
		crc_reverse[i] = r;
	}
	crc_table_ready = 1;
}

uint32_t calc_crc(const uint8_t *data, long size)
{
	uint32_t crc32 = 0; /* Schieberegister */
	long i;

	if (!crc_table_ready) {
		build_crc_table();
	}

	for (i = 0; i < size; i++) {
		uint8_t idx = (crc32 >> 24) ^ crc_reverse[data[i]];

		crc32 = (crc32 << 8) ^ crc_table[idx];
	}
	return crc32;
}

char getPrintableChar(char c)
{
	if ((c >= 0x20) && (c <= 0x7e)) {
		return c;
	} else {
		return '.';
	}
}

void sio_hexdump(const uint8_t *data, int size, uint32_t offset)
{
	int i;

	for (i = 0; i < size; i+=16) {
		int n;

		sio_printf("%08x  ", offset + i);

		for (n = 0; n < 16; n++) {
			if ((i + n) < size) {
				sio_printf("%02x ", data[i + n]);
			} else {
				sio_printf("  ");
			}
			if (n == 7) {
				sio_putc(' ');
			}
		}
		sio_putc(' ');
		sio_putc('|');
		for (n = 0; n < 16; n++) {
			if ((i + n) < size) {
				sio_putc(getPrintableChar(data[i + n]));
			} else {
				sio_putc(' ');
			}
		}
		sio_putc('|');
		sio_putc('\n');
	}
}

int crc32check(const char *msg)
{
	unsigned int i;

	sio_printf("Checking for \"%s\".\n", msg);

	for (i = 0; i < sizeof(crc)/sizeof(crc[0]); i++) {
		uint32_t crcvalue;
		const uint8_t *start = (void *) crc[i].start;
		const uint8_t *end = (void *) crc[i].end;

		crcvalue = calc_crc(start, end - start);

		if (crc[i].crc != crcvalue) {
			sio_printf("section %s CRC32 expected 0x%08x, but is 0x%08x, addr 0x%08x, end 0x%08x, size 0x%08x\n",
				crc[i].section, crc[i].crc, crcvalue, start, end, end - start);
			error_printf("kloader ELF integrity check failed in section %s. %s\n", crc[i].section, msg);
			//sio_hexdump(start, end - start, crc[i].fileoffset);
			sio_hexdump(start, 0x100, crc[i].fileoffset);
			return -1;
		} else {
			sio_printf("kloader section %s CRC32 OK\n", crc[i].section);
		}
	}
	return 0;
}
