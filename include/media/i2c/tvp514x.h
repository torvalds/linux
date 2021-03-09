/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/video/tvp514x.h
 *
 * Copyright (C) 2008 Texas Instruments Inc
 * Author: Vaibhav Hiremath <hvaibhav@ti.com>
 *
 * Contributors:
 *     Sivaraj R <sivaraj@ti.com>
 *     Brijesh R Jadav <brijesh.j@ti.com>
 *     Hardik Shah <hardik.shah@ti.com>
 *     Manjunath Hadli <mrh@ti.com>
 *     Karicheri Muralidharan <m-karicheri2@ti.com>
 */

#ifndef _TVP514X_H
#define _TVP514X_H

/*
 * Other macros
 */
#define TVP514X_MODULE_NAME		"tvp514x"

#define TVP514X_XCLK_BT656		(27000000)

/* Number of pixels and number of lines per frame for different standards */
#define NTSC_NUM_ACTIVE_PIXELS		(720)
#define NTSC_NUM_ACTIVE_LINES		(480)
#define PAL_NUM_ACTIVE_PIXELS		(720)
#define PAL_NUM_ACTIVE_LINES		(576)

/* enum for different decoder input pin configuration */
enum tvp514x_input {
	/*
	 * CVBS input selection
	 */
	INPUT_CVBS_VI1A = 0x0,
	INPUT_CVBS_VI1B,
	INPUT_CVBS_VI1C,
	INPUT_CVBS_VI2A = 0x04,
	INPUT_CVBS_VI2B,
	INPUT_CVBS_VI2C,
	INPUT_CVBS_VI3A = 0x08,
	INPUT_CVBS_VI3B,
	INPUT_CVBS_VI3C,
	INPUT_CVBS_VI4A = 0x0C,
	/*
	 * S-Video input selection
	 */
	INPUT_SVIDEO_VI2A_VI1A = 0x44,
	INPUT_SVIDEO_VI2B_VI1B,
	INPUT_SVIDEO_VI2C_VI1C,
	INPUT_SVIDEO_VI2A_VI3A = 0x54,
	INPUT_SVIDEO_VI2B_VI3B,
	INPUT_SVIDEO_VI2C_VI3C,
	INPUT_SVIDEO_VI4A_VI1A = 0x4C,
	INPUT_SVIDEO_VI4A_VI1B,
	INPUT_SVIDEO_VI4A_VI1C,
	INPUT_SVIDEO_VI4A_VI3A = 0x5C,
	INPUT_SVIDEO_VI4A_VI3B,
	INPUT_SVIDEO_VI4A_VI3C,

	/* Need to add entries for
	 * RGB, YPbPr and SCART.
	 */
	INPUT_INVALID
};

/* enum for output format supported. */
enum tvp514x_output {
	OUTPUT_10BIT_422_EMBEDDED_SYNC = 0,
	OUTPUT_20BIT_422_SEPERATE_SYNC,
	OUTPUT_10BIT_422_SEPERATE_SYNC = 3,
	OUTPUT_INVALID
};

/**
 * struct tvp514x_platform_data - Platform data values and access functions.
 * @clk_polarity: Clock polarity of the current interface.
 * @hs_polarity: HSYNC Polarity configuration for current interface.
 * @vs_polarity: VSYNC Polarity configuration for current interface.
 */
struct tvp514x_platform_data {
	/* Interface control params */
	bool clk_polarity;
	bool hs_polarity;
	bool vs_polarity;
};


#endif				/* ifndef _TVP514X_H */
