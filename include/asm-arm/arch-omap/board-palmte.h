/*
 * linux/include/asm-arm/arch-omap/board-palmte.h
 *
 * Hardware definitions for the Palm Tungsten E device.
 *
 * Maintainters :	http://palmtelinux.sf.net
 *			palmtelinux-developpers@lists.sf.net
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __OMAP_BOARD_PALMTE_H
#define __OMAP_BOARD_PALMTE_H

#include <asm/arch/gpio.h>

#define PALMTE_USBDETECT_GPIO	0
#define PALMTE_USB_OR_DC_GPIO	1
#define PALMTE_TSC_GPIO		4
#define PALMTE_PINTDAV_GPIO	6
#define PALMTE_MMC_WP_GPIO	8
#define PALMTE_MMC_POWER_GPIO	9
#define PALMTE_HDQ_GPIO		11
#define PALMTE_HEADPHONES_GPIO	14
#define PALMTE_SPEAKER_GPIO	15
#define PALMTE_DC_GPIO		OMAP_MPUIO(2)
#define PALMTE_MMC_SWITCH_GPIO	OMAP_MPUIO(4)
#define PALMTE_MMC1_GPIO	OMAP_MPUIO(6)
#define PALMTE_MMC2_GPIO	OMAP_MPUIO(7)
#define PALMTE_MMC3_GPIO	OMAP_MPUIO(11)

#endif	/* __OMAP_BOARD_PALMTE_H */
