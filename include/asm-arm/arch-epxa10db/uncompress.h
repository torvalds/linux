/*
 *  linux/include/asm-arm/arch-epxa10db/uncompress.h
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2001 Altera Corporation
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
#include "asm/arch/platform.h"
#include "asm/arch/hardware.h"
#define UART00_TYPE (volatile unsigned int*)
#include "asm/arch/uart00.h"

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	while (*s) {
		while ((*UART_TSR(EXC_UART00_BASE) &
		       UART_TSR_TX_LEVEL_MSK)==15)
			barrier();

		*UART_TD(EXC_UART00_BASE) = *s;

		if (*s == '\n') {
			while ((*UART_TSR(EXC_UART00_BASE) &
			       UART_TSR_TX_LEVEL_MSK)==15)
				barrier();

			*UART_TD(EXC_UART00_BASE) = '\r';
		}
		s++;
	}
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()
