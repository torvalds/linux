/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * mx2-cam.h - i.MX27/i.MX25 camera driver header file
 *
 * Copyright (C) 2003, Intel Corporation
 * Copyright (C) 2008, Sascha Hauer <s.hauer@pengutronix.de>
 * Copyright (C) 2010, Baruch Siach <baruch@tkos.co.il>
 */

#ifndef __MACH_MX2_CAM_H_
#define __MACH_MX2_CAM_H_

#define MX2_CAMERA_EXT_VSYNC		(1 << 1)
#define MX2_CAMERA_CCIR			(1 << 2)
#define MX2_CAMERA_CCIR_INTERLACE	(1 << 3)
#define MX2_CAMERA_HSYNC_HIGH		(1 << 4)
#define MX2_CAMERA_GATED_CLOCK		(1 << 5)
#define MX2_CAMERA_INV_DATA		(1 << 6)
#define MX2_CAMERA_PCLK_SAMPLE_RISING	(1 << 7)

/**
 * struct mx2_camera_platform_data - optional platform data for mx2_camera
 * @flags: any combination of MX2_CAMERA_*
 * @clk: clock rate of the csi block / 2
 */
struct mx2_camera_platform_data {
	unsigned long flags;
	unsigned long clk;
};

#endif /* __MACH_MX2_CAM_H_ */
