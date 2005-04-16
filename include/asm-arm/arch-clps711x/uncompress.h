/*
 *  linux/include/asm-arm/arch-clps711x/uncompress.h
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
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
#include <linux/config.h>
#include <asm/arch/io.h>
#include <asm/arch/hardware.h>
#include <asm/hardware/clps7111.h>

#undef CLPS7111_BASE
#define CLPS7111_BASE CLPS7111_PHYS_BASE

#define barrier()		__asm__ __volatile__("": : :"memory")
#define __raw_readl(p)		(*(unsigned long *)(p))
#define __raw_writel(v,p)	(*(unsigned long *)(p) = (v))

#ifdef CONFIG_DEBUG_CLPS711X_UART2
#define SYSFLGx	SYSFLG2
#define UARTDRx	UARTDR2
#else
#define SYSFLGx	SYSFLG1
#define UARTDRx	UARTDR1
#endif

/*
 * This does not append a newline
 */
static void putstr(const char *s)
{
	char c;

	while ((c = *s++) != '\0') {
		while (clps_readl(SYSFLGx) & SYSFLG_UTXFF)
			barrier();
		clps_writel(c, UARTDRx);

		if (c == '\n') {
			while (clps_readl(SYSFLGx) & SYSFLG_UTXFF)
				barrier();
			clps_writel('\r', UARTDRx);
		}
	}
	while (clps_readl(SYSFLGx) & SYSFLG_UBUSY)
		barrier();
}

/*
 * nothing to do
 */
#define arch_decomp_setup()

#define arch_decomp_wdog()
