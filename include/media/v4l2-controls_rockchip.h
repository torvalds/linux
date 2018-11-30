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
#include <media/v4l2-config_rockchip.h>

#define RK_VIDIOC_CAMERA_MODULEINFO \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 10, struct camera_module_info_s)
#define RK_VIDIOC_SENSOR_MODE_DATA \
	_IOR('v', BASE_VIDIOC_PRIVATE, struct isp_supplemental_sensor_mode_data)
#define RK_VIDIOC_SENSOR_CONFIGINFO \
	_IOR('v', BASE_VIDIOC_PRIVATE + 1, struct sensor_config_info_s)
#define RK_VIDIOC_SENSOR_REG_ACCESS \
	_IOWR('v', BASE_VIDIOC_PRIVATE + 2, struct sensor_reg_rw_s)

#define V4L2_CID_USER_RK_BASE (V4L2_CID_USER_BASE + 0x1080)
#define RK_V4L2_CID_VBLANKING (V4L2_CID_USER_RK_BASE + 1)
#define RK_V4L2_CID_GAIN_PERCENT (V4L2_CID_USER_RK_BASE + 2)
#define RK_V4L2_CID_AUTO_FPS (V4L2_CID_USER_RK_BASE + 3)
#define RK_V4L2_CID_VTS (V4L2_CID_USER_RK_BASE + 4)
#define RK_V4L2_CID_CLS_EXP (V4L2_CID_USER_RK_BASE + 5)

#endif
