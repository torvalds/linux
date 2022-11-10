/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright (C) 2021 ASPEED Technology Inc.
 */

#ifndef _UAPI_LINUX_ASPEED_VIDEO_H
#define _UAPI_LINUX_ASPEED_VIDEO_H

#include <linux/v4l2-controls.h>

// enum for aspeed video's v4l2 s_input
enum aspeed_video_input {
	VIDEO_INPUT_VGA = 0,
	VIDEO_INPUT_GFX,
	VIDEO_INPUT_MEM,
	VIDEO_INPUT_MAX
};

#define V4L2_CID_ASPEED_HQ_MODE			(V4L2_CID_USER_ASPEED_BASE  + 1)
#define V4L2_CID_ASPEED_HQ_JPEG_QUALITY		(V4L2_CID_USER_ASPEED_BASE  + 2)

#endif /* _UAPI_LINUX_ASPEED_VIDEO_H */
