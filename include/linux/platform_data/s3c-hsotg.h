/* SPDX-License-Identifier: GPL-2.0-only */
/* include/linux/platform_data/s3c-hsotg.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C USB2.0 High-speed / OtG platform information
*/

#ifndef __LINUX_USB_S3C_HSOTG_H
#define __LINUX_USB_S3C_HSOTG_H

struct platform_device;

enum dwc2_hsotg_dmamode {
	S3C_HSOTG_DMA_NONE,	/* do not use DMA at-all */
	S3C_HSOTG_DMA_ONLY,	/* always use DMA */
	S3C_HSOTG_DMA_DRV,	/* DMA is chosen by driver */
};

/**
 * struct dwc2_hsotg_plat - platform data for high-speed otg/udc
 * @dma: Whether to use DMA or not.
 * @is_osc: The clock source is an oscillator, not a crystal
 */
struct dwc2_hsotg_plat {
	enum dwc2_hsotg_dmamode	dma;
	unsigned int		is_osc:1;
	int                     phy_type;

	int (*phy_init)(struct platform_device *pdev, int type);
	int (*phy_exit)(struct platform_device *pdev, int type);
};

extern void dwc2_hsotg_set_platdata(struct dwc2_hsotg_plat *pd);

#endif /* __LINUX_USB_S3C_HSOTG_H */
