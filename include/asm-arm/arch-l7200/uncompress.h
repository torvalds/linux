/*
 * linux/include/asm-arm/arch-l7200/uncompress.h
 *
 * Copyright (C) 2000 Steve Hill (sjhill@cotw.com)
 *
 * Changelog:
 *  05-01-2000	SJH	Created
 *  05-13-2000	SJH	Filled in function bodies
 *  07-26-2000	SJH	Removed hard coded buad rate
 */

#include <asm/hardware.h>

#define IO_UART  IO_START + 0x00044000

#define __raw_writeb(v,p)	(*(volatile unsigned char *)(p) = (v))
#define __raw_readb(p)		(*(volatile unsigned char *)(p))

static __inline__ void putc(char c)
{
	while(__raw_readb(IO_UART + 0x18) & 0x20 ||
		__raw_readb(IO_UART + 0x18) & 0x08);
	__raw_writeb(c, IO_UART + 0x00);
}

static void putstr(const char *s)
{
	while (*s) {
		if (*s == 10) {			/* If a LF, add CR */
			putc(10);
			putc(13);
		}
		putc(*(s++));
	}
}

static __inline__ void arch_decomp_setup(void)
{
	__raw_writeb(0x00, IO_UART + 0x08);	/* Set HSB */
	__raw_writeb(0x00, IO_UART + 0x20);	/* Disable IRQs */
	__raw_writeb(0x01, IO_UART + 0x14);	/* Enable UART */
}

#define arch_decomp_wdog()
