/*
 * include/asm-arm/arch-at91/hardware.h
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
#elif defined(CONFIG_ARCH_AT91SAM9263)
#include <asm/arch/at91sam9263.h>
#elif defined(CONFIG_ARCH_AT91SAM9RL)
#include <asm/arch/at91sam9rl.h>
#elif defined(CONFIG_ARCH_AT91CAP9)
#include <asm/arch/at91cap9.h>
#elif defined(CONFIG_ARCH_AT91X40)
#include <asm/arch/at91x40.h>
#else
#error "Unsupported AT91 processor"
#endif


#ifdef CONFIG_MMU
/*
 * Remap the peripherals from address 0xFFF78000 .. 0xFFFFFFFF
 * to 0xFEF78000 .. 0xFF000000.  (544Kb)
 */
#define AT91_IO_PHYS_BASE	0xFFF78000
#define AT91_IO_VIRT_BASE	(0xFF000000 - AT91_IO_SIZE)
#else
/*
 * Identity mapping for the non MMU case.
 */
#define AT91_IO_PHYS_BASE	AT91_BASE_SYS
#define AT91_IO_VIRT_BASE	AT91_IO_PHYS_BASE
#endif

#define AT91_IO_SIZE		(0xFFFFFFFF - AT91_IO_PHYS_BASE + 1)

 /* Convert a physical IO address to virtual IO address */
#define AT91_IO_P2V(x)		((x) - AT91_IO_PHYS_BASE + AT91_IO_VIRT_BASE)

/*
 * Virtual to Physical Address mapping for IO devices.
 */
#define AT91_VA_BASE_SYS	AT91_IO_P2V(AT91_BASE_SYS)
#define AT91_VA_BASE_EMAC	AT91_IO_P2V(AT91RM9200_BASE_EMAC)

 /* Internal SRAM is mapped below the IO devices */
#define AT91_SRAM_MAX		SZ_1M
#define AT91_VIRT_BASE		(AT91_IO_VIRT_BASE - AT91_SRAM_MAX)

/* Serial ports */
#define ATMEL_MAX_UART		7		/* 6 USART3's and one DBGU port (SAM9260) */

/* External Memory Map */
#define AT91_CHIPSELECT_0	0x10000000
#define AT91_CHIPSELECT_1	0x20000000
#define AT91_CHIPSELECT_2	0x30000000
#define AT91_CHIPSELECT_3	0x40000000
#define AT91_CHIPSELECT_4	0x50000000
#define AT91_CHIPSELECT_5	0x60000000
#define AT91_CHIPSELECT_6	0x70000000
#define AT91_CHIPSELECT_7	0x80000000

/* SDRAM */
#ifdef CONFIG_DRAM_BASE
#define AT91_SDRAM_BASE		CONFIG_DRAM_BASE
#else
#define AT91_SDRAM_BASE		AT91_CHIPSELECT_1
#endif

/* Clocks */
#define AT91_SLOW_CLOCK		32768		/* slow clock */


#endif
