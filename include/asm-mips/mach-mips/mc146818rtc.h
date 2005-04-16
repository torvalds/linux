/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2003 by Ralf Baechle
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
 *
 * RTC routines for Malta style attached PIIX4 device, which contains a
 * Motorola MC146818A-compatible Real Time Clock.
 */
#ifndef __ASM_MACH_MALTA_MC146818RTC_H
#define __ASM_MACH_MALTA_MC146818RTC_H

#include <asm/io.h>
#include <asm/mips-boards/generic.h>
#include <asm/mips-boards/malta.h>

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_IRQ		8

static inline unsigned char CMOS_READ(unsigned long addr)
{
	outb(addr, MALTA_RTC_ADR_REG);
	return inb(MALTA_RTC_DAT_REG);
}

static inline void CMOS_WRITE(unsigned char data, unsigned long addr)
{
	outb(addr, MALTA_RTC_ADR_REG);
	outb(data, MALTA_RTC_DAT_REG);
}

#define RTC_ALWAYS_BCD	0

#define mc146818_decode_year(year) ((year) < 70 ? (year) + 2000 : (year) + 1970)

#endif /* __ASM_MACH_MALTA_MC146818RTC_H */
