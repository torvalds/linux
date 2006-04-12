/*
 *  linux/include/asm-arm/arch-ebsa285/hardware.h
 *
 *  Copyright (C) 1998-1999 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  This file contains the hardware definitions of the EBSA-285.
 */
#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include <linux/config.h>
#include <asm/arch/memory.h>

#ifdef CONFIG_ARCH_FOOTBRIDGE
/*   Virtual      Physical	Size
 * 0xff800000	0x40000000	1MB	X-Bus
 * 0xff000000	0x7c000000	1MB	PCI I/O space
 * 0xfe000000	0x42000000	1MB	CSR
 * 0xfd000000	0x78000000	1MB	Outbound write flush (not supported)
 * 0xfc000000	0x79000000	1MB	PCI IACK/special space
 * 0xfb000000	0x7a000000	16MB	PCI Config type 1
 * 0xfa000000	0x7b000000	16MB	PCI Config type 0
 * 0xf9000000	0x50000000	1MB	Cache flush
 * 0xf0000000	0x80000000	16MB	ISA memory
 */
#define XBUS_SIZE		0x00100000
#define XBUS_BASE		0xff800000

#define PCIO_SIZE		0x00100000
#define PCIO_BASE		0xff000000

#define ARMCSR_SIZE		0x00100000
#define ARMCSR_BASE		0xfe000000

#define WFLUSH_SIZE		0x00100000
#define WFLUSH_BASE		0xfd000000

#define PCIIACK_SIZE		0x00100000
#define PCIIACK_BASE		0xfc000000

#define PCICFG1_SIZE		0x01000000
#define PCICFG1_BASE		0xfb000000

#define PCICFG0_SIZE		0x01000000
#define PCICFG0_BASE		0xfa000000

#define PCIMEM_SIZE		0x01000000
#define PCIMEM_BASE		0xf0000000

#elif defined(CONFIG_ARCH_CO285)
/*
 * This is the COEBSA285 cut-down mapping
 */
#define PCIMEM_SIZE		0x80000000
#define PCIMEM_BASE		0x80000000

#define WFLUSH_SIZE		0x01000000
#define WFLUSH_BASE		0x7d000000

#define ARMCSR_SIZE		0x00100000
#define ARMCSR_BASE		0x7cf00000

#define XBUS_SIZE		0x00020000
#define XBUS_BASE		0x7cee0000

#define PCIO_SIZE		0x00010000
#define PCIO_BASE		0x7ced0000

#else

#error "Undefined footbridge architecture"

#endif

#define XBUS_LEDS		((volatile unsigned char *)(XBUS_BASE + 0x12000))
#define XBUS_LED_AMBER		(1 << 0)
#define XBUS_LED_GREEN		(1 << 1)
#define XBUS_LED_RED		(1 << 2)
#define XBUS_LED_TOGGLE		(1 << 8)

#define XBUS_SWITCH		((volatile unsigned char *)(XBUS_BASE + 0x12000))
#define XBUS_SWITCH_SWITCH	((*XBUS_SWITCH) & 15)
#define XBUS_SWITCH_J17_13	((*XBUS_SWITCH) & (1 << 4))
#define XBUS_SWITCH_J17_11	((*XBUS_SWITCH) & (1 << 5))
#define XBUS_SWITCH_J17_9	((*XBUS_SWITCH) & (1 << 6))

#define UNCACHEABLE_ADDR	(ARMCSR_BASE + 0x108)


/* PIC irq control */
#define PIC_LO			0x20
#define PIC_MASK_LO		0x21
#define PIC_HI			0xA0
#define PIC_MASK_HI		0xA1

/* GPIO pins */
#define GPIO_CCLK		0x800
#define GPIO_DSCLK		0x400
#define GPIO_E2CLK		0x200
#define GPIO_IOLOAD		0x100
#define GPIO_RED_LED		0x080
#define GPIO_WDTIMER		0x040
#define GPIO_DATA		0x020
#define GPIO_IOCLK		0x010
#define GPIO_DONE		0x008
#define GPIO_FAN		0x004
#define GPIO_GREEN_LED		0x002
#define GPIO_RESET		0x001

/* CPLD pins */
#define CPLD_DS_ENABLE		8
#define CPLD_7111_DISABLE	4
#define CPLD_UNMUTE		2
#define CPLD_FLASH_WR_ENABLE	1

#ifndef __ASSEMBLY__
extern void gpio_modify_op(int mask, int set);
extern void gpio_modify_io(int mask, int in);
extern int  gpio_read(void);
extern void cpld_modify(int mask, int set);
#endif

#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM 	0x81000000

#endif
