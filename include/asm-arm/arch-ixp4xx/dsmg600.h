/*
 * DSM-G600 platform specific definitions
 *
 * Copyright (C) 2006 Tower Technologies
 * Author: Alessandro Zummo <a.zummo@towertech.it>
 *
 * based on ixdp425.h:
 *	Copyright 2004 (C) MontaVista, Software, Inc.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <asm/hardware.h>"
#endif

#define DSMG600_SDA_PIN		5
#define DSMG600_SCL_PIN		4

/*
 * DSMG600 PCI IRQs
 */
#define DSMG600_PCI_MAX_DEV	4
#define DSMG600_PCI_IRQ_LINES	3


/* PCI controller GPIO to IRQ pin mappings */
#define DSMG600_PCI_INTA_PIN	11
#define DSMG600_PCI_INTB_PIN	10
#define DSMG600_PCI_INTC_PIN	9
#define DSMG600_PCI_INTD_PIN	8
#define DSMG600_PCI_INTE_PIN	7
#define DSMG600_PCI_INTF_PIN	6

/* DSM-G600 Timer Setting */
#define DSMG600_FREQ 66000000

/* Buttons */

#define DSMG600_PB_GPIO		15	/* power button */
#define DSMG600_PB_BM		(1L << DSMG600_PB_GPIO)

#define DSMG600_RB_GPIO		3	/* reset button */

#define DSMG600_RB_IRQ		IRQ_IXP4XX_GPIO3

#define DSMG600_PO_GPIO		2	/* power off */

/* LEDs */

#define DSMG600_LED_PWR_GPIO	0
#define DSMG600_LED_PWR_BM	(1L << DSMG600_LED_PWR_GPIO)

#define DSMG600_LED_WLAN_GPIO	14
#define DSMG600_LED_WLAN_BM	(1L << DSMG600_LED_WLAN_GPIO)
