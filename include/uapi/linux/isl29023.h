/*
 * Copyright (C) 2011-2014 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef __UAPI_LINUX_ISL29023_H__
#define __UAPI_LINUX_ISL29023_H__

#include <linux/types.h>

#define ISL29023_PD_MODE	0x0
#define ISL29023_ALS_ONCE_MODE	0x1
#define ISL29023_IR_ONCE_MODE	0x2
#define ISL29023_ALS_CONT_MODE	0x5
#define ISL29023_IR_CONT_MODE	0x6

#define ISL29023_INT_PERSISTS_1		0x0
#define ISL29023_INT_PERSISTS_4		0x1
#define ISL29023_INT_PERSISTS_8		0x2
#define ISL29023_INT_PERSISTS_16	0x3

#define ISL29023_RES_16		0x0
#define ISL29023_RES_12		0x1
#define ISL29023_RES_8		0x2
#define ISL29023_RES_4		0x3

#define ISL29023_RANGE_1K	0x0
#define ISL29023_RANGE_4K	0x1
#define ISL29023_RANGE_16K	0x2
#define ISL29023_RANGE_64K	0x3

#endif
