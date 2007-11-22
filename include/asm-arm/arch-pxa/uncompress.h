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

#include <linux/serial_reg.h>
#include <asm/arch/pxa-regs.h>

#define FFUART		((volatile unsigned long *)0x40100000)
#define BTUART		((volatile unsigned long *)0x40200000)
#define STUART		((volatile unsigned long *)0x40700000)
#define HWUART		((volatile unsigned long *)0x41600000)

#define UART		FFUART


static inline void putc(char c)
{
	if (!(UART[UART_IER] & IER_UUE))
		return;
	while (!(UART[UART_LSR] & LSR_TDRQ))
		barrier();
	UART[UART_TX] = c;
}

/*
 * This does not append a newline
 */
static inline void flush(void)
{
}

/*
 * nothing to do
 */
#define arch_decomp_setup()
#define arch_decomp_wdog()
