/* SPDX-License-Identifier: ((GPL-2.0+ WITH Linux-syscall-note) OR MIT)
 *
 * Rockchip hdmirx driver
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 */

#ifndef _UAPI_RK_HDMIRX_CONFIG_H
#define _UAPI_RK_HDMIRX_CONFIG_H

#include <linux/types.h>
#include <linux/v4l2-controls.h>

enum mute_type {
	MUTE_OFF = 0,
	MUTE_VIDEO = 1,
	MUTE_AUDIO = 2,
	MUTE_ALL = 3,
};

enum audio_stat {
	AUDIO_OFF = 0,
	AUDIO_ON = 1,
	AUDIO_UNSTABLE = 2,
};

enum input_mode {
	MODE_HDMI = 0,
	MODE_DVI = 1,
};

enum hdmirx_color_range {
	HDMIRX_DEFAULT_RANGE = 0,
	HDMIRX_LIMIT_RANGE = 1,
	HDMIRX_FULL_RANGE = 2,
};

enum hdmirx_video_standard {
	HDMIRX_XVYCC601 = 0,
	HDMIRX_XVYCC709 = 1,
	HDMIRX_SYCC601 = 2,
	HDMIRX_ADOBE_YCC601 = 3,
	HDMIRX_ADOBE_RGB = 4,
	HDMIRX_BT2020 = 5,
	HDMIRX_BT2020_RGB = 6,
};

/* Private v4l2 ioctl */
#define RK_HDMIRX_CMD_GET_FPS \
	_IOR('V', BASE_VIDIOC_PRIVATE + 0, int)

#define RK_HDMIRX_CMD_GET_SIGNAL_STABLE_STATUS \
	_IOR('V', BASE_VIDIOC_PRIVATE + 1, int)

#define RK_HDMIRX_CMD_GET_HDCP_STATUS \
	_IOR('V', BASE_VIDIOC_PRIVATE + 2, int)

#define RK_HDMIRX_CMD_SET_MUTE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, int)

#define RK_HDMIRX_CMD_SET_HPD \
	_IOW('V', BASE_VIDIOC_PRIVATE + 4, int)

#define RK_HDMIRX_CMD_SET_AUDIO_STATE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 5, int)

#define RK_HDMIRX_CMD_SOFT_RESET \
	_IO('V', BASE_VIDIOC_PRIVATE + 6)

#define RK_HDMIRX_CMD_RESET_AUDIO_FIFO \
	_IO('V', BASE_VIDIOC_PRIVATE + 7)

#define RK_HDMIRX_CMD_GET_INPUT_MODE \
	_IOR('V', BASE_VIDIOC_PRIVATE + 8, int)

#define RK_HDMIRX_CMD_GET_COLOR_RANGE \
	_IOR('V', BASE_VIDIOC_PRIVATE + 9, int)

#define RK_HDMIRX_CMD_GET_COLOR_SPACE \
	_IOR('V', BASE_VIDIOC_PRIVATE + 10, int)

/* Private v4l2 event */
#define RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST \
	(V4L2_EVENT_PRIVATE_START + 1)

#endif /* _UAPI_RK_HDMIRX_CONFIG_H */
