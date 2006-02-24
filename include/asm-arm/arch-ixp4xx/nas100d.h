/*
 * include/asm-arm/arch-ixp4xx/nas100d.h
 *
 * NAS100D platform specific definitions
 *
 * Copyright (c) 2005 Tower Technologies
 *
 * Author: Alessandro Zummo <a.zummo@towertech.it>
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

#define NAS100D_SDA_PIN		5
#define NAS100D_SCL_PIN		6

/*
 * NAS100D PCI IRQs
 */
#define NAS100D_PCI_MAX_DEV	3
#define NAS100D_PCI_IRQ_LINES	3


/* PCI controller GPIO to IRQ pin mappings */
#define NAS100D_PCI_INTA_PIN	11
#define NAS100D_PCI_INTB_PIN	10
#define NAS100D_PCI_INTC_PIN	9
#define NAS100D_PCI_INTD_PIN	8
#define NAS100D_PCI_INTE_PIN	7

/* GPIO */

#define NAS100D_GPIO0           0
#define NAS100D_GPIO1           1
#define NAS100D_GPIO2           2
#define NAS100D_GPIO3           3
#define NAS100D_GPIO4           4
#define NAS100D_GPIO5           5
#define NAS100D_GPIO6           6
#define NAS100D_GPIO7           7
#define NAS100D_GPIO8           8
#define NAS100D_GPIO9           9
#define NAS100D_GPIO10          10
#define NAS100D_GPIO11          11
#define NAS100D_GPIO12          12
#define NAS100D_GPIO13          13
#define NAS100D_GPIO14          14
#define NAS100D_GPIO15          15


/* Buttons */

#define NAS100D_PB_GPIO         NAS100D_GPIO14
#define NAS100D_RB_GPIO         NAS100D_GPIO4
#define NAS100D_PO_GPIO         NAS100D_GPIO12   /* power off */

#define NAS100D_PB_IRQ          IRQ_IXP4XX_GPIO14
#define NAS100D_RB_IRQ          IRQ_IXP4XX_GPIO4

/*
#define NAS100D_PB_BM           (1L << NAS100D_PB_GPIO)
#define NAS100D_PO_BM           (1L << NAS100D_PO_GPIO)
#define NAS100D_RB_BM           (1L << NAS100D_RB_GPIO)
*/
