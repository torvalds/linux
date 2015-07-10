/*
 * OMAP GPMC Platform data
 *
 * Copyright (C) 2014 Texas Instruments, Inc. - http://www.ti.com
 *	Roger Quadros <rogerq@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#ifndef _GPMC_OMAP_H_
#define _GPMC_OMAP_H_

/* Maximum Number of Chip Selects */
#define GPMC_CS_NUM		8

/* Data for each chip select */
struct gpmc_omap_cs_data {
	bool valid;			/* data is valid */
	bool is_nand;			/* device within this CS is NAND */
	struct platform_device *pdev;	/* device within this CS region */
	unsigned int pdata_size;
};

struct gpmc_omap_platform_data {
	struct gpmc_omap_cs_data cs[GPMC_CS_NUM];
};

#endif /* _GPMC_OMAP_H */
