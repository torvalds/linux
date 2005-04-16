/*
 * include/asm-arm/arch-ixp4xx/ixdp425.h
 *
 * IXDP425 platform specific definitions
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (c) MontaVista, Software, Inc. 
 * 
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */

#ifndef __ASM_ARCH_HARDWARE_H__
#error "Do not include this directly, instead #include <asm/hardware.h>"
#endif

#define	IXDP425_FLASH_BASE	IXP4XX_EXP_BUS_CS0_BASE_PHYS
#define	IXDP425_FLASH_SIZE	IXP4XX_EXP_BUS_CSX_REGION_SIZE

#define	IXDP425_SDA_PIN		7
#define	IXDP425_SCL_PIN		6

/*
 * IXDP425 PCI IRQs
 */
#define IXDP425_PCI_MAX_DEV	4
#define IXDP425_PCI_IRQ_LINES	4


/* PCI controller GPIO to IRQ pin mappings */
#define IXDP425_PCI_INTA_PIN	11
#define IXDP425_PCI_INTB_PIN	10
#define	IXDP425_PCI_INTC_PIN	9
#define	IXDP425_PCI_INTD_PIN	8


