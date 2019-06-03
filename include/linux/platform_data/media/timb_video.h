/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * timb_video.h Platform struct for the Timberdale video driver
 * Copyright (c) 2009-2010 Intel Corporation
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
