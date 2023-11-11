/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2008
 * Guennadi Liakhovetski, DENX Software Engineering, <lg@denx.de>
 */

#ifndef __ASM_ARCH_MX3FB_H__
#define __ASM_ARCH_MX3FB_H__

#include <linux/device.h>
#include <linux/fb.h>

/* Proprietary FB_SYNC_ flags */
#define FB_SYNC_OE_ACT_HIGH	0x80000000
#define FB_SYNC_CLK_INVERT	0x40000000
#define FB_SYNC_DATA_INVERT	0x20000000
#define FB_SYNC_CLK_IDLE_EN	0x10000000
#define FB_SYNC_SHARP_MODE	0x08000000
#define FB_SYNC_SWAP_RGB	0x04000000
#define FB_SYNC_CLK_SEL_EN	0x02000000

/*
 * Specify the way your display is connected. The IPU can arbitrarily
 * map the internal colors to the external data lines. We only support
 * the following mappings at the moment.
 */
enum disp_data_mapping {
	/* blue -> d[0..5], green -> d[6..11], red -> d[12..17] */
	IPU_DISP_DATA_MAPPING_RGB666,
	/* blue -> d[0..4], green -> d[5..10], red -> d[11..15] */
	IPU_DISP_DATA_MAPPING_RGB565,
	/* blue -> d[0..7], green -> d[8..15], red -> d[16..23] */
	IPU_DISP_DATA_MAPPING_RGB888,
};

/**
 * struct mx3fb_platform_data - mx3fb platform data
 *
 * @dma_dev:	pointer to the dma-device, used for dma-slave connection
 * @mode:	pointer to a platform-provided per mxc_register_fb() videomode
 */
struct mx3fb_platform_data {
	struct device			*dma_dev;
	const char			*name;
	const struct fb_videomode	*mode;
	int				num_modes;
	enum disp_data_mapping		disp_data_fmt;
};

#endif
