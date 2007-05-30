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
 * This file is licensed under the terms of the GNU General Public
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

/* Buttons */

#define NSLU2_PB_GPIO		5
#define NSLU2_PO_GPIO		8	/* power off */
#define NSLU2_RB_GPIO		12

#define NSLU2_PB_IRQ		IRQ_IXP4XX_GPIO5
#define NSLU2_RB_IRQ		IRQ_IXP4XX_GPIO12

#define NSLU2_PB_BM		(1L << NSLU2_PB_GPIO)
#define NSLU2_PO_BM		(1L << NSLU2_PO_GPIO)
#define NSLU2_RB_BM		(1L << NSLU2_RB_GPIO)

/* Buzzer */

#define NSLU2_GPIO_BUZZ		4
#define NSLU2_BZ_BM		(1L << NSLU2_GPIO_BUZZ)

/* LEDs */

#define NSLU2_LED_RED_GPIO	0
#define NSLU2_LED_GRN_GPIO	1

#define NSLU2_LED_RED_BM	(1L << NSLU2_LED_RED_GPIO)
#define NSLU2_LED_GRN_BM	(1L << NSLU2_LED_GRN_GPIO)

#define NSLU2_LED_DISK1_GPIO	3
#define NSLU2_LED_DISK2_GPIO	2

#define NSLU2_LED_DISK1_BM	(1L << NSLU2_LED_DISK1_GPIO)
#define NSLU2_LED_DISK2_BM	(1L << NSLU2_LED_DISK2_GPIO)


