/*
 * Copyright (C) 1999, 2000, 2005  MIPS Technologies, Inc.
 *	All rights reserved.
 *	Authors: Carsten Langgaard <carstenl@mips.com>
 *		 Maciej W. Rozycki <macro@mips.com>
 * Copyright (C) 2003, 05 Ralf Baechle (ralf@linux-mips.org)
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 */
#ifndef __ASM_MACH_ATLAS_MC146818RTC_H
#define __ASM_MACH_ATLAS_MC146818RTC_H

#include <linux/types.h>

#include <asm/addrspace.h>

#include <asm/mips-boards/atlas.h>
#include <asm/mips-boards/atlasint.h>

#define ARCH_RTC_LOCATION

#define RTC_PORT(x)	(ATLAS_RTC_ADR_REG + (x) * 8)
#define RTC_IO_EXTENT	0x100
#define RTC_IOMAPPED	0
#define RTC_IRQ		ATLAS_INT_RTC

static inline unsigned char CMOS_READ(unsigned long addr)
{
	volatile u32 *ireg = (void *)CKSEG1ADDR(RTC_PORT(0));
	volatile u32 *dreg = (void *)CKSEG1ADDR(RTC_PORT(1));

	*ireg = addr;
	return *dreg;
}

static inline void CMOS_WRITE(unsigned char data, unsigned long addr)
{
	volatile u32 *ireg = (void *)CKSEG1ADDR(RTC_PORT(0));
	volatile u32 *dreg = (void *)CKSEG1ADDR(RTC_PORT(1));

	*ireg = addr;
	*dreg = data;
}

#define RTC_ALWAYS_BCD	0

#define mc146818_decode_year(year) ((year) < 70 ? (year) + 2000 : (year) + 1900)

#endif /* __ASM_MACH_ATLAS_MC146818RTC_H */
