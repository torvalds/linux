/*
 * Copyright (c) 2014-2017 Mentor Graphics Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */

#ifndef __LINUX_IMX_MEDIA_H__
#define __LINUX_IMX_MEDIA_H__

/*
 * events from the subdevs
 */
#define V4L2_EVENT_IMX_CLASS                V4L2_EVENT_PRIVATE_START
#define V4L2_EVENT_IMX_FRAME_INTERVAL_ERROR (V4L2_EVENT_IMX_CLASS + 1)

enum imx_ctrl_id {
	V4L2_CID_IMX_FIM_ENABLE = (V4L2_CID_USER_IMX_BASE + 0),
	V4L2_CID_IMX_FIM_NUM,
	V4L2_CID_IMX_FIM_TOLERANCE_MIN,
	V4L2_CID_IMX_FIM_TOLERANCE_MAX,
	V4L2_CID_IMX_FIM_NUM_SKIP,
	V4L2_CID_IMX_FIM_ICAP_EDGE,
	V4L2_CID_IMX_FIM_ICAP_CHANNEL,
};

#endif
