/*
 * include/asm-arm/arch-ixp2000/enp2611.h
 *
 * Register and other defines for Radisys ENP-2611
 *
 * Created 2004 by Lennert Buytenhek from the ixdp2x01 code.  The
 * original version carries the following notices:
 *
 * Original Author: Naeem Afzal <naeem.m.afzal@intel.com>
 * Maintainer: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright (C) 2002 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#ifndef __ENP2611_H
#define __ENP2611_H

#define ENP2611_CALEB_PHYS_BASE		0xc5000000
#define ENP2611_CALEB_VIRT_BASE		0xfe000000
#define ENP2611_CALEB_SIZE		0x00100000

#define ENP2611_PM3386_0_PHYS_BASE	0xc6000000
#define ENP2611_PM3386_0_VIRT_BASE	0xfe100000
#define ENP2611_PM3386_0_SIZE		0x00100000

#define ENP2611_PM3386_1_PHYS_BASE	0xc6400000
#define ENP2611_PM3386_1_VIRT_BASE	0xfe200000
#define ENP2611_PM3386_1_SIZE		0x00100000

#define ENP2611_GPIO_SCL		7
#define ENP2611_GPIO_SDA		6

#define IRQ_ENP2611_THERMAL		IRQ_IXP2000_GPIO4
#define IRQ_ENP2611_OPTION_BOARD	IRQ_IXP2000_GPIO3
#define IRQ_ENP2611_CALEB		IRQ_IXP2000_GPIO2
#define IRQ_ENP2611_PM3386_1		IRQ_IXP2000_GPIO1
#define IRQ_ENP2611_PM3386_0		IRQ_IXP2000_GPIO0


#endif
