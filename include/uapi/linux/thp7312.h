/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * THine THP7312 user space header file.
 *
 * Copyright (C) 2021 THine Electronics, Inc.
 * Copyright (C) 2023 Ideas on Board Oy
 */

#ifndef __UAPI_THP7312_H_
#define __UAPI_THP7312_H_

#include <linux/v4l2-controls.h>

#define V4L2_CID_THP7312_LOW_LIGHT_COMPENSATION		(V4L2_CID_USER_THP7312_BASE + 0x01)
#define V4L2_CID_THP7312_AUTO_FOCUS_METHOD		(V4L2_CID_USER_THP7312_BASE + 0x02)
#define V4L2_CID_THP7312_NOISE_REDUCTION_AUTO		(V4L2_CID_USER_THP7312_BASE + 0x03)
#define V4L2_CID_THP7312_NOISE_REDUCTION_ABSOLUTE	(V4L2_CID_USER_THP7312_BASE + 0x04)

#endif /* __UAPI_THP7312_H_ */
