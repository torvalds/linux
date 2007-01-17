/*
 * include/asm-arm/arch-ixp4xx/nslu2.h
 *
 * NSLU2 platform specific definitions
 *
 * Author: Mark Rakes <mrakes AT mac.com>
 * Maintainers: http://www.nslu2-linux.org
 *
 * based on ixdp425.h:
 *	Copyright 2004 (c) MontaVista, Software, Inc.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <asm/hardware.h>"
#endif

#define NSLU2_SDA_PIN		7
#define NSLU2_SCL_PIN		6

/*
 * NSLU2 PCI IRQs
 */
#define NSLU2_PCI_MAX_DEV	3
#define NSLU2_PCI_IRQ_LINES	3


/* PCI controller GPIO to IRQ pin mappings */
#define NSLU2_PCI_INTA_PIN	11
#define NSLU2_PCI_INTB_PIN	10
#define NSLU2_PCI_INTC_PIN	9
#define NSLU2_PCI_INTD_PIN	8


/* NSLU2 Timer */
#define NSLU2_FREQ 66000000
#define NSLU2_CLOCK_TICK_RATE (((NSLU2_FREQ / HZ & ~IXP4XX_OST_RELOAD_MASK) + 1) * HZ)
#define NSLU2_CLOCK_TICKS_PER_USEC ((NSLU2_CLOCK_TICK_RATE + USEC_PER_SEC/2) / USEC_PER_SEC)

/* GPIO */

#define NSLU2_GPIO0		0
#define NSLU2_GPIO1		1
#define NSLU2_GPIO2		2
#define NSLU2_GPIO3		3
#define NSLU2_GPIO4		4
#define NSLU2_GPIO5		5
#define NSLU2_GPIO6		6
#define NSLU2_GPIO7		7
#define NSLU2_GPIO8		8
#define NSLU2_GPIO9		9
#define NSLU2_GPIO10		10
#define NSLU2_GPIO11		11
#define NSLU2_GPIO12		12
#define NSLU2_GPIO13		13
#define NSLU2_GPIO14		14
#define NSLU2_GPIO15		15

/* Buttons */

#define NSLU2_PB_GPIO		NSLU2_GPIO5
#define NSLU2_PO_GPIO		NSLU2_GPIO8	/* power off */
#define NSLU2_RB_GPIO		NSLU2_GPIO12

#define NSLU2_PB_IRQ		IRQ_IXP4XX_GPIO5
#define NSLU2_RB_IRQ		IRQ_IXP4XX_GPIO12

#define NSLU2_PB_BM		(1L << NSLU2_PB_GPIO)
#define NSLU2_PO_BM		(1L << NSLU2_PO_GPIO)
#define NSLU2_RB_BM		(1L << NSLU2_RB_GPIO)

/* Buzzer */

#define NSLU2_GPIO_BUZZ		4
#define NSLU2_BZ_BM		(1L << NSLU2_GPIO_BUZZ)

/* LEDs */

#define NSLU2_LED_RED		NSLU2_GPIO0
#define NSLU2_LED_GRN		NSLU2_GPIO1

#define NSLU2_LED_RED_BM	(1L << NSLU2_LED_RED)
#define NSLU2_LED_GRN_BM	(1L << NSLU2_LED_GRN)

#define NSLU2_LED_DISK1		NSLU2_GPIO3
#define NSLU2_LED_DISK2		NSLU2_GPIO2

#define NSLU2_LED_DISK1_BM	(1L << NSLU2_GPIO2)
#define NSLU2_LED_DISK2_BM	(1L << NSLU2_GPIO3)


