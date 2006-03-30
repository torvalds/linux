/*
 *  linux/include/asm-arm/arch-integrator/uncompress.h
 *
 *  Copyright (C) 1999 ARM Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define AMBA_UART_DR	(*(volatile unsigned char *)0x16000000)
#define AMBA_UART_LCRH	(*(volatile unsigned char *)0x16000008)
#define AMBA_UART_LCRM	(*(volatile unsigned char *)0x1600000c)
#define AMBA_UART_LCRL	(*(volatile unsigned char *)0x16000010)
#define AMBA_UART_CR	(*(volatile unsigned char *)0x16000014)
#define AMBA_UART_FR	(*(volatile unsigned char *)0x16000018)

/*
 * This does not append a newline
 */
static void putc(int c)
{
	while (AMBA_UART_FR & (1 << 5))
		barrier();

	AMBA_UART_DR = c;
}

static inline void flush(void)
{
	while (AMBA_UART_FR & (1 << 3))
		barrier();
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()
