/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 2001, 03 by Ralf Baechle
 *
 * RTC routines for Jazz style attached Dallas chip.
 */
#ifndef __ASM_MACH_JAZZ_MC146818RTC_H
#define __ASM_MACH_JAZZ_MC146818RTC_H

#include <asm/io.h>
#include <asm/jazz.h>

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_IRQ		8

static inline unsigned char CMOS_READ(unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));

	return *(char *)JAZZ_RTC_BASE;
}

static inline void CMOS_WRITE(unsigned char data, unsigned long addr)
{
	outb_p(addr, RTC_PORT(0));
	*(char *)JAZZ_RTC_BASE = data;
}

#define RTC_ALWAYS_BCD	0

#endif /* __ASM_MACH_JAZZ_MC146818RTC_H */
