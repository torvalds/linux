/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021 ASPEED Technology Inc.
 */

#ifndef _UAPI_LINUX_ASPEED_VIDEO_H
#define _UAPI_LINUX_ASPEED_VIDEO_H

#include <linux/v4l2-controls.h>

#define V4L2_CID_ASPEED_COMPRESSION_SCHEME	(V4L2_CID_USER_ASPEED_BASE  + 1)
#define V4L2_CID_ASPEED_HQ_MODE			(V4L2_CID_USER_ASPEED_BASE  + 2)
#define V4L2_CID_ASPEED_HQ_JPEG_QUALITY		(V4L2_CID_USER_ASPEED_BASE  + 3)

#endif /* _UAPI_LINUX_ASPEED_VIDEO_H */
