#ifndef _KPRINT_H_
#define _KPRINT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Functions for printing in kernel mode (ps2link and SIO). */
	void kputc(int c);
	void kputs(const char *str);
	int kprintf(const char *format, ...);
	void kputx(uint32_t val);
	/* Reimplemented in kprint.c -- removed from modern ps2sdk's sio.h. */
	int sio_printf(const char *format, ...);
	void panic();

#ifdef __cplusplus
}
#endif

#endif
