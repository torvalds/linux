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
 * This file is licensed under the terms of the GNU General Public
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

/* Buttons */

#define NAS100D_PB_GPIO         14   /* power button */
#define NAS100D_RB_GPIO         4    /* reset button */

/* Power control */

#define NAS100D_PO_GPIO         12   /* power off */

/* LEDs */

#define NAS100D_LED_WLAN_GPIO	0
#define NAS100D_LED_DISK_GPIO	3
#define NAS100D_LED_PWR_GPIO	15
