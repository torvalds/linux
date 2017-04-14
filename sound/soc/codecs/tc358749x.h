/*
 * tc358749x.h TC358749XBG ALSA SoC audio codec driver
 *
 * Copyright (c) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Roy <luoxiaotan@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 */

#ifndef _TC358749X_H
#define _TC358749X_H

#define TC358749X_FORCE_MUTE		0x8600
#define MUTE				0x1
#define FORCE_DMUTE_MASK		BIT(0)
#define FORCE_AMUTE_MASK		BIT(4)

#define TC358749X_FS_SET		0x8621
#define FS_SET_MASK			0xf
#define FS_44100			0x0
#define FS_48000			0x2
#define FS_32000			0x3
#define FS_22050			0x4
#define FS_24000			0x6
#define FS_88200			0x8
#define FS_96000			0xa
#define FS_176400			0xc
#define FS_192000			0xe

struct tc358749x_priv {
	struct regmap			*regmap;
	struct i2c_client		*client;
	struct device			*dev;
	struct gpio_desc		*gpio_power;
	struct gpio_desc		*gpio_power18;
	struct gpio_desc		*gpio_power33;
	struct gpio_desc		*gpio_csi_ctl;
	struct gpio_desc		*gpio_reset;
	struct gpio_desc		*gpio_stanby;
	struct gpio_desc		*gpio_int;
};

#endif
