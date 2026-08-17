#include <stdio.h>
#include <kprint.h>
#include <sio.h>
/* fioPutc(), used by kputs() below. Needs NEWLIB_PORT_AWARE (set in Makefile). */
#include <fileio.h>

#include "memory.h"
#include "bootlog.h"
#include "iopmem.h"

#define BUFFER_SIZE 256

/* This special version of sio_putc() only accesses kernel memory.
 * This function also works when no TLBs are valid.
 */
void kputc(int c)
{
	register int sp __asm__("sp");

	if(c == '\n')
	{
	    kputc('\r');
	}

	if (sp & KSEG0_MASK) {
		/* Running in kernel mode. */
	
		/* Block until we're ready to transmit.  */
		while ((_lw(SIO_ISR | KSEG1_MASK) & 0xf000) == 0x8000);
	
		_sb(c, SIO_TXFIFO | KSEG1_MASK);
	} else {
		/* Running in user mode, no conversion needed. */

		/* Block until we're ready to transmit.  */
		while ((_lw(SIO_ISR) & 0xf000) == 0x8000);
	
		_sb(c, SIO_TXFIFO);
	}
}

void kputs(const char *str)
{
	register int sp __asm__("sp");
	const char *b;

	if (sp & KSEG0_MASK) {
		/* Running in kernel mode. */

		/* Code was written to run from user space address.
		 * Need to convert parameter to kernel space address,
		 * because TLB mapping can be missing.
		 */
		str = U2K(str);
	}
	for (b = str; *b != 0; b++) {
	    kputc(*b);
	}
	if (sp & KSEG0_MASK) {
		/* Running in kernel mode. */
		for (b = str; *b != 0; b++) {
			iop_putc(*b);
		}
	} else {
		/* Running in user mode, no conversion needed. */
		for (b = str; *b != 0; b++) {
			fioPutc(1, *b);
		}
	}
}

/** Print hexadecimal number in kernel mode. */
void kputx(uint32_t val)
{
	int i;

	for (i = 0; i < 8; i++) {
		char v;

		v = (val >> (4 * (7 - i))) & 0xF;
		if (v < 10) {
			kputc('0' + v);
			iop_putc('0' + v);
		} else {
			kputc('a' + v - 10);
			iop_putc('a' + v - 10);
		}
	}
}

int kprintf(const char *format, ...)
{
	va_list args;
	int ret;
	static char buffer[BUFFER_SIZE];
	char *b;
	register int sp __asm__("sp");

	if (sp & KSEG0_MASK) {
		/* Running in kernel mode. */
		format = U2K(format);
		b = U2K(buffer);
	} else {
		/* Running in user mode, no conversion needed. */
		b = buffer;
	}

	va_start(args, format);
	ret = vsnprintf(b, BUFFER_SIZE, format, args);
	kputs(b);
	va_end(args);

	/* Mirror to the on-screen log. Only from user mode: in kernel mode the
	 * buffer pointer has been translated to KSEG0 and the TLB mapping the log
	 * needs may not exist, and nothing printed from there is worth a fault. */
	if (!(sp & KSEG0_MASK)) {
		bootlogAppend(b);
	}

	return ret;
}

/* sio_printf() was provided by older ps2sdk and has since been removed -- the
 * current sio.h keeps sio_putc/sio_puts/sio_putsn/sio_write but no formatted
 * variant. main.cpp and crc32check.c both call it, so it is reimplemented here
 * rather than at those call sites; this file already owns SIO printing and is
 * already in EE_OBJS.
 *
 * Deliberately NOT routed through kputs() like kprintf() above: kputs() also
 * mirrors output to iop_putc()/fioPutc(), which the original sio_printf() never
 * did. sio_putsn() is the direct equivalent -- "puts without appending a
 * newline" -- so the format string stays in control of line breaks. It is used
 * un-initialised elsewhere in this program already (graphic.cpp:847).
 *
 * Its own buffer, so that interleaved kprintf()/sio_printf() cannot clobber
 * each other. */
int sio_printf(const char *format, ...)
{
	va_list args;
	int ret;
	static char sio_buffer[BUFFER_SIZE];

	va_start(args, format);
	ret = vsnprintf(sio_buffer, BUFFER_SIZE, format, args);
	va_end(args);

	sio_putsn(sio_buffer);

	return ret;
}

/** Called if system is in an unrecoverable state. */
void panic()
{
	kputs("panic!\n");

	/* Nothing can be done here. */
	while(1);
}
