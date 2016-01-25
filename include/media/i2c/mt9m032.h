/*
 * Driver for MT9M032 CMOS Image Sensor from Micron
 *
 * Copyright (C) 2010-2011 Lund Engineering
 * Contact: Gil Lund <gwlund@lundeng.com>
 * Author: Martin Hostettler <martin@neutronstar.dyndns.org>
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
