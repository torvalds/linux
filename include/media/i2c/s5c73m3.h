/*
 * Samsung LSI S5C73M3 8M pixel camera driver
 *
 * Copyright (C) 2012, Samsung Electronics, Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Andrzej Hajda <a.hajda@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef MEDIA_S5C73M3__
#define MEDIA_S5C73M3__

#include <linux/videodev2.h>
#include <media/v4l2-mediabus.h>

/**
 * struct s5c73m3_platform_data - s5c73m3 driver platform data
 * @mclk_frequency: sensor's master clock frequency in Hz
 * @bus_type:    bus type
 * @nlanes:      maximum number of MIPI-CSI lanes used
 * @horiz_flip:  default horizontal image flip value, non zero to enable
 * @vert_flip:   default vertical image flip value, non zero to enable
 */

struct s5c73m3_platform_data {
	unsigned long mclk_frequency;

	enum v4l2_mbus_type bus_type;
	u8 nlanes;
	u8 horiz_flip;
	u8 vert_flip;
};

#endif /* MEDIA_S5C73M3__ */
