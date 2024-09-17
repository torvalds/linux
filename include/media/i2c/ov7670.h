/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * A V4L2 driver for OmniVision OV7670 cameras.
 *
 * Copyright 2010 One Laptop Per Child
 */

#ifndef __OV7670_H
#define __OV7670_H

struct ov7670_config {
	int min_width;			/* Filter out smaller sizes */
	int min_height;			/* Filter out smaller sizes */
	int clock_speed;		/* External clock speed (MHz) */
	bool use_smbus;			/* Use smbus I/O instead of I2C */
	bool pll_bypass;		/* Choose whether to bypass the PLL */
	bool pclk_hb_disable;		/* Disable toggling pixclk during horizontal blanking */
};

#endif
