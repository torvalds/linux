/*
 * Copyright 2010 Wolfram Sang <w.sang@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#ifndef __ASM_ARCH_IMX_ESDHC_H
#define __ASM_ARCH_IMX_ESDHC_H

#include <linux/types.h>

enum wp_types {
	ESDHC_WP_NONE,		/* no WP, neither controller nor gpio */
	ESDHC_WP_CONTROLLER,	/* mmc controller internal WP */
	ESDHC_WP_GPIO,		/* external gpio pin for WP */
};

enum cd_types {
	ESDHC_CD_NONE,		/* no CD, neither controller nor gpio */
	ESDHC_CD_CONTROLLER,	/* mmc controller internal CD */
	ESDHC_CD_GPIO,		/* external gpio pin for CD */
	ESDHC_CD_PERMANENT,	/* no CD, card permanently wired to host */
};

/**
 * struct esdhc_platform_data - platform data for esdhc on i.MX
 *
 * ESDHC_WP(CD)_CONTROLLER type is not available on i.MX25/35.
 *
 * @wp_gpio:	gpio for write_protect
 * @cd_gpio:	gpio for card_detect interrupt
 * @wp_type:	type of write_protect method (see wp_types enum above)
 * @cd_type:	type of card_detect method (see cd_types enum above)
 */

struct esdhc_platform_data {
	unsigned int wp_gpio;
	unsigned int cd_gpio;
	enum wp_types wp_type;
	enum cd_types cd_type;
	int max_bus_width;
	unsigned int delay_line;
	unsigned int tuning_step;       /* The delay cell steps in tuning procedure */
	unsigned int tuning_start_tap;	/* The start delay cell point in tuning procedure */
};
#endif /* __ASM_ARCH_IMX_ESDHC_H */
