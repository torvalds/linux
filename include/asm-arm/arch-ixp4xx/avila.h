/*
 * include/asm-arm/arch-ixp4xx/avila.h
 *
 * Gateworks Avila platform specific definitions
 *
 * Author: Michael-Luke Jones <mlj28@cam.ac.uk>
 *
 * Based on ixdp425.h
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

#define	AVILA_SDA_PIN		7
#define	AVILA_SCL_PIN		6

/*
 * AVILA PCI IRQs
 */
#define AVILA_PCI_MAX_DEV	4
#define LOFT_PCI_MAX_DEV    6
#define AVILA_PCI_IRQ_LINES	4


/* PCI controller GPIO to IRQ pin mappings */
#define AVILA_PCI_INTA_PIN	11
#define AVILA_PCI_INTB_PIN	10
#define AVILA_PCI_INTC_PIN	9
#define AVILA_PCI_INTD_PIN	8


