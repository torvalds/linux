/*
 * include/asm-arm/arch-at91rm9200/uncompress.h
 *
 *  Copyright (C) 2003 SAN People
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

#ifndef __ASM_ARCH_UNCOMPRESS_H
#define __ASM_ARCH_UNCOMPRESS_H

#include <asm/hardware.h>

/*
 * The following code assumes the serial port has already been
 * initialized by the bootloader.  We search for the first enabled
 * port in the most probable order.  If you didn't setup a port in
 * your bootloader then nothing will appear (which might be desired).
 *
 * This does not append a newline
 */
static void putc(int c)
{
	void __iomem *sys = (void __iomem *) AT91_BASE_SYS;	/* physical address */

	while (!(__raw_readl(sys + AT91_DBGU_SR) & AT91_DBGU_TXRDY))
		barrier();
	__raw_writel(c, sys + AT91_DBGU_THR);
}

static inline void flush(void)
{
	void __iomem *sys = (void __iomem *) AT91_BASE_SYS;	/* physical address */

	/* wait for transmission to complete */
	while (!(__raw_readl(sys + AT91_DBGU_SR) & AT91_DBGU_TXEMPTY))
		barrier();
}

#define arch_decomp_setup()

#define arch_decomp_wdog()

#endif
