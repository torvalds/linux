/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998, 2001, 03 by Ralf Baechle
 *
 * RTC routines for PC style attached Dallas chip.
 */
#ifndef __ASM_MACH_DDB5074_MC146818RTC_H
#define __ASM_MACH_DDB5074_MC146818RTC_H

#include <asm/ddb5xxx/ddb5074.h>
#include <asm/ddb5xxx/ddb5xxx.h>

#define RTC_PORT(x)	(0x70 + (x))
#define RTC_IRQ		8

static inline unsigned char CMOS_READ(unsigned long addr)
{
	return *(volatile unsigned char *)(KSEG1ADDR(DDB_PCI_MEM_BASE)+addr);
}

static inline void CMOS_WRITE(unsigned char data, unsigned long addr)
{
	*(volatile unsigned char *)(KSEG1ADDR(DDB_PCI_MEM_BASE)+addr) = data;
}

#define RTC_ALWAYS_BCD	1

#endif /* __ASM_MACH_DDB5074_MC146818RTC_H */
