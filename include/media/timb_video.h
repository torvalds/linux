/*
 * timb_video.h Platform struct for the Timberdale video driver
 * Copyright (c) 2009-2010 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _TIMB_VIDEO_
#define _TIMB_VIDEO_ 1

#include <linux/i2c.h>

struct timb_video_platform_data {
	int dma_channel;
	int i2c_adapter; /* The I2C adapter where the encoder is attached */
	struct {
		const char *module_name;
		struct i2c_board_info *info;
	} encoder;
};

#endif
