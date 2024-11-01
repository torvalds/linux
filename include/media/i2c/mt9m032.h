/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Driver for MT9M032 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2010-2011 Lund Engineering
 * Contact: Gil Lund <gwlund@lundeng.com>
 * Author: Martin Hostettler <martin@neutronstar.dyndns.org>
 */

#ifndef MT9M032_H
#define MT9M032_H

#define MT9M032_NAME		"mt9m032"
#define MT9M032_I2C_ADDR	(0xb8 >> 1)

struct mt9m032_platform_data {
	u32 ext_clock;
	u32 pix_clock;
	bool invert_pixclock;

};
#endif /* MT9M032_H */
