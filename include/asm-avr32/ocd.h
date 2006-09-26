/*
 * AVR32 OCD Registers
 *
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_OCD_H
#define __ASM_AVR32_OCD_H

/* Debug Registers */
#define DBGREG_DID		  0
#define DBGREG_DC		  8
#define DBGREG_DS		 16
#define DBGREG_RWCS		 28
#define DBGREG_RWA		 36
#define DBGREG_RWD		 40
#define DBGREG_WT		 44
#define DBGREG_DTC		 52
#define DBGREG_DTSA0		 56
#define DBGREG_DTSA1		 60
#define DBGREG_DTEA0		 72
#define DBGREG_DTEA1		 76
#define DBGREG_BWC0A		 88
#define DBGREG_BWC0B		 92
#define DBGREG_BWC1A		 96
#define DBGREG_BWC1B		100
#define DBGREG_BWC2A		104
#define DBGREG_BWC2B		108
#define DBGREG_BWC3A		112
#define DBGREG_BWC3B		116
#define DBGREG_BWA0A		120
#define DBGREG_BWA0B		124
#define DBGREG_BWA1A		128
#define DBGREG_BWA1B		132
#define DBGREG_BWA2A		136
#define DBGREG_BWA2B		140
#define DBGREG_BWA3A		144
#define DBGREG_BWA3B		148
#define DBGREG_BWD3A		153
#define DBGREG_BWD3B		156

#define DBGREG_PID		284

#define SABAH_OCD		0x01
#define SABAH_ICACHE		0x02
#define SABAH_MEM_CACHED	0x04
#define SABAH_MEM_UNCACHED	0x05

/* Fields in the Development Control register */
#define DC_SS_BIT		8

#define DC_SS			(1 <<  DC_SS_BIT)
#define DC_DBE			(1 << 13)
#define DC_RID			(1 << 27)
#define DC_ORP			(1 << 28)
#define DC_MM			(1 << 29)
#define DC_RES			(1 << 30)

/* Fields in the Development Status register */
#define DS_SSS			(1 <<  0)
#define DS_SWB			(1 <<  1)
#define DS_HWB			(1 <<  2)
#define DS_BP_SHIFT		8
#define DS_BP_MASK		(0xff << DS_BP_SHIFT)

#define __mfdr(addr)							\
({									\
	register unsigned long value;					\
	asm volatile("mfdr	%0, %1" : "=r"(value) : "i"(addr));	\
	value;								\
})
#define __mtdr(addr, value)						\
	asm volatile("mtdr	%0, %1" : : "i"(addr), "r"(value))

#endif /* __ASM_AVR32_OCD_H */
