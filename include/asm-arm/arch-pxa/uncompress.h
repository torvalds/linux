/*
 * linux/include/asm-arm/arch-pxa/uncompress.h
 *
 * Author:	Nicolas Pitre
 * Copyright:	(C) 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define FFUART		((volatile unsigned long *)0x40100000)
#define BTUART		((volatile unsigned long *)0x40200000)
#define STUART		((volatile unsigned long *)0x40700000)

#define UART		FFUART


static __inline__ void putc(char c)
{
	while (!(UART[5] & 0x20));
	UART[0] = c;
}

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
