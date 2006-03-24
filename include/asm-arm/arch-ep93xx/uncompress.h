/*
 * linux/include/asm-arm/arch-ep93xx/uncompress.h
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <asm/arch/ep93xx-regs.h>

static unsigned char __raw_readb(unsigned int ptr)
{
	return *((volatile unsigned char *)ptr);
}

static void __raw_writeb(unsigned char value, unsigned int ptr)
{
	*((volatile unsigned char *)ptr) = value;
}


#define PHYS_UART1_DATA		0x808c0000
#define PHYS_UART1_FLAG		0x808c0018
#define UART1_FLAG_TXFF		0x20

static __inline__ void putc(char c)
{
	int i;

	for (i = 0; i < 1000; i++) {
		/* Transmit fifo not full?  */
		if (!(__raw_readb(PHYS_UART1_FLAG) & UART1_FLAG_TXFF))
			break;
	}

	__raw_writeb(c, PHYS_UART1_DATA);
}

static void putstr(const char *s)
{
	while (*s) {
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

#define arch_decomp_setup()
#define arch_decomp_wdog()
