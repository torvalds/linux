/* mb-regs.h: motherboard registers
 *
 * Copyright (C) 2003, 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_MB_REGS_H
#define _ASM_MB_REGS_H

#include <asm/cpu-irqs.h>
#include <asm/sections.h>
#include <asm/mem-layout.h>

#define __region_IO	KERNEL_IO_START	/* the region from 0xe0000000 to 0xffffffff has suitable
					 * protection laid over the top for use in memory-mapped
					 * I/O
					 */

#define __region_CS0	0xff000000	/* Boot ROMs area */

#ifdef CONFIG_MB93091_VDK
/*
 * VDK motherboard and CPU card specific stuff
 */

#include <asm/mb93091-fpga-irqs.h>

#define IRQ_CPU_MB93493_0	IRQ_CPU_EXTERNAL0
#define IRQ_CPU_MB93493_1	IRQ_CPU_EXTERNAL1

#define __region_CS2	0xe0000000	/* SLBUS/PCI I/O space */
#define __region_CS2_M		0x0fffffff /* mask */
#define __region_CS2_C		0x00000000 /* control */
#define __region_CS5	0xf0000000	/* MB93493 CSC area (DAV daughter board) */
#define __region_CS5_M		0x00ffffff
#define __region_CS5_C		0x00010000
#define __region_CS7	0xf1000000	/* CB70 CPU-card PCMCIA port I/O space */
#define __region_CS7_M		0x00ffffff
#define __region_CS7_C		0x00410701
#define __region_CS1	0xfc000000	/* SLBUS/PCI bridge control registers */
#define __region_CS1_M		0x000fffff
#define __region_CS1_C		0x00000000
#define __region_CS6	0xfc100000	/* CB70 CPU-card DM9000 LAN I/O space */
#define __region_CS6_M		0x000fffff
#define __region_CS6_C		0x00400707
#define __region_CS3	0xfc200000	/* MB93493 CSR area (DAV daughter board) */
#define __region_CS3_M		0x000fffff
#define __region_CS3_C		0xc8100000
#define __region_CS4	0xfd000000	/* CB70 CPU-card extra flash space */
#define __region_CS4_M		0x00ffffff
#define __region_CS4_C		0x00000f07

#define __region_PCI_IO		(__region_CS2 + 0x04000000UL)
#define __region_PCI_MEM	(__region_CS2 + 0x08000000UL)
#define __flush_PCI_writes()						\
do {									\
	__builtin_write8((volatile void *) __region_PCI_MEM, 0);	\
} while(0)

#define __is_PCI_IO(addr) \
	(((unsigned long)(addr) >> 24) - (__region_PCI_IO >> 24)  < (0x04000000UL >> 24))

#define __is_PCI_MEM(addr) \
	((unsigned long)(addr) - __region_PCI_MEM < 0x08000000UL)

#define __is_PCI_addr(addr) \
	((unsigned long)(addr) - __region_PCI_IO < 0x0c000000UL)

#define __get_CLKSW()	({ *(volatile unsigned long *)(__region_CS2 + 0x0130000cUL) & 0xffUL; })
#define __get_CLKIN()	(__get_CLKSW() * 125U * 100000U / 24U)

#ifndef __ASSEMBLY__
extern int __nongprelbss mb93090_mb00_detected;
#endif

#define __addr_LEDS()		(__region_CS2 + 0x01200004UL)
#ifdef CONFIG_MB93090_MB00
#define __set_LEDS(X)							\
do {									\
	if (mb93090_mb00_detected)					\
		__builtin_write32((void *) __addr_LEDS(), ~(X));	\
} while (0)
#else
#define __set_LEDS(X)
#endif

#define __addr_LCD()		(__region_CS2 + 0x01200008UL)
#define __get_LCD(B)		__builtin_read32((volatile void *) (B))
#define __set_LCD(B,X)		__builtin_write32((volatile void *) (B), (X))

#define LCD_D			0x000000ff		/* LCD data bus */
#define LCD_RW			0x00000100		/* LCD R/W signal */
#define LCD_RS			0x00000200		/* LCD Register Select */
#define LCD_E			0x00000400		/* LCD Start Enable Signal */

#define LCD_CMD_CLEAR		(LCD_E|0x001)
#define LCD_CMD_HOME		(LCD_E|0x002)
#define LCD_CMD_CURSOR_INC	(LCD_E|0x004)
#define LCD_CMD_SCROLL_INC	(LCD_E|0x005)
#define LCD_CMD_CURSOR_DEC	(LCD_E|0x006)
#define LCD_CMD_SCROLL_DEC	(LCD_E|0x007)
#define LCD_CMD_OFF		(LCD_E|0x008)
#define LCD_CMD_ON(CRSR,BLINK)	(LCD_E|0x00c|(CRSR<<1)|BLINK)
#define LCD_CMD_CURSOR_MOVE_L	(LCD_E|0x010)
#define LCD_CMD_CURSOR_MOVE_R	(LCD_E|0x014)
#define LCD_CMD_DISPLAY_SHIFT_L	(LCD_E|0x018)
#define LCD_CMD_DISPLAY_SHIFT_R	(LCD_E|0x01c)
#define LCD_CMD_FUNCSET(DL,N,F)	(LCD_E|0x020|(DL<<4)|(N<<3)|(F<<2))
#define LCD_CMD_SET_CG_ADDR(X)	(LCD_E|0x040|X)
#define LCD_CMD_SET_DD_ADDR(X)	(LCD_E|0x080|X)
#define LCD_CMD_READ_BUSY	(LCD_E|LCD_RW)
#define LCD_DATA_WRITE(X)	(LCD_E|LCD_RS|(X))
#define LCD_DATA_READ		(LCD_E|LCD_RS|LCD_RW)

#else
/*
 * PDK unit specific stuff
 */

#include <asm/mb93093-fpga-irqs.h>

#define IRQ_CPU_MB93493_0	IRQ_CPU_EXTERNAL0
#define IRQ_CPU_MB93493_1	IRQ_CPU_EXTERNAL1

#define __region_CS5	0xf0000000	/* MB93493 CSC area (DAV daughter board) */
#define __region_CS5_M		0x00ffffff /* mask */
#define __region_CS5_C		0x00010000 /* control */
#define __region_CS2	0x20000000	/* FPGA registers */
#define __region_CS2_M		0x000fffff
#define __region_CS2_C		0x00000000
#define __region_CS1	0xfc100000	/* LAN registers */
#define __region_CS1_M		0x000fffff
#define __region_CS1_C		0x00010404
#define __region_CS3	0xfc200000	/* MB93493 CSR area (DAV daughter board) */
#define __region_CS3_M		0x000fffff
#define __region_CS3_C		0xc8000000
#define __region_CS4	0xfd000000	/* extra ROMs area */
#define __region_CS4_M		0x00ffffff
#define __region_CS4_C		0x00000f07

#define __region_CS6	0xfe000000	/* not used - hide behind CPU resource I/O regs */
#define __region_CS6_M		0x000fffff
#define __region_CS6_C		0x00000f07
#define __region_CS7	0xfe000000	/* not used - hide behind CPU resource I/O regs */
#define __region_CS7_M		0x000fffff
#define __region_CS7_C		0x00000f07

#define __is_PCI_IO(addr)	0	/* no PCI */
#define __is_PCI_MEM(addr)	0
#define __is_PCI_addr(addr)	0
#define __region_PCI_IO		0
#define __region_PCI_MEM	0
#define __flush_PCI_writes()	do { } while(0)

#define __get_CLKSW()		0UL
#define __get_CLKIN()		66000000UL

#define __addr_LEDS()		(__region_CS2 + 0x00000023UL)
#define __set_LEDS(X)		__builtin_write8((volatile void *) __addr_LEDS(), (X))

#define __addr_FPGATR()		(__region_CS2 + 0x00000030UL)
#define __set_FPGATR(X)		__builtin_write32((volatile void *) __addr_FPGATR(), (X))
#define __get_FPGATR()		__builtin_read32((volatile void *) __addr_FPGATR())

#define MB93093_FPGA_FPGATR_AUDIO_CLK	0x00000003

#define __set_FPGATR_AUDIO_CLK(V) \
	__set_FPGATR((__get_FPGATR() & ~MB93093_FPGA_FPGATR_AUDIO_CLK) | (V))

#define MB93093_FPGA_FPGATR_AUDIO_CLK_OFF	0x0
#define MB93093_FPGA_FPGATR_AUDIO_CLK_11MHz	0x1
#define MB93093_FPGA_FPGATR_AUDIO_CLK_12MHz	0x2
#define MB93093_FPGA_FPGATR_AUDIO_CLK_02MHz	0x3

#define MB93093_FPGA_SWR_PUSHSWMASK	(0x1F<<26)
#define MB93093_FPGA_SWR_PUSHSW4	(1<<29)

#define __addr_FPGA_SWR		((volatile void *)(__region_CS2 + 0x28UL))
#define __get_FPGA_PUSHSW1_5()	(__builtin_read32(__addr_FPGA_SWR) & MB93093_FPGA_SWR_PUSHSWMASK)


#endif

#endif /* _ASM_MB_REGS_H */
