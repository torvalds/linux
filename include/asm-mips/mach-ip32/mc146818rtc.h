/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 2001, 03 by Ralf Baechle
 * Copyright (C) 2000 Harald Koerfgen
 *
 * RTC routines for IP32 style attached Dallas chip.
 */
#ifndef __ASM_MACH_IP32_MC146818RTC_H
#define __ASM_MACH_IP32_MC146818RTC_H

#include <asm/ip32/mace.h>

#define RTC_PORT(x)	(0x70 + (x))

static unsigned char CMOS_READ(unsigned long addr)
{
	return mace->isa.rtc[addr << 8];
}

static inline void CMOS_WRITE(unsigned char data, unsigned long addr)
{
	mace->isa.rtc[addr << 8] = data;
}

/*
 * FIXME: Do it right. For now just assume that noone lives in 20th century
 * and no O2 user in 22th century ;-)
 */
#define mc146818_decode_year(year) ((year) + 2000)

#define RTC_ALWAYS_BCD	0

#endif /* __ASM_MACH_IP32_MC146818RTC_H */
