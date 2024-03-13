/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * include/uapi/linux/smiapp.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2014 Intel Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#ifndef __UAPI_LINUX_SMIAPP_H_
#define __UAPI_LINUX_SMIAPP_H_

#define V4L2_SMIAPP_TEST_PATTERN_MODE_DISABLED			0
#define V4L2_SMIAPP_TEST_PATTERN_MODE_SOLID_COLOUR		1
#define V4L2_SMIAPP_TEST_PATTERN_MODE_COLOUR_BARS		2
#define V4L2_SMIAPP_TEST_PATTERN_MODE_COLOUR_BARS_GREY		3
#define V4L2_SMIAPP_TEST_PATTERN_MODE_PN9			4

#endif /* __UAPI_LINUX_SMIAPP_H_ */
