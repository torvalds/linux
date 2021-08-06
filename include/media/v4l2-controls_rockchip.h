/*
 *************************************************************************
 * Rockchip driver for CIF ISP 1.0
 * (Based on Intel driver for sofiaxxx)
 *
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *************************************************************************
 */

#ifndef _V4L2_CONTROLS_ROCKCHIP_H
#define _V4L2_CONTROLS_ROCKCHIP_H

#include <linux/videodev2.h>

#define V4L2_CID_USER_RK_BASE (V4L2_CID_USER_BASE + 0x1080)

#define RK_V4L2_CID_AUDIO_SAMPLING_RATE (V4L2_CID_USER_RK_BASE + 0x100)
#define RK_V4L2_CID_AUDIO_PRESENT (V4L2_CID_USER_RK_BASE + 0x101)

#endif
