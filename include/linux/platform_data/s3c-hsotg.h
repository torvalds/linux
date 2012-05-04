/* include/linux/platform_data/s3c-hsotg.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C USB2.0 High-speed / OtG platform information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

enum s3c_hsotg_dmamode {
	S3C_HSOTG_DMA_NONE,	/* do not use DMA at-all */
	S3C_HSOTG_DMA_ONLY,	/* always use DMA */
	S3C_HSOTG_DMA_DRV,	/* DMA is chosen by driver */
};

/**
 * struct s3c_hsotg_plat - platform data for high-speed otg/udc
 * @dma: Whether to use DMA or not.
 * @is_osc: The clock source is an oscillator, not a crystal
 */
struct s3c_hsotg_plat {
	enum s3c_hsotg_dmamode	dma;
	unsigned int		is_osc:1;
	int                     phy_type;

	int (*phy_init)(struct platform_device *pdev, int type);
	int (*phy_exit)(struct platform_device *pdev, int type);
};

extern void s3c_hsotg_set_platdata(struct s3c_hsotg_plat *pd);
