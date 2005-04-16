/* OmniVision* camera chip driver API
 *
 * Copyright (c) 1999-2004 Mark McClelland
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version. NO WARRANTY OF ANY KIND is expressed or implied.
 *
 * * OmniVision is a trademark of OmniVision Technologies, Inc. This driver
 * is not sponsored or developed by them.
 */

#ifndef __LINUX_OVCAMCHIP_H
#define __LINUX_OVCAMCHIP_H

#include <linux/videodev.h>
#include <linux/i2c.h>

/* Remove these once they are officially defined */
#ifndef I2C_DRIVERID_OVCAMCHIP
	#define I2C_DRIVERID_OVCAMCHIP	0xf00f
#endif
#ifndef I2C_HW_SMBUS_OV511
	#define I2C_HW_SMBUS_OV511	0xfe
#endif
#ifndef I2C_HW_SMBUS_OV518
	#define I2C_HW_SMBUS_OV518	0xff
#endif
#ifndef I2C_HW_SMBUS_OVFX2
	#define I2C_HW_SMBUS_OVFX2	0xfd
#endif

/* --------------------------------- */
/*           ENUMERATIONS            */
/* --------------------------------- */

/* Controls */
enum {
	OVCAMCHIP_CID_CONT,		/* Contrast */
	OVCAMCHIP_CID_BRIGHT,		/* Brightness */
	OVCAMCHIP_CID_SAT,		/* Saturation */
	OVCAMCHIP_CID_HUE,		/* Hue */
	OVCAMCHIP_CID_EXP,		/* Exposure */
	OVCAMCHIP_CID_FREQ,		/* Light frequency */
	OVCAMCHIP_CID_BANDFILT,		/* Banding filter */
	OVCAMCHIP_CID_AUTOBRIGHT,	/* Auto brightness */
	OVCAMCHIP_CID_AUTOEXP,		/* Auto exposure */
	OVCAMCHIP_CID_BACKLIGHT,	/* Back light compensation */
	OVCAMCHIP_CID_MIRROR,		/* Mirror horizontally */
};

/* Chip types */
#define NUM_CC_TYPES	9
enum {
	CC_UNKNOWN,
	CC_OV76BE,
	CC_OV7610,
	CC_OV7620,
	CC_OV7620AE,
	CC_OV6620,
	CC_OV6630,
	CC_OV6630AE,
	CC_OV6630AF,
};

/* --------------------------------- */
/*           I2C ADDRESSES           */
/* --------------------------------- */

#define OV7xx0_SID   (0x42 >> 1)
#define OV6xx0_SID   (0xC0 >> 1)

/* --------------------------------- */
/*                API                */
/* --------------------------------- */

struct ovcamchip_control {
	__u32 id;
	__s32 value;
};

struct ovcamchip_window {
	int x;
	int y;
	int width;
	int height;
	int format;
	int quarter;		/* Scale width and height down 2x */

	/* This stuff will be removed eventually */
	int clockdiv;		/* Clock divisor setting */
};

/* Commands */
#define OVCAMCHIP_CMD_Q_SUBTYPE     _IOR  (0x88, 0x00, int)
#define OVCAMCHIP_CMD_INITIALIZE    _IOW  (0x88, 0x01, int)
/* You must call OVCAMCHIP_CMD_INITIALIZE before any of commands below! */
#define OVCAMCHIP_CMD_S_CTRL        _IOW  (0x88, 0x02, struct ovcamchip_control)
#define OVCAMCHIP_CMD_G_CTRL        _IOWR (0x88, 0x03, struct ovcamchip_control)
#define OVCAMCHIP_CMD_S_MODE        _IOW  (0x88, 0x04, struct ovcamchip_window)
#define OVCAMCHIP_MAX_CMD           _IO   (0x88, 0x3f)

#endif
