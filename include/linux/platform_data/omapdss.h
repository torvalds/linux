/*
 * Copyright (C) 2016 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __OMAPDSS_PDATA_H
#define __OMAPDSS_PDATA_H

enum omapdss_version {
	OMAPDSS_VER_UNKNOWN = 0,
	OMAPDSS_VER_OMAP24xx,
	OMAPDSS_VER_OMAP34xx_ES1,	/* OMAP3430 ES1.0, 2.0 */
	OMAPDSS_VER_OMAP34xx_ES3,	/* OMAP3430 ES3.0+ */
	OMAPDSS_VER_OMAP3630,
	OMAPDSS_VER_AM35xx,
	OMAPDSS_VER_OMAP4430_ES1,	/* OMAP4430 ES1.0 */
	OMAPDSS_VER_OMAP4430_ES2,	/* OMAP4430 ES2.0, 2.1, 2.2 */
	OMAPDSS_VER_OMAP4,		/* All other OMAP4s */
	OMAPDSS_VER_OMAP5,
	OMAPDSS_VER_AM43xx,
	OMAPDSS_VER_DRA7xx,
};

/* Board specific data */
struct omap_dss_board_info {
	const char *default_display_name;
	int (*dsi_enable_pads)(int dsi_id, unsigned int lane_mask);
	void (*dsi_disable_pads)(int dsi_id, unsigned int lane_mask);
	int (*set_min_bus_tput)(struct device *dev, unsigned long r);
	enum omapdss_version version;
};

#endif /* __OMAPDSS_PDATA_H */
