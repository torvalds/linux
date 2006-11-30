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

#if defined(CONFIG_ARCH_AT91RM9200)
#include <asm/arch/at91rm9200.h>
#elif defined(CONFIG_ARCH_AT91SAM9260)
#include <asm/arch/at91sam9260.h>
#elif defined(CONFIG_ARCH_AT91SAM9261)
#include <asm/arch/at91sam9261.h>
#else
#error "Unsupported AT91 processor"
#endif


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
#define AT91_VA_BASE_SPI	AT91_IO_P2V(AT91RM9200_BASE_SPI)
#define AT91_VA_BASE_EMAC	AT91_IO_P2V(AT91RM9200_BASE_EMAC)
#define AT91_VA_BASE_TWI	AT91_IO_P2V(AT91RM9200_BASE_TWI)
#define AT91_VA_BASE_MCI	AT91_IO_P2V(AT91RM9200_BASE_MCI)
#define AT91_VA_BASE_UDP	AT91_IO_P2V(AT91RM9200_BASE_UDP)

 /* Internal SRAM is mapped below the IO devices */
#define AT91_SRAM_VIRT_BASE	(AT91_IO_VIRT_BASE - AT91RM9200_SRAM_SIZE)

/* Serial ports */
#define ATMEL_MAX_UART		5		/* 4 USART3's and one DBGU port */

/* FLASH */
#define AT91_FLASH_BASE		0x10000000	/* NCS0: Flash physical base address */

/* SDRAM */
#define AT91_SDRAM_BASE		0x20000000	/* NCS1: SDRAM physical base address */

/* SmartMedia */
#define AT91_SMARTMEDIA_BASE	0x40000000	/* NCS3: Smartmedia physical base address */

/* Compact Flash */
#define AT91_CF_BASE		0x50000000	/* NCS4-NCS6: Compact Flash physical base address */

/* Clocks */
#define AT91_SLOW_CLOCK		32768		/* slow clock */

#ifndef __ASSEMBLY__
#include <asm/io.h>

static inline unsigned int at91_sys_read(unsigned int reg_offset)
{
	void __iomem *addr = (void __iomem *)AT91_VA_BASE_SYS;

	return __raw_readl(addr + reg_offset);
}

static inline void at91_sys_write(unsigned int reg_offset, unsigned long value)
{
	void __iomem *addr = (void __iomem *)AT91_VA_BASE_SYS;

	__raw_writel(value, addr + reg_offset);
}
#endif

#endif
