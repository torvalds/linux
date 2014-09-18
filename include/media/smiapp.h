/*
 * include/media/smiapp.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __SMIAPP_H_
#define __SMIAPP_H_

#include <media/v4l2-subdev.h>

#define SMIAPP_NAME		"smiapp"

#define SMIAPP_DFL_I2C_ADDR	(0x20 >> 1) /* Default I2C Address */
#define SMIAPP_ALT_I2C_ADDR	(0x6e >> 1) /* Alternate I2C Address */

#define SMIAPP_CSI_SIGNALLING_MODE_CCP2_DATA_CLOCK	0
#define SMIAPP_CSI_SIGNALLING_MODE_CCP2_DATA_STROBE	1
#define SMIAPP_CSI_SIGNALLING_MODE_CSI2			2

#define SMIAPP_NO_XSHUTDOWN	-1

/*
 * Sometimes due to board layout considerations the camera module can be
 * mounted rotated. The typical rotation used is 180 degrees which can be
 * corrected by giving a default H-FLIP and V-FLIP in the sensor readout.
 * FIXME: rotation also changes the bayer pattern.
 */
enum smiapp_module_board_orient {
	SMIAPP_MODULE_BOARD_ORIENT_0 = 0,
	SMIAPP_MODULE_BOARD_ORIENT_180,
};

struct smiapp_flash_strobe_parms {
	u8 mode;
	u32 strobe_width_high_us;
	u16 strobe_delay;
	u16 stobe_start_point;
	u8 trigger;
};

struct smiapp_platform_data {
	/*
	 * Change the cci address if i2c_addr_alt is set.
	 * Both default and alternate cci addr need to be present
	 */
	unsigned short i2c_addr_dfl;	/* Default i2c addr */
	unsigned short i2c_addr_alt;	/* Alternate i2c addr */

	unsigned int nvm_size;			/* bytes */
	unsigned int ext_clk;			/* sensor external clk */

	unsigned int lanes;		/* Number of CSI-2 lanes */
	u8 csi_signalling_mode;		/* SMIAPP_CSI_SIGNALLING_MODE_* */
	const s64 *op_sys_clock;

	enum smiapp_module_board_orient module_board_orient;

	struct smiapp_flash_strobe_parms *strobe_setup;

	int (*set_xclk)(struct v4l2_subdev *sd, int hz);
	int xshutdown;			/* gpio or SMIAPP_NO_XSHUTDOWN */
};

#endif /* __SMIAPP_H_  */
