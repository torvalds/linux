/* include/asm-arm/arch-lh7a40x/constants.h
 *
 *  Copyright (C) 2004 Coastal Environmental Systems
 *  Copyright (C) 2004 Logic Product Development
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#ifndef __ASM_ARCH_CONSTANTS_H
#define __ASM_ARCH_CONSTANTS_H


/* Addressing constants */

	/* SoC CPU IO addressing */
#define IO_PHYS			(0x80000000)
#define IO_VIRT			(0xf8000000)
#define IO_SIZE			(0x0000B000)

#ifdef CONFIG_MACH_KEV7A400
# define CPLD_PHYS		(0x20000000)
# define CPLD_VIRT		(0xf2000000)
# define CPLD_SIZE		PAGE_SIZE
#endif

#if defined (CONFIG_MACH_LPD7A400) || defined (CONFIG_MACH_LPD7A404)

# define IOBARRIER_PHYS		0xc0000000 /* Start of SDRAM */
/*# define IOBARRIER_PHYS		0x00000000 */ /* Start of flash */
# define IOBARRIER_VIRT		0xf0000000
# define IOBARRIER_SIZE		PAGE_SIZE

# define CF_PHYS		0x60200000
# define CF_VIRT		0xf6020000
# define CF_SIZE		(8*1024)

	/* The IO mappings for the LPD CPLD are, unfortunately, sparse.  */
# define CPLDX_PHYS(x)		(0x70000000 | ((x) << 20))
# define CPLDX_VIRT(x)		(0xf7000000 | ((x) << 16))
# define CPLD00_PHYS		CPLDX_PHYS (0x00) /* Wired LAN */
# define CPLD00_VIRT		CPLDX_VIRT (0x00)
# define CPLD00_SIZE		PAGE_SIZE
# define CPLD02_PHYS		CPLDX_PHYS (0x02)
# define CPLD02_VIRT		CPLDX_VIRT (0x02)
# define CPLD02_SIZE		PAGE_SIZE
# define CPLD06_PHYS		CPLDX_PHYS (0x06)
# define CPLD06_VIRT		CPLDX_VIRT (0x06)
# define CPLD06_SIZE		PAGE_SIZE
# define CPLD08_PHYS		CPLDX_PHYS (0x08)
# define CPLD08_VIRT		CPLDX_VIRT (0x08)
# define CPLD08_SIZE		PAGE_SIZE
# define CPLD0C_PHYS		CPLDX_PHYS (0x0c)
# define CPLD0C_VIRT		CPLDX_VIRT (0x0c)
# define CPLD0C_SIZE		PAGE_SIZE
# define CPLD0E_PHYS		CPLDX_PHYS (0x0e)
# define CPLD0E_VIRT		CPLDX_VIRT (0x0e)
# define CPLD0E_SIZE		PAGE_SIZE
# define CPLD10_PHYS		CPLDX_PHYS (0x10)
# define CPLD10_VIRT		CPLDX_VIRT (0x10)
# define CPLD10_SIZE		PAGE_SIZE
# define CPLD12_PHYS		CPLDX_PHYS (0x12)
# define CPLD12_VIRT		CPLDX_VIRT (0x12)
# define CPLD12_SIZE		PAGE_SIZE
# define CPLD14_PHYS		CPLDX_PHYS (0x14)
# define CPLD14_VIRT		CPLDX_VIRT (0x14)
# define CPLD14_SIZE		PAGE_SIZE
# define CPLD16_PHYS		CPLDX_PHYS (0x16)
# define CPLD16_VIRT		CPLDX_VIRT (0x16)
# define CPLD16_SIZE		PAGE_SIZE
# define CPLD18_PHYS		CPLDX_PHYS (0x18)
# define CPLD18_VIRT		CPLDX_VIRT (0x18)
# define CPLD18_SIZE		PAGE_SIZE
# define CPLD1A_PHYS		CPLDX_PHYS (0x1a)
# define CPLD1A_VIRT		CPLDX_VIRT (0x1a)
# define CPLD1A_SIZE		PAGE_SIZE
#endif

	/* Timing constants */

#define	XTAL_IN			14745600	/* 14.7456 MHz crystal */
#define PLL_CLOCK		(XTAL_IN * 21)	/* 309 MHz PLL clock */
#define MAX_HCLK_KHZ		100000		/* HCLK max limit ~100MHz */

#endif /* __ASM_ARCH_CONSTANTS_H */
