/*
 * Platform data for Renesas R-Car VIN soc-camera driver
 *
 * Copyright (C) 2011-2013 Renesas Solutions Corp.
 * Copyright (C) 2013 Cogent Embedded, Inc., <source@cogentembedded.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __CAMERA_RCAR_H_
#define __CAMERA_RCAR_H_

#define RCAR_VIN_HSYNC_ACTIVE_LOW	(1 << 0)
#define RCAR_VIN_VSYNC_ACTIVE_LOW	(1 << 1)
#define RCAR_VIN_BT601			(1 << 2)
#define RCAR_VIN_BT656			(1 << 3)

struct rcar_vin_platform_data {
	unsigned int flags;
};

#endif /* __CAMERA_RCAR_H_ */
