/*
 * linux/include/asm-arm/arch-h720x/uncompress.h
 *
 * Copyright (C) 2001-2002 Jungjun Kim
 */

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <asm/hardware.h>

#define LSR 	0x14
#define TEMPTY 	0x40

static void putstr(const char *s)
{
	char c;
	volatile unsigned char *p = (volatile unsigned char *)(IO_PHYS+0x20000);

	while ( (c = *s++) != '\0') {
		/* wait until transmit buffer is empty */
		while((p[LSR] & TEMPTY) == 0x0);
		/* write next character */
		*p = c;

		if(c == '\n') {
			while((p[LSR] & TEMPTY) == 0x0);
			*p = '\r';
		}
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()

#endif
