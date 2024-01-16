/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * max2175.h
 *
 * Maxim Integrated MAX2175 RF to Bits tuner driver - user space header file.
 *
 * Copyright (C) 2016 Maxim Integrated Products
 * Copyright (C) 2017 Renesas Electronics Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __UAPI_MAX2175_H_
#define __UAPI_MAX2175_H_

#include <linux/v4l2-controls.h>

#define V4L2_CID_MAX2175_I2S_ENABLE	(V4L2_CID_USER_MAX217X_BASE + 0x01)
#define V4L2_CID_MAX2175_HSLS		(V4L2_CID_USER_MAX217X_BASE + 0x02)
#define V4L2_CID_MAX2175_RX_MODE	(V4L2_CID_USER_MAX217X_BASE + 0x03)

#endif /* __UAPI_MAX2175_H_ */
