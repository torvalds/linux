/*
 * A V4L2 driver for OmniVision OV7670 cameras.
 *
 * Copyright 2010 One Laptop Per Child
 *
 * This file may be distributed under the terms of the GNU General
 * Public License, version 2.
 */

#ifndef __OV7670_H
#define __OV7670_H

struct ov7670_config {
	int min_width;			/* Filter out smaller sizes */
	int min_height;			/* Filter out smaller sizes */
	int clock_speed;		/* External clock speed (MHz) */
	bool use_smbus;			/* Use smbus I/O instead of I2C */
	bool pll_bypass;		/* Choose whether to bypass the PLL */
};

#endif
