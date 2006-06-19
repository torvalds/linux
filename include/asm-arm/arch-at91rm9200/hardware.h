/*
 * include/asm-arm/arch-at91rm9200/hardware.h
 *
 *  Copyright (C) 2003 SAN People
 *  Copyright (C) 2003 ATMEL
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <asm/sizes.h>

#include <asm/arch/at91rm9200.h>
#include <asm/arch/at91rm9200_sys.h>

/*
 * Remap the peripherals from address 0xFFFA0000 .. 0xFFFFFFFF
 * to 0xFEFA0000 .. 0xFF000000.  (384Kb)
 */
#define AT91_IO_PHYS_BASE	0xFFFA0000
#define AT91_IO_SIZE		(0xFFFFFFFF - AT91_IO_PHYS_BASE + 1)
#define AT91_IO_VIRT_BASE	(0xFF000000 - AT91_IO_SIZE)

 /* Convert a physical IO address to virtual IO address */
#define AT91_IO_P2V(x)	((x) - AT91_IO_PHYS_BASE + AT91_IO_VIRT_BASE)

/*
 * Virtual to Physical Address mapping for IO devices.
 */
#define AT91_VA_BASE_SYS	AT91_IO_P2V(AT91_BASE_SYS)
#define AT91_VA_BASE_SPI	AT91_IO_P2V(AT91_BASE_SPI)
#define AT91_VA_BASE_SSC2	AT91_IO_P2V(AT91_BASE_SSC2)
#define AT91_VA_BASE_SSC1	AT91_IO_P2V(AT91_BASE_SSC1)
#define AT91_VA_BASE_SSC0	AT91_IO_P2V(AT91_BASE_SSC0)
#define AT91_VA_BASE_US3	AT91_IO_P2V(AT91_BASE_US3)
#define AT91_VA_BASE_US2	AT91_IO_P2V(AT91_BASE_US2)
#define AT91_VA_BASE_US1	AT91_IO_P2V(AT91_BASE_US1)
#define AT91_VA_BASE_US0	AT91_IO_P2V(AT91_BASE_US0)
#define AT91_VA_BASE_EMAC	AT91_IO_P2V(AT91_BASE_EMAC)
#define AT91_VA_BASE_TWI	AT91_IO_P2V(AT91_BASE_TWI)
#define AT91_VA_BASE_MCI	AT91_IO_P2V(AT91_BASE_MCI)
#define AT91_VA_BASE_UDP	AT91_IO_P2V(AT91_BASE_UDP)
#define AT91_VA_BASE_TCB1	AT91_IO_P2V(AT91_BASE_TCB1)
#define AT91_VA_BASE_TCB0	AT91_IO_P2V(AT91_BASE_TCB0)

/* Internal SRAM */
#define AT91_SRAM_BASE		0x00200000	/* Internal SRAM base address */
#define AT91_SRAM_SIZE		0x00004000	/* Internal SRAM SIZE (16Kb) */

 /* Internal SRAM is mapped below the IO devices */
#define AT91_SRAM_VIRT_BASE	(AT91_IO_VIRT_BASE - AT91_SRAM_SIZE)

/* Serial ports */
#define AT91_NR_UART		5		/* 4 USART3's and one DBGU port */

/* FLASH */
#define AT91_FLASH_BASE		0x10000000	/* NCS0: Flash physical base address */

/* SDRAM */
#define AT91_SDRAM_BASE		0x20000000	/* NCS1: SDRAM physical base address */

/* SmartMedia */
#define AT91_SMARTMEDIA_BASE	0x40000000	/* NCS3: Smartmedia physical base address */

/* Compact Flash */
#define AT91_CF_BASE		0x50000000	/* NCS4-NCS6: Compact Flash physical base address */

/* Multi-Master Memory controller */
#define AT91_UHP_BASE		0x00300000	/* USB Host controller */

/* Clocks */
#define AT91_SLOW_CLOCK		32768		/* slow clock */

#ifndef __ASSEMBLY__
#include <asm/io.h>

static inline unsigned int at91_sys_read(unsigned int reg_offset)
{
	void __iomem *addr = (void __iomem *)AT91_VA_BASE_SYS;

	return readl(addr + reg_offset);
}

static inline void at91_sys_write(unsigned int reg_offset, unsigned long value)
{
	void __iomem *addr = (void __iomem *)AT91_VA_BASE_SYS;

	writel(value, addr + reg_offset);
}
#endif

#endif
