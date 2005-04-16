/*
 * include/asm-arm/arch-ixp4xx/uncompress.h 
 *
 * Copyright (C) 2002 Intel Corporation.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ARCH_UNCOMPRESS_H_
#define _ARCH_UNCOMPRESS_H_

#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <linux/serial_reg.h>

#define TX_DONE (UART_LSR_TEMT|UART_LSR_THRE)

static volatile u32* uart_base;

static __inline__ void putc(char c)
{
	/* Check THRE and TEMT bits before we transmit the character.
	 */
	while ((uart_base[UART_LSR] & TX_DONE) != TX_DONE); 
	*uart_base = c;
}

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	while (*s)
	{
		putc(*s);
		if (*s == '\n')
			putc('\r');
		s++;
	}
}

static __inline__ void __arch_decomp_setup(unsigned long arch_id)
{
	/*
	 * Coyote and gtwx5715 only have UART2 connected
	 */
	if (machine_is_adi_coyote() || machine_is_gtwx5715())
		uart_base = (volatile u32*) IXP4XX_UART2_BASE_PHYS;
	else
		uart_base = (volatile u32*) IXP4XX_UART1_BASE_PHYS;
}

/*
 * arch_id is a variable in decompress_kernel()
 */
#define arch_decomp_setup()	__arch_decomp_setup(arch_id)

#define arch_decomp_wdog()

#endif
